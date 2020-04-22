/* > sensord.c */
/* Daniel F. Smith, 2014, 2018 */
#define _DEFAULT_SOURCE 1 /* for tm_gmtoff */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <math.h> /* isnan */
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdbool.h>

#include <sys/socket.h>
#include <netinet/in.h>

/* see https://github.com/dfsmith/stack-json */
#include "stack-json/src/json.h"

typedef unsigned int uint;

#ifdef TEST
#define TRACEp(x) x /* serial port */
#define TRACEs(x) x /* sockets */
#define TRACEc(x) x /* calculations */
#define TRACE(x) x
#define DBG(x) x
#else
#define TRACEp(x)
#define TRACEs(x)
#define TRACEc(x)
#define TRACE(x)
#define DBG(x)
#endif

#define JSONSOURCE "mosquitto_sub -t auto/sensor"

#define SENSORTTY "/dev/ttyarduino"
//#define SENSORTTY "testdata"

#define AVGTIME 60
#define MAXPROBES 10
#define PORT 8888

#define MAX(a,b) ((a)>(b)?(a):(b))
#define lengthof(x) (sizeof(x)/sizeof(*(x)))

typedef struct {
	const char *id;
	const char *location;
} probeinfo;

probeinfo probelookup[]={
	{"arduino-0","garage box"},
	{"arduino-1","garage outside"},
	{"arduino-2","garage inside"},
	{"arduino-3","garage door"},
	{"net:30:AE:A4:38:33:A8","kitchen"},
	{"net:A4:CF:12:24:97:84","master bedroom"},
};
static const int probelookups=lengthof(probelookup);

/* HTTP headers */
#define CRLF "\r\n"
#define LEN8MARKER "**LEN8**"
const char header_ok[]="HTTP/1.1 200 OK" CRLF "Content-Length: " LEN8MARKER CRLF
                       "Access-Control-Allow-Origin: *" CRLF
                       "Content-Type: text/plain" CRLF CRLF;
const char header_bad[]="HTTP/1.1 400 Bad request" CRLF CRLF;
const char header_notfound[]="HTTP/1.1 404 Not found" CRLF CRLF;
const char header_error[]="HTTP/1.1 500 Internal error" CRLF CRLF;

/* -- doubly linked circular lists -- */

#define dll__init(MAIN,NEW) (MAIN)=(NEW)->prev=(NEW)->next=(NEW)
#define dll__insert(AT,NEW) \
	(NEW)->prev=(AT)->prev; \
	(NEW)->next=(AT); \
	(AT)->prev->next=(NEW); \
	(AT)->prev=(NEW);
#define dll_addhead(MAIN,NEW) do { if (!(MAIN)) dll__init(MAIN,NEW); else {dll__insert((MAIN),NEW); (MAIN)=(NEW);}} while(0)
#define dll_addtail(MAIN,NEW) do { if (!(MAIN)) dll__init(MAIN,NEW); else {dll__insert((MAIN)->prev,NEW);}} while(0)
#define dll_remove(MAIN,OLD) \
	do { \
		(OLD)->next->prev=(OLD)->prev; \
		(OLD)->prev->next=(OLD)->next; \
		if ((OLD)==(MAIN)) (MAIN)=((OLD)->next==(MAIN))?NULL:(OLD)->next; \
	} while(0)
#define dll_empty(MAIN) ((MAIN)==NULL)
#define dll_head(MAIN) (MAIN)
#define dll_tail(MAIN) ((MAIN)?(MAIN)->prev:NULL)
#define for_dll(MAIN,P)             for((P)=(MAIN);      (P);(P)=((P)->next==(MAIN))      ?NULL:(P)->next)
#define for_dll_r(MAIN,P) if (MAIN) for((P)=(MAIN)->prev;(P);(P)=((P)->prev==(MAIN)->prev)?NULL:(P)->prev)

/* -- main program -- */

static const char *progname="sensord";
static struct timeval epoch; /* time sensord started */
static double epoch_s=0.0;

static double tvdiff(const struct timeval *end,const struct timeval *start) {
	double t;
	t=end->tv_sec - start->tv_sec;
	t+=(((1000000 + end->tv_usec) - start->tv_usec)*1e-6 - 1.0);
	return t;
}

static double tvsec(const struct timeval *tv) {
	double t;
	t=tv->tv_sec;
	t+=tv->tv_usec * 1e-6;
	return t;
}

static double timenow(void) {
	struct timeval tv;
	gettimeofday(&tv,NULL);
	if (epoch_s==0.0) {
		epoch=tv;
		epoch_s=tvsec(&epoch);
	}
	return tvdiff(&tv,&epoch);
}

/* -- probe information -- */

typedef struct {
	double time;
	double temp;
	double rh;
	double pressure;
	double state;

	uint havetime:1;
	uint havetemp:1;
	uint haverh:1;
	uint havepressure:1;
	uint havestate:1;
} measurement;
#define MEASUREMENT_NULL {0,0.0,0.0,0.0,0.0, 0,0,0,0,0}
const measurement measurement_null=MEASUREMENT_NULL;
/* zero is used for clearing stats */
#define MEASUREMENT_ZERO {0,0.0,0.0,0.0,0.0, 1,1,1,1,1}
const measurement measurement_zero=MEASUREMENT_ZERO;

struct probedata_s {
	struct probedata_s *next,*prev;
	int probe;
	measurement m;
};

struct probeavg_s {
	struct probedata_s *list;
	int sumreset; /* when 0, recalculate sum */

	measurement offset;	/* may be needed to reduce rounding errors in, e.g., seconds from epoch */

	measurement sum;	/* sum of values in list */
	measurement sum2;	/* sum of squares of values */
	measurement sumt;	/* sum of values * time */
	measurement n;		/* number of values in list */

	measurement avg;	/* cache of mean calculation */
	measurement grad;	/* cache of gradient of values */
};
static const struct probeavg_s probeavg_zero={NULL,0,
	MEASUREMENT_ZERO,
	MEASUREMENT_ZERO,MEASUREMENT_ZERO,MEASUREMENT_ZERO,MEASUREMENT_ZERO,
	MEASUREMENT_ZERO,MEASUREMENT_ZERO
};

struct probestate_s {
	int probes;
	struct probeavg_s *probe;
};

typedef struct {
	/* not a list yet: struct probeport_s *next,*prev; */
	int fd;
	char *line;
	int current;
	int max;
	struct probedata_s data;
	uint complete:1;
} probeport;

static int probefromid(const char *id) {
	int p;
	for(p=0;p<probelookups;p++) {
		if (strcmp(probelookup[p].id,id)==0)
			return p;
	}
	return -1;
}

static bool measurementop(measurement *dest,char op,const measurement *arg,double x) {
	bool okay=true;
	#define SRC  1
	#define DEST 2
	#define BOTH 3
	#define CHK(READING,NEED,STM,COND)							\
		do {													\
			bool updated=false;									\
			do{													\
				if ((NEED & 1) && !arg->have##READING) break;	\
				if ((NEED & 2) && !dest->have##READING) break;	\
				if (!(COND)) break;								\
				STM;											\
				updated=true;									\
			} while(0);											\
			dest->have##READING=updated;						\
			if (isnan(dest->READING)) okay=false;				\
		} while(0)

	#define DIVOP(READING,OP,NEED) CHK(READING,NEED,dest->READING/=arg->READING,arg->READING!=0.0)
	#define UNIOP(READING,OP,NEED) CHK(READING,NEED,dest->READING OP,true)
	#define REGOP(READING,OP,NEED) CHK(READING,NEED,dest->READING OP arg->READING,true)
	#define MULOP(READING,OP,NEED) CHK(READING,NEED,dest->READING OP x * arg->READING,true)
	#define ADDOP(READING,OP,NEED) CHK(READING,NEED,dest->READING OP x + arg->READING,true)
	#define SQUOP(READING,OP,NEED) CHK(READING,NEED,dest->READING OP arg->READING * arg->READING,true)
	#define TIMOP(READING,OP,NEED) CHK(READING,NEED,dest->READING OP arg->READING * arg->time,true)
	#define DOOPS(DOOP,OP,NEED) do{				\
		DOOP(time,OP,NEED); DOOP(temp,OP,NEED);		\
		DOOP(rh,OP,NEED);   DOOP(pressure,OP,NEED);	\
		DOOP(state,OP,NEED);				\
		}while(0)
	switch(op) {
	case '=': *dest=*arg;           break;
	case '+': DOOPS(REGOP,+=,BOTH); break;
	case '-': DOOPS(REGOP,-=,BOTH); break;
	case '*': DOOPS(REGOP,*=,BOTH); break;
	case '/': DOOPS(DIVOP,/=,BOTH); break;
	case 'a': DOOPS(ADDOP,= ,SRC);  break;
	case 'm': DOOPS(MULOP,= ,SRC);  break;
	case 's': DOOPS(SQUOP,+=,BOTH); break;
	case 'S': DOOPS(SQUOP,-=,BOTH); break;
	case 't': DOOPS(TIMOP,+=,BOTH); break;
	case 'T': DOOPS(TIMOP,-=,BOTH); break;
	case 'i': DOOPS(UNIOP,++,DEST); break; /* increment if present */
	case 'd': DOOPS(UNIOP,--,DEST); break; /* increment if present */
	default: break;
	}
	return okay;
}

#define MS_UNITS	1	/* print units */
#define MS_NOOFFSET	2	/* ignore offset */
#define MS_G		4	/* generic float format */

static char *double_string(char *buf,size_t buflen,const char *fmt,double val,const char *unit) {
	if (!fmt) snprintf(buf,buflen,"?%s",unit);
	else      snprintf(buf,buflen,fmt,val,unit);
	return buf;
}

#define VALUE_STRING_FN(TYPE,UNIT,ADD,GFMT,FMT) \
	static char *TYPE##_string(const struct probeavg_s *ctx,const measurement *m,uint flags) { \
		static char buf[24]; \
		double offset=(flags & MS_NOOFFSET)?0.0:ctx->offset.TYPE + ADD; \
		const char *fmt=(!m || !m->have##TYPE)?NULL:(flags & MS_G)?GFMT"%s":FMT"%s"; \
		return double_string(buf,sizeof(buf),fmt,m->TYPE + offset,(flags & MS_UNITS)?UNIT:""); \
	}
VALUE_STRING_FN(time,    "s",    epoch_s,"%.4g","%.1f")
VALUE_STRING_FN(temp,    "degC", 0,      "%.3g","%.2f")
VALUE_STRING_FN(rh,      "%rh",  0,      "%.3g","%.1f")
VALUE_STRING_FN(pressure,"hPa",  0,      "%.4g","%.1f")
VALUE_STRING_FN(state,   "state",0,      "%.2g","%.1f")

static char *measurement_string(const struct probeavg_s *ctx,const measurement *m,unsigned int flags) {
	static char line[128];
	snprintf(line,sizeof(line),"%s %s %s %s %s",
		time_string(ctx,m,flags),
		temp_string(ctx,m,flags),
		rh_string(ctx,m,flags),
		pressure_string(ctx,m,flags),
		state_string(ctx,m,flags));
 	return line;
}

static void probe_delete(probeport *pp) {
	if (pp->fd!=-1) close(pp->fd);
	free(pp);
}

static probeport *probe_new(const char *probename,int maxlinelen) {
	probeport *pp;
	int fd;

	fd=open(probename,O_RDONLY);
	if (fd==-1) return NULL;

	pp=malloc(sizeof(*pp)+maxlinelen);
	if (!pp) return NULL;
	pp->fd=fd;
	pp->line=(char*)(pp+1);
	pp->current=0;
	pp->max=maxlinelen;
	pp->complete=0; /* may start with incomplete line */
	return pp;
}

static int decodedata(probeport *p) {
	/* decode a complete line from the probe port */
	/* returns number of new data items (1 or 0) or error (-1) */
	char *l=p->line,*e;
	double x;

	//DBG(printf("decodedata:in \"%s\"\n",l);)

	if (l[0]=='\0') {
		/* blank line */
		return 0;
	}
	if (strncmp("#",l,1)==0) {
		/* comment line */
		return 0;
	}
	if (strncmp("probe ",l,6)==0) {
		/* line of the form "probe N DDDunit DDDunit..." */
		l+=6;
		p->data.probe=strtol(l,&e,0);
		if (e==l) return 0; /* malformed */
		l=e+1;

		p->data.m=measurement_null;
		while(*l) {
			/* fix next data string */
			e=strchr(l,' ');
			if (e!=NULL) {*e='\0'; e=e+1;}
			//DBG(printf("decodedata:str \"%s\"\n",l);)
			x=strtod(l,&l);
			#define SETM(T,V) do {p->data.m.T=V; p->data.m.have##T=1;} while(0)
			if (strcmp("hPa",  l)==0) SETM(pressure,x);
			if (strcmp("degC", l)==0) SETM(temp,x);
			if (strcmp("%rh",  l)==0) SETM(rh,x);
			if (strcmp("state",l)==0) SETM(state,x);

			if (!e) break;
			l=e;
		}
		p->data.m.time=timenow(); /* offset is removed later */
		p->data.m.havetime=1;
		DBG(printf("decodedata:out %s\n",measurement_string(&probeavg_zero,&p->data.m,MS_UNITS));)
		return 1;
	}
	TRACEp(printf("decodedata: bad line \"%s\" %d/%d\n",p->line,p->current,p->max);)
	return -1; /* malformed line */
}

static struct probedata_s *readserialdata(probeport *p,const char **error) {
	/* read at least 1 char from probe stream */
	char *end,*start;
	ssize_t len;
	struct probedata_s *r=NULL;
	const char *err=NULL;
	int max;

	do {
		start=&p->line[p->current];
		max=p->max - p->current-1;
		TRACEp(printf("readmoredata: read current=%d max=%d\n",p->current,max);)
		len=read(p->fd,start,max);
		if (len==-1) {err="bad serial read"; break;}
		if (len==0)  {err="no more serial data"; break;}
		p->current+=len;

		while((end=memchr(start,'\n',p->current))!=NULL) *end='\0';
		end=memchr(start,'\0',p->current);
		TRACEp(printf("readmoredata: got %zd (max %d) bytes on %d: \"%.*s\"\n",
			len,max,p->fd,(int)((end)?end-start:len),start);)
		if (!end && p->current+1 >= p->max) {
			/* force end of line */
			end=&p->line[p->current];
		}
		if (!end) break; /* incomplete line */

		TRACEp(printf("line: \"%s\"\n",p->line);)
		switch(decodedata(p)) {
		case 1:	r=&p->data; break;
		case 0: break;
		default: if (p->complete) err="bad data decode"; break;
		}
		p->complete=1;

		/* eat off decoded data */
		len=&p->line[p->current] - (end);
		if (len>1) memmove(p->line,end+1,len-1);
		p->current=(len>1)?len-1:0;
	} while(0);
	if (error) *error=err;
	return r;
}

static void storejson(const json_valuecontext *root,const json_value *v,void *context) {
	struct probedata_s *p=context;
	
	switch(v->type) {
	case json_type_string:
		if (json_matches_path(root,"id",NULL)) {
			char buf[32];
			if (json_string_to_utf8(buf,sizeof(buf),&v->string) >= sizeof(buf))
				return; /* too long */
			p->probe=probefromid(buf);
			return;
		}
		break;
	case json_type_number:
		if (json_matches_path(root,"probe",NULL)) {
			p->probe=v->number;
			return;
		}
		#define MATCH(KEY,STORE) \
			if (json_matches_path(root,KEY,NULL)) { \
				p->m.STORE=v->number; \
				p->m.have##STORE=1; \
				return; \
			}
		MATCH("s",time);
		MATCH("degc",temp);
		MATCH("rh",rh);
		MATCH("hpa",pressure);
		MATCH("state",state);
		break;
	default:
		break;
	}
	/* ignore no match */
	return;
}

static void ignorejsonerr(const json_valuecontext *c,const char *e,json_in s,json_in h,const char *m,void *context) {
	struct probedata_s *p=context;
	(void)c; (void)e; (void)s; (void)h; (void)m; (void)context;
	memset(p,0,sizeof(*p));
	return;
}

static struct probedata_s *readjsondata(int jsonfd,bool *more,const char **error) {
	static struct probedata_s r;
	static char buf[256];
	static ssize_t buflen;
	const char *err=NULL;
	const char *end;
	static json_callbacks cb={.got_value=storejson,.error=ignorejsonerr};
	
	do {
		/* each read should form one or more complete JSON data blocks */
		if (buflen==0) {
			buflen=read(jsonfd,buf,sizeof(buf)-1);
			if (buflen==0) {err="no more JSON data"; break;}
			if (buflen==-1) {err="bad JSON read"; break;}
			buf[buflen]='\0';
		}
		
		memset(&r,0,sizeof(r));
		cb.context=&r;
		end=json_parse(&cb,buf);
		if (end) {
			size_t removed=end-buf;
			memmove(buf,end,buflen-removed);
			buflen-=removed;
			if (!r.m.havetime) {
				r.m.time=timenow();
				r.m.havetime=1;
			}
			*more=(buflen==0)?false:true;
			return &r;
		}
		else {
			err="bad JSON data";
			buflen=0;
			break;
		}
	} while(0);
	if (error) *error=err;
	*more=false;
	return NULL;
}

static void combineavg(struct probeavg_s *p,const struct probedata_s *newdata) {
	struct probedata_s *d;
	double now;
	bool okay;

	now=timenow();

	/* on shutdown, remove all callocs */
	if (!newdata) {
		while(!dll_empty(p->list)) {
			d=dll_head(p->list);
			dll_remove(p->list,d);
			free(d);
		}
		return;
	}

	okay=true;
	#define ADD() \
		okay&=measurementop(&p->sum, '+',&d->m,0); \
		okay&=measurementop(&p->sum2,'s',&d->m,0); \
		okay&=measurementop(&p->sumt,'t',&d->m,0); \
		okay&=measurementop(&p->n,   'i',&d->m,0);
	#define SUB() \
		okay&=measurementop(&p->sum, '-',&d->m,0); \
		okay&=measurementop(&p->sum2,'S',&d->m,0); \
		okay&=measurementop(&p->sumt,'T',&d->m,0); \
		okay&=measurementop(&p->n,   'd',&d->m,0);

	/* periodically recalculate sum to mitigate rounding errors */
	if (!okay && p->sumreset>10) p->sumreset=10;
	if (--p->sumreset <= 0) {
		measurement diff;
		p->sumreset=6000;
		measurementop(&diff,     '=',&p->offset,0);
		measurementop(&diff,     '-',&p->avg,0);
		TRACEc(printf("re-sum: %s\n",measurement_string(p,&diff,0));)
		TRACEc(printf("      : %s\n",measurement_string(p,&p->avg,0));)
		measurementop(&p->offset,'=',&p->avg,0);
		measurementop(&p->sum,   '=',&measurement_zero,0);
		measurementop(&p->n,     '=',&measurement_zero,0);
		for_dll(p->list,d) {
			measurementop(&d->m,'+',&diff,0);
			ADD()
		}
		DBG(p->avg=p->sum; measurementop(&p->avg,'/',&p->n,0);)
		TRACEc(printf("      : %s\n",measurement_string(p,&p->avg,0));)
	}

	/* expire old (tailing) values */
	while(!dll_empty(p->list)) {
		d=dll_tail(p->list);
		TRACEc(printf("oldest time %f offset %f now %f\n",d->m.time,p->offset.time,now);)
		if (d->m.time + p->offset.time + AVGTIME > now) break;
		dll_remove(p->list,d);
		TRACEc(printf("remove old data from %f\n",d->m.time);)
		SUB()
		free(d);
	}

	/* add new value */
	d=calloc(1,sizeof(*d));
	if (!d) return;
	*d=*newdata;
	measurementop(&d->m,  '-',&p->offset,0);
	ADD()
	dll_addhead(p->list,d);

	/* calculate mean */
	p->avg=p->sum;
	measurementop(&p->avg,'/',&p->n,0);

	/* calculate gradient */
	measurement nty;
	double div=p->sum2.time - p->sum.time*p->sum.time/p->n.time;
	if (div==0.0)
		p->grad=measurement_null;
	else {
		p->grad=p->sumt;
		measurementop(&nty,'m',&p->avg,p->sum.time);
		measurementop(&p->grad,'-',&nty,0);
		//DBG(printf("top_time=%f-%f=%f\n",p->sumt.time,nty.time,p->grad.time);)
		//DBG(printf("bottom=%f-%f=%f\n",p->sum2.time,p->sum.time*p->sum.time/p->n.time,p->sum2.time - p->sum.time*p->sum.time/p->n.time);)
		measurementop(&p->grad,'m',&p->grad,1.0/div);
		DBG(printf("gradient %p: %s\n",p,measurement_string(p,&p->grad,MS_UNITS|MS_G|MS_NOOFFSET));)
	}
}

static void processdata(struct probestate_s *s,struct probedata_s *d) {
	if (!s || !d) return;
	if (d->probe >= MAXPROBES) return;
	if (d->probe >= s->probes) {
		if (s->probes==0 && s->probe!=NULL) {free(s->probe); s->probe=NULL;}
		s->probe=realloc(s->probe,sizeof(*s->probe)*(d->probe+1));
		if (!s->probe) {s->probes=0; return;}
		for(;s->probes <= d->probe;s->probes++)
			s->probe[s->probes]=probeavg_zero;
	}
	combineavg(&s->probe[d->probe],d);
	DBG(printf("processdata: %s\n",measurement_string(&s->probe[d->probe],&s->probe[d->probe].avg,MS_UNITS));)
}

/* -- web server stuff -- */

struct server_s {
	struct server_s *next,*prev;
	int socket;
	enum server_state {
		server_reseting,
		server_reading,
		server_writing,
		server_processing,
		server_closing} state;
	unsigned int count; /* count of server chars written */
	unsigned int buflen; /* amount of buf[] used */
	int idle; /* detect stalled client */
	bool noreset;
	#define URISPEC "%199s" /* stringify sizeof uri */
	char uri[200];
	char buf[2048+MAXPROBES*128];
};

static void server_delete(struct server_s *sl) {
	close(sl->socket);
	free(sl);
}

static void server_reset(struct server_s *sl) {
	if (sl->noreset || sl->state==server_closing) {
		sl->state=server_closing;
		return;
	}
	/* reset server for more requests */
	sl->state=server_reading;
	sl->count=0;
	sl->idle=0;
	sl->buflen=0;
	memset(sl->buf,0,sizeof(sl->buf));
}

static struct server_s *server_new(int socket) {
	/* create new server on socket */
	struct server_s *sl;
	sl=calloc(1,sizeof(*sl));
	if (!sl) return NULL;
	sl->state=server_reseting;
	sl->socket=socket;
	sl->noreset=false;
	server_reset(sl);
	return sl;
}

enum server_process_result {sl_ok,sl_close,sl_again};

static enum server_process_result server_process(struct server_s *sl,bool readable,bool writable,struct probestate_s *s) {
	char *crlf;
	int tmp;
	char tmp128[128];
	ssize_t r,w,send;
	enum server_state enter_state=sl->state;

	switch(sl->state) {
	case server_reading:
		if (!readable) {sl->idle+=AVGTIME; break;}
		r=read(sl->socket,sl->buf+sl->buflen,sizeof(sl->buf)-sl->buflen-1);
		if (r==-1 || r<0) {sl->state=server_closing; break;}
		sl->buflen+=r;
		crlf=strstr(sl->buf,CRLF CRLF); /* buf is always terminated */
		if (sl->buflen>=sizeof(sl->buf)-1 || r==0 || crlf) {
			/* note: this may fail for multiple-request operations */
			TRACEs(printf("GOT: \"%*s\"\n",sl->buflen,sl->buf);)
			sl->state=server_processing;
			break;
		}
		break;
	case server_writing:
		if (!writable) {sl->idle+=AVGTIME; break;}
		send=sl->buflen - sl->count;
		TRACEs(printf("SEND: \"%*s\" (%zd chars)\n",(int)send,sl->buf+sl->count,send);)
		w=write(sl->socket,sl->buf+sl->count,send);
		if (w==-1) sl->state=server_closing;
		else if (w==0) sl->state=server_closing;
		else if (w==send) sl->state=server_reseting;
		else sl->count+=w;
		break;
	case server_processing:
		#define OUTADD(...) do { \
			if (sl->buflen < sizeof(sl->buf)) \
				sl->buflen+=snprintf(sl->buf+sl->buflen,sizeof(sl->buf)-sl->buflen,__VA_ARGS__); \
			} while(0)
		#define OUTSTART(STATUS) do { \
				sl->buflen=0; \
				OUTADD("%s",STATUS); \
			} while(0)
		#define OUT_OK() do { \
				char *lenptr; \
				lenptr=strstr(sl->buf,LEN8MARKER); \
				if (lenptr) sprintf(lenptr,"%8zu",sl->buflen-sizeof(header_ok)+1); \
				sl->state=server_writing; \
			} while(0)
		#define OUT_NOK() do {sl->state=server_writing; sl->noreset=true; } while(0)
		sl->idle=0;
		/* buf must start with request-line */
		if (sl->buflen<4 || sscanf(sl->buf,"GET " URISPEC,sl->uri)!=1) {
			TRACEs(printf("FAIL: \"%s\"\n",sl->buf);)
			sl->state=server_closing;
			break;
		}
		TRACEs(printf("PROCESSING: %s\n",sl->uri);)
		if (strcmp(sl->uri,"/")==0) {
			OUTSTART(header_ok);
			OUTADD("# temperature server" CRLF);
			OUTADD("# /localtime        -> YYYY-MM-DD HH:MM:SS time_t tz_offset" CRLF);
			OUTADD("# /measurementlines -> [units]: [probe0] [probe1]... ..." CRLF);
			OUTADD("# /gradientlines    -> [units]: [probe0] [probe1]... ..." CRLF);
			OUTADD("# /probes           -> [N] [probeline] ..." CRLF);
			OUTADD("# /probe/[N]        -> [probeline]" CRLF);
			OUTADD("# /locations        -> [N] [location] ..." CRLF);
			OUTADD("# /location/[N]     -> [location]" CRLF);
			OUTADD("# /idprobes         -> [N] [id] ..." CRLF);
			OUTADD("# /idprobe/[id]     -> [N]" CRLF);
			OUTADD("# probeline => time degC %%rh state" CRLF);
			OUT_OK();
			break;
		}
		if (strcmp(sl->uri,"/localtime")==0) {
			struct tm tm;
			time_t t;
			time(&t);
			tzset();
			if (!localtime_r(&t,&tm)) {
				OUTSTART(header_error);
				OUT_NOK();
				break;
			}
			tm.tm_year+=1900;
			OUTSTART(header_ok);
			OUTADD("%04d-%02d-%02d %02d:%02d:%02d %ld %+ld" CRLF,
				tm.tm_year,tm.tm_mon+1,tm.tm_mday,
				tm.tm_hour,tm.tm_min  ,tm.tm_sec,
				(long int)t,-tm.tm_gmtoff);
			OUT_OK();
			break;
		}
		if (strcmp(sl->uri,"/probes")==0) {
			int i;
			OUTSTART(header_ok);
			for(i=0;i < s->probes;i++) {
				OUTADD("%d %s" CRLF,i,measurement_string(&s->probe[i],&s->probe[i].avg,0));
			}
			OUT_OK();
			break;
		}
		if (strcmp(sl->uri,"/measurementlines")==0) {
			int i;
			OUTSTART(header_ok);
			#define ULINE(STAT,UN,CONV,FLAGS) \
				OUTADD("%s:",UN); \
				for(i=0;i < s->probes;i++) \
					OUTADD(" %s",CONV(&s->probe[i],&s->probe[i].STAT,FLAGS)); \
				OUTADD("\n");
			ULINE(avg,"seconds",time_string,0);
			ULINE(avg,"degC",temp_string,0);
			ULINE(avg,"%rh",rh_string,0);
			ULINE(avg,"hPa",pressure_string,0);
			ULINE(avg,"state",state_string,0);
			OUT_OK();
			break;
		}
		if (strcmp(sl->uri,"/gradientlines")==0) {
			int i;
			OUTSTART(header_ok);
			ULINE(grad,"seconds/s",time_string,MS_NOOFFSET|MS_G);
			ULINE(grad,"degC/s",temp_string,MS_NOOFFSET|MS_G);
			ULINE(grad,"%rh/s",rh_string,MS_NOOFFSET|MS_G);
			ULINE(grad,"hPa/s",pressure_string,MS_NOOFFSET|MS_G);
			ULINE(grad,"state/s",state_string,MS_NOOFFSET|MS_G);
			OUT_OK();
			break;
		}
		if (strcmp(sl->uri,"/idprobes")==0) {
			int i;
			OUTSTART(header_ok);
			for(i=0;i < probelookups; i++) {
				OUTADD("%d %s" CRLF,i,probelookup[i].id);
			}
			OUT_OK();
			break;
		}
		if (strcmp(sl->uri,"/locations")==0) {
			int i;
			OUTSTART(header_ok);
			for(i=0;i < probelookups; i++) {
				OUTADD("%d %s" CRLF,i,probelookup[i].location);
			}
			OUT_OK();
			break;
		}
		if (sscanf(sl->uri,"/location/%d",&tmp)==1) {
			if (tmp<0 || tmp >= probelookups) {
				OUTSTART(header_bad);
				OUT_NOK();
			}
			else {
				OUTSTART(header_ok);
				OUTADD("%s" CRLF,probelookup[tmp].location);
				OUT_OK();
			}
			break;
		}
		if (sscanf(sl->uri,"/probe/%d",&tmp)==1) {
			if (tmp<0 || tmp >= s->probes) {
				OUTSTART(header_bad);
				OUT_NOK();
			}
			else {
				struct probedata_s *d;
				OUTSTART(header_ok);
				d=dll_head(s->probe[tmp].list);
				OUTADD("%s" CRLF,measurement_string(&s->probe[tmp],&d->m,0));
				OUT_OK();
			}
			break;
		}
		if (sscanf(sl->uri,"/idprobe/%127s",tmp128)==1) {
			int i;
			i=probefromid(tmp128);
			if (i<0) {
				OUTSTART(header_bad);
				OUT_NOK();
			}
			else {
				OUTSTART(header_ok);
				OUTADD("%d" CRLF,i);
				OUT_OK();
			}
			break;
		}
	
		/* otherwise... */
		OUTSTART(header_notfound);
		OUT_NOK();
		break;
	case server_reseting:
		TRACEs(printf("RESET\n");)
		server_reset(sl);
		break;
	case server_closing:
		/* will eventually get all of them */
		TRACEs(printf("CLOSING\n");)
		break;
	}
	if (sl->idle >= 300) sl->state=server_closing;

	if (sl->state == server_closing) return sl_close;
	if (sl->state != enter_state) return sl_again;
	return sl_ok;
}

int tcpport(struct in_addr interface,int port) {
	struct sockaddr_in sa;
	int ss;
	int one=1;

	do {
		ss=socket(PF_INET,SOCK_STREAM,IPPROTO_TCP);
		if (ss==-1) break;
		if (setsockopt(ss,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one))==-1)
			break;

		sa.sin_family=AF_INET;
		sa.sin_port=htons(port);
		sa.sin_addr=interface;
		if (bind(ss,(void*)&sa,sizeof(sa))==-1) break;
		listen(ss,32);
		return ss;
	} while(0);
	/* error */
	return -1;
}

/* -- do stuff -- */

static const char *mainloop(int listenfd,probeport *pp,FILE *json) {
	fd_set readfds,writefds;
	int maxfd;
	struct timeval timeout;
	struct server_s *serverlist=NULL,*sl,*closesl=NULL;
	int tmp;
	const char *e=NULL;
	struct probestate_s probestate={0,NULL};
	bool again;
	int jsonfd;

	if (pp->fd==-1) return "bad data tty";
	if (listenfd==-1) return "bad listening socket";
	/* jsonfd optional */
	jsonfd=(json)?fileno(json):-1;

	for(;;) {
		/* set up select() */
		timeout.tv_sec=AVGTIME;
		timeout.tv_usec=0;
		FD_ZERO(&readfds); FD_ZERO(&writefds); maxfd=0;
		if (pp->fd>=0)   {FD_SET(pp->fd,&readfds); maxfd=MAX(maxfd,pp->fd);}
		if (listenfd>=0) {FD_SET(listenfd,&readfds); maxfd=MAX(maxfd,listenfd);}
		if (jsonfd>=0)   {FD_SET(jsonfd,&readfds); maxfd=MAX(maxfd,jsonfd);}
		for_dll(serverlist,sl) {
			switch(sl->state) {
			case server_reading:
				FD_SET(sl->socket,&readfds); break;
			case server_writing:
				FD_SET(sl->socket,&writefds); break;
			default: /* needs attention */
				timeout.tv_sec=0; break;
			}
			maxfd=MAX(maxfd,sl->socket);
		}
		tmp=select(maxfd+1,&readfds,&writefds,NULL,&timeout);
		TRACE(printf("select returned %d\n",tmp);)
		if (tmp==-1) {e="select failed"; break;}

		/* process select() results */
		if (listenfd>=0 && FD_ISSET(listenfd,&readfds)) {
			int ss;
			ss=accept(listenfd,NULL,NULL);
			sl=server_new(ss);
			if (sl) dll_addhead(serverlist,sl);
			continue; /* updated serverlist invalidates select results */
		}
		/* process JSON injection */
		if (jsonfd>=0 && FD_ISSET(jsonfd,&readfds)) {
			bool more;
			TRACEp(printf("select on %d (JSON injection) good for reading\n",jsonfd);)
			do {
				processdata(&probestate,readjsondata(jsonfd,&more,&e));
			} while(more);
			if (e) break;
		}
		/* process select()'s probelist */
		if (FD_ISSET(pp->fd,&readfds)) {
			TRACEp(printf("select on %d good for reading\n",pp->fd);)
			processdata(&probestate,readserialdata(pp,&e));
			if (e) break;
		}
		/* process select()'s serverlist */
		do {
			again=false;
			for_dll(serverlist,sl) {
				bool readable,writable;
				enum server_process_result res;

				readable=!!FD_ISSET(sl->socket,&readfds);  FD_CLR(sl->socket,&readfds);
				writable=!!FD_ISSET(sl->socket,&writefds); FD_CLR(sl->socket,&writefds);

				res=server_process(sl,readable,writable,&probestate);
				switch(res) {
				case sl_close: closesl=sl; break;
				case sl_again: again=true; break;
				case sl_ok:
				default: break;
				}
				if (sl_close) break;
			}
			if (closesl) {
				/* cannot remove server within for_dll() */
				dll_remove(serverlist,closesl);
				server_delete(closesl);
				closesl=NULL;
				again=true;
			}
		} while(again);
	}

	/* clean up and quit */
	while(!dll_empty(serverlist)) {
		closesl=dll_head(serverlist);
		dll_remove(serverlist,closesl);
		server_delete(closesl);
	}
	for(;probestate.probes>=1;probestate.probes--)
		combineavg(&probestate.probe[probestate.probes-1],NULL);
	free(probestate.probe);
	return e;
}

int main(int argc,char *argv[]) {
	const char *e=NULL,*e2=NULL;
	int ss=-1;
	FILE *jj=NULL;
	probeport *pp=NULL;
	const char *probename=SENSORTTY;
	struct in_addr interface={htonl(INADDR_ANY)};
	int port=PORT;

	progname=*argv++; argc--;
	if (argc>0) {probename=*argv++; argc--;}
	
	fflush(stdin); /* don't pass on stdin to sub-processes */

	/* debugging */
	DBG(probename="testdatatty";)
	DBG(port=PORT+1;)
	DBG(TRACE(printf("starting server on port %d\n",port);))

	do {
		pp=probe_new(probename,100);
		if (!pp) {e="cannot open"; e2=probename; break;}
		
		jj=popen(JSONSOURCE,"r"); /* JSON injection stream */
//		jj=popen("bash -c \"echo hello; sleep 1; echo there\"","r"); /* JSON injection stream */
		if (!jj) {fprintf(stderr,"Warning: cannot open injection port\n");}

		ss=tcpport(interface,port);
		if (ss<0) {e="cannot open TCP port"; break;}

		e=mainloop(ss,pp,jj);
		if (e) e2=strerror(errno);
	} while(0);

	if (pp) probe_delete(pp);
	if (ss!=-1) close(ss);
	if (jj) pclose(jj);

	if (e) {
		if (e2) fprintf(stderr,"%s: %s (%s)\n",progname,e,e2);
		else    fprintf(stderr,"%s: %s\n",progname,e);
		return 1;
	}
	return 0;
}
