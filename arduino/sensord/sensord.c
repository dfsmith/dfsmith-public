/* > sensord.c */
/* Daniel F. Smith, 2014 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>

#define SOCKLIBINCLUDES
#include "socklib.h"

#define TRACE(x)
#define DBG(x)

#define SENSORTTY "/dev/ttyarduino"
//#define SENSORTTY "testdata"
#define AVGTIME 60
#define MAXPROBES 10
#define PORT 8888

#define MAX(a,b) ((a)>(b)?(a):(b))
#define lengthof(x) (sizeof(x)/sizeof(*(x)))
typedef unsigned int bool;

/* HTTP headers */
#define HEADER_OK_SET_CONTENTLENGTH(H,LEN) sprintf((H)+33,"%8zu",LEN)
const char header_ok[]="HTTP/1.1 200 OK\r\nContent-Length: 00000000\r\n"
                       "Access-Control-Allow-Origin: *\r\n"
                       "Content-Type: text/plain\r\n\r\n";
const char header_bad[]="HTTP/1.1 400 Bad request\r\n\r\n";
const char header_notfound[]="HTTP/1.1 404 Not found\r\n\r\n";
const char header_error[]="HTTP/1.1 500 Internal error\r\n\r\n";

/* -- doubly linked circular lists -- */

#define dll_add(MAIN,NEW) \
	do { \
		if (!(MAIN)) { \
			(NEW)->prev=(NEW); \
			(NEW)->next=(NEW); \
		} \
		else { \
			(MAIN)->prev->next=(NEW); \
			(NEW)->prev=(MAIN)->prev; \
			(NEW)->next=(MAIN); \
			(MAIN)->prev=(NEW); \
		} \
		(MAIN)=(NEW); \
	} while(0)
#define dll_remove(MAIN,OLD) \
	do { \
		(OLD)->next->prev=(OLD)->prev; \
		(OLD)->prev->next=(OLD)->next; \
		if ((OLD)==(MAIN)) (MAIN)=(OLD)->next==(MAIN)?NULL:(OLD)->next; \
	} while(0)
#define dll_empty(MAIN) ((MAIN)==NULL)
#define dll_head(MAIN) (MAIN)
#define dll_tail(MAIN) ((MAIN)?(MAIN)->prev:NULL)
#define for_dll(MAIN,P)             for((P)=(MAIN);      (P);(P)=((P)->next==(MAIN))      ?NULL:(P)->next)
#define for_dll_r(MAIN,P) if (MAIN) for((P)=(MAIN)->prev;(P);(P)=((P)->prev==(MAIN)->prev)?NULL:(P)->prev)

/* -- main program -- */

typedef struct {
	double temp;
	double rh;	
	double pressure;
	double state;
	
	uint havetemp:1;
	uint haverh:1;
	uint havepressure:1;
	uint havestate:1;
} measurement;
#define MEASUREMENT_ZERO {0.0,0.0,0.0,0.0, 1,1,1,1}
const measurement measurement_null={0.0,0.0,0.0,0.0, 0,0,0,0};
const measurement measurement_zero=MEASUREMENT_ZERO;

struct tempdata_s {
	struct tempdata_s *next,*prev;
	struct timeval when;
	int probe;
	measurement m;
};

struct tempavg_s {
	struct tempdata_s *list;
	int sumreset; /* when 0, recalculate sum */
	measurement sum; /* sum of items in list */
	measurement n; /* number of items in list */
	measurement avg; /* cache of mean calculation */
};
const struct tempavg_s tempavg_zero={NULL,0,MEASUREMENT_ZERO,MEASUREMENT_ZERO};

struct probestate_s {
	struct tempavg_s *probe;
	int probes;
};

typedef struct {
	/* not a list yet: struct probeport_s *next,*prev; */
	int fd;
	char *line;
	int current;
	int max;
	struct tempdata_s data;
	uint complete:1;
} probeport;

static void measurementop(measurement *dest,char op,const measurement *arg) {
	#define DIVOP(READING,OP) do{if (arg->have##READING && arg->READING!=0.0) {dest->READING /= arg->READING;} else {dest->have##READING=0;}}while(0)
	#define UNIOP(READING,OP) do{if (arg->have##READING) {dest->READING OP;}}while(0)
	#define REGOP(READING,OP) do{if (arg->have##READING) {dest->READING OP arg->READING;}}while(0)
	#define DOOPS(DOOP,OP) do{DOOP(temp,OP); DOOP(rh,OP); DOOP(pressure,OP); DOOP(state,OP);}while(0)
	switch(op) {
	case '=':
		*dest=*arg;
		break;
	case '+': DOOPS(REGOP,+=); break;
	case '-': DOOPS(REGOP,-=); break;
	case '*': DOOPS(REGOP,*=); break;
	case '/': DOOPS(DIVOP,/=); break;
	case 'i': DOOPS(UNIOP,++); break; /* increment if present */
	case 'd': DOOPS(UNIOP,--); break; /* increment if present */
	default: break;
	}
}

static char *temp_string(const measurement *m,int u) {
	static char l[20];
	const char *unit=u?"degC":"";
	if (!m || !m->havetemp) snprintf(l,sizeof(l),"?%s",unit);
	else                    snprintf(l,sizeof(l),"%.3f%s",m->temp,unit);
	return l;
}

static char *rh_string(const measurement *m,int u) {
	static char l[20];
	const char *unit=u?"%rh":"";
	if (!m || !m->haverh) snprintf(l,sizeof(l),"?%s",unit);
	else                  snprintf(l,sizeof(l),"%.2f%s",m->rh,unit);
	return l;
}

static char *pressure_string(const measurement *m,int u) {
	static char l[20];
	const char *unit=u?"hPa":"";
	if (!m || !m->havepressure) snprintf(l,sizeof(l),"?%s",unit);
	else                        snprintf(l,sizeof(l),"%.2f%s",m->pressure,unit);
	return l;
}

static char *state_string(const measurement *m,int u) {
	static char l[20];
	const char *unit=u?"state":"";
	if (!m || !m->havestate) snprintf(l,sizeof(l),"?%s",unit);
	else                     snprintf(l,sizeof(l),"%.1f%s",m->state,unit);
	return l;
}

static char *measurement_string(const measurement *m,int u) {
	static char line[100];
	snprintf(line,sizeof(line),"%s %s %s %s",
		temp_string(m,u),
		rh_string(m,u),
		pressure_string(m,u),
		state_string(m,u));
	return line;
}

static char *measurement_tempother(const measurement *m,int u) {
	static char line[100];
	if (m->haverh)
		snprintf(line,sizeof(line),"%s %s",temp_string(m,u),rh_string(m,u));
	else
		snprintf(line,sizeof(line),"%s %s",temp_string(m,u),pressure_string(m,u));
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
		gettimeofday(&p->data.when,NULL);
		DBG(printf("decodedata:out %s\n",measurement_string(&p->data.m,1));)
		return 1;
	}
	TRACE(printf("decodedata: bad line \"%s\" %d/%d\n",p->line,p->current,p->max);)
	return -1; /* malformed line */
}

static struct tempdata_s *readmoredata(probeport *p,const char **error) {
	/* read at least 1 char from probe stream */
	char *end,*start;
	ssize_t len;
	struct tempdata_s *r=NULL;
	const char *err=NULL;
	
	do {
		start=&p->line[p->current];
		len=read(p->fd,start,p->max - p->current-1);
		if (len==-1) {err="bad read"; break;}
		TRACE(printf("readmoredata: got %zd of %zd bytes on %d: \"%*s\"\n",
			len,p->max-p->current-1,p->fd,(int)len,start);)
		if (len==0)  {err="no more data"; break;}
		p->current+=len;
		p->line[p->current]='\0';

		end=strchr(p->line,'\n');
		if (!end && p->current+1 >= p->max) {
			/* implicit '\n' at forced end of line */
			end=&p->line[p->current];
		}
		TRACE(printf("line: \"%*s\"\n",(int)p->current,p->line);)
		if (!end) break; /* incomplete line */

		*end='\0';
		switch(decodedata(p)) {
		case 1:	r=&p->data; break;
		case 0: break;
		default: if (p->complete) err="bad data decode"; break;
		}
		p->complete=1;

		/* eat off decoded data */
		len=&p->line[p->current] - (end);
		if (len>0) {
			memmove(p->line,end+1,len-1);
			p->current=len-1;
		}
	} while(0);
	if (error) *error=err;
	return r;
}

static double tvdiff(const struct timeval *end,const struct timeval *start) {
	double t;
	t=end->tv_sec;
	t-=start->tv_sec;
	t+=end->tv_usec*1e-6;
	t-=start->tv_usec*1e-6;
	return t;
}

static void combineavg(struct tempavg_s *p,struct tempdata_s *newdata) {
	struct tempdata_s *d;
	struct timeval tv;
	gettimeofday(&tv,NULL);

	/* on shutdown, remove all callocs */
	if (!newdata) {
		while(!dll_empty(p->list)) {
			d=dll_head(p->list);
			dll_remove(p->list,d);
			free(d);
		}
		return;
	}

	/* recalculate sum to mitigate rounding errors */
	if (--p->sumreset <= 0) {
		p->sumreset=6000;
		measurementop(&p->sum,'=',&measurement_zero);
		measurementop(&p->n,'=',&measurement_zero);
		for_dll(p->list,d) {
			measurementop(&p->sum,'+',&d->m);
			measurementop(&p->n,'i',&d->m);
		}
	}

	/* expire old values */
	while(!dll_empty(p->list)) {
		d=dll_tail(p->list);
		if (tvdiff(&tv,&d->when) < AVGTIME) break;
		dll_remove(p->list,d);
		measurementop(&p->sum,'-',&d->m);
		measurementop(&p->n,'d',&d->m);
		free(d);
	}

	/* add new value */
	d=calloc(1,sizeof(*d));
	if (!d) return;
	*d=*newdata;
	measurementop(&p->sum,'+',&d->m);
	measurementop(&p->n,'i',&d->m);
	dll_add(p->list,d);
	
	/* calculate mean */
	p->avg=p->sum;
	measurementop(&p->avg,'/',&p->n);
}

static void processdata(struct probestate_s *s,struct tempdata_s *d) {
	if (!s || !d) return;
	if (d->probe >= MAXPROBES) return;
	if (!s->probe) s->probes=0;
	if (d->probe >= s->probes) {
		s->probe=realloc(s->probe,sizeof(*s->probe)*(d->probe+1));
		if (!s->probe) {s->probes=0; return;}
		for(;s->probes <= d->probe;s->probes++)
			s->probe[s->probes]=tempavg_zero;
	}
	combineavg(&s->probe[d->probe],d);
	DBG(printf("processdata: %s\n",measurement_string(&s->probe[d->probe].avg,1));)
}

/* -- web server stuff -- */

struct server_s {
	struct server_s *next,*prev;
	int socket;
	enum {
		server_reseting,
		server_reading,
		server_writing,
		server_writinglast,
		server_processing,
		server_closing} state;
	int count,buflen;
	int idle;
	#define URISPEC "%199s" /* stringify sizeof uri */
	char uri[200];
	char buf[3800];
};

static void server_delete(struct server_s *sl) {
	close(sl->socket);
	free(sl);
}

static void server_reset(struct server_s *sl) {
	switch(sl->state) {
	case server_reseting:
	case server_reading:
	case server_writing:
	case server_processing:
		sl->state=server_reading;
		break;
	default:
		sl->state=server_closing;
		break;
	}
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
	server_reset(sl);
	return sl;
}

static struct server_s *server_process(struct server_s *sl,bool readable,bool writable,struct probestate_s *s) {
	char *crlf;
	int tmp;
	ssize_t r,w,send;
	
	switch(sl->state) {
	case server_reading:
		if (!readable) {sl->idle+=AVGTIME; break;}
		r=read(sl->socket,sl->buf+sl->buflen,sizeof(sl->buf)-sl->buflen-1);
		if (r==-1) {sl->state=server_closing; break;}
		sl->buflen+=r;
		crlf=strstr(sl->buf,"\r\n\r\n"); /* buf is always terminated */
		if (sl->buflen>=sizeof(sl->buf)-1 || r==0 || crlf) {
			/* note: this may fail for multiple-request operations */
			TRACE(printf("GOT: \"%*s\"\n",sl->buflen,sl->buf);)
			sl->state=server_processing;
			break;
		}
		break;
	case server_writing:
	case server_writinglast:
		if (!writable) {sl->idle+=AVGTIME; break;}
		send=sl->buflen - sl->count;
		TRACE(printf("SEND: \"%*s\" (%zd chars)\n",(int)send,sl->buf+sl->count,send);)
		w=write(sl->socket,sl->buf+sl->count,send);
		if (w==-1) sl->state=server_closing;
		else if (w==0) sl->state=server_closing;
		else if (w==send && sl->state==server_writinglast) sl->state=server_closing;
		else if (w==send) sl->state=server_reseting;
		else sl->count+=w;
		break;
	case server_processing:
		#define OUTADD(...) do { \
			if (sl->buflen < sizeof(sl->buf)) \
				sl->buflen+=snprintf(sl->buf+sl->buflen,sizeof(sl->buf)-sl->buflen,__VA_ARGS__); \
			} while(0)
		#define OUT_OK() do { \
				HEADER_OK_SET_CONTENTLENGTH(sl->buf,sl->buflen-sizeof(header_ok)+1); \
				sl->state=server_writing; \
			} while(0)
		#define OUT_NOK() (sl->state=server_writinglast)
		#define OUT(...) do { \
				sl->buflen=0; \
				OUTADD(__VA_ARGS__); \
			} while(0)
		sl->idle=0;
		/* buf must start with requst-line */
		if (sl->buflen<4 || sscanf(sl->buf,"GET " URISPEC,sl->uri)!=1) {
			TRACE(printf("FAIL: \"%s\"\n",sl->buf);)
			sl->state=server_closing;
			break;
		}
		TRACE(printf("PROCESSING: %s\n",sl->uri);)
		if (strcmp(sl->uri,"/")==0) {
			OUT("%s# temperature server\r\n# probeline probe degC %%rh state\r\n",header_ok);
			OUT_OK();
			break;
		}
		if (strcmp(sl->uri,"/localtime")==0) {
			struct tm tm;
			time_t t;
			time(&t);
			if (!localtime_r(&t,&tm)) {
				OUT("%s",header_error);
				OUT_NOK();
				break;
			}
			tm.tm_year+=1900;
			OUT("%s%04d-%02d-%02d %02d:%02d:%02d\r\n",header_ok,
				tm.tm_year,tm.tm_mon,tm.tm_mday,
				tm.tm_hour,tm.tm_min,tm.tm_sec);
			OUT_OK();
			break;
		}
		if (strcmp(sl->uri,"/probes")==0) {
			int i;
			OUT("%s",header_ok);
			for(i=0;i < s->probes;i++) {
				OUTADD("%d %s\r\n",i,measurement_string(&s->probe[i].avg,0));
			}
			OUT_OK();
			break;
		}
		if (strcmp(sl->uri,"/probeline")==0) {
			/* legacy */
			int i;
			long long int t;
			struct timeval tv;
			gettimeofday(&tv,NULL);
			t=tv.tv_sec;
			OUT("%s%lld",header_ok,t);
			for(i=0;i < s->probes;i++)
				OUTADD(" %s",measurement_tempother(&s->probe[i].avg,0));
			OUTADD("\r\n");
			OUT_OK();
			break;
		}
		if (strcmp(sl->uri,"/measurementlines")==0) {
			int i;
			double t;
			struct timeval tv;

			gettimeofday(&tv,NULL);
			t=tv.tv_sec;
			t+=tv.tv_usec * 1e-6;
			OUT("%sseconds: %.1f",header_ok,t);

			#define ULINE(UN,CONV) \
				OUTADD("\n%s:",UN); \
				for(i=0;i < s->probes;i++) \
					OUTADD(" %s",CONV(&s->probe[i].avg,0));
			ULINE("degC",temp_string);
			ULINE("%rh",rh_string);
			ULINE("hPa",pressure_string);
			ULINE("state",state_string);
			OUTADD("\n");
			OUT_OK();
			break;
		}
		if (sscanf(sl->uri,"/probe/%d",&tmp)==1) {
			if (tmp<0 || tmp >= s->probes) {
				OUT("%s",header_bad);
				OUT_NOK();
			}
			else {
				struct tempdata_s *d;
				OUT("%s",header_ok);
				d=dll_head(s->probe[tmp].list);
				OUTADD("%s\r\n",measurement_string(&d->m,0));
				OUT_OK();
			}
			break;
		}
		/* otherwise... */
		OUT("%s",header_notfound);
		OUT_NOK();
		break;
	case server_reseting:
		TRACE(printf("RESET\n");)
		server_reset(sl);
		break;
	case server_closing:
		/* will eventually get all of them */
		TRACE(printf("CLOSING\n");)
		sl->idle=300;
		break;
	}
	if (sl->idle >= 300) return sl;
	return NULL;
}

/* -- do stuff -- */

static const char *mainloop(int listenfd,probeport *pp) {
	fd_set readfds,writefds;
	int maxfd;
	struct timeval timeout;
	struct server_s *serverlist=NULL,*sl,*closesl=NULL;
	int tmp;
	const char *e=NULL;
	struct probestate_s probestate={NULL,0};
	
	if (pp->fd==-1) return "bad data tty";
	if (listenfd==-1) return "bad listening socket";
	
	for(;;) {
		/* set up select() */
		timeout.tv_sec=AVGTIME;
		timeout.tv_usec=0;
		FD_ZERO(&readfds); FD_ZERO(&writefds); maxfd=0;
		FD_SET(pp->fd,&readfds); maxfd=MAX(maxfd,pp->fd);
		FD_SET(listenfd,&readfds); maxfd=MAX(maxfd,listenfd);
		for_dll(serverlist,sl) {
			switch(sl->state) {
			case server_reading:
				FD_SET(sl->socket,&readfds); break;
			case server_writing:
			case server_writinglast:
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
		if (FD_ISSET(listenfd,&readfds)) {
			int ss;
			ss=accept(listenfd,NULL,NULL);
			sl=server_new(ss);
			if (sl) dll_add(serverlist,sl);
			continue; /* updated serverlist invalidates select results */
		}
		/* process select()'s probelist */
		if (FD_ISSET(pp->fd,&readfds)) {
			TRACE(printf("select on %d good for reading\n",pp->fd);)
			processdata(&probestate,readmoredata(pp,&e));
			if (e) break;
		}
		/* process select()'s serverlist */
		for_dll(serverlist,sl) {
			bool readable=FD_ISSET(sl->socket,&readfds);
			bool writable=FD_ISSET(sl->socket,&writefds);
			closesl=server_process(sl,readable,writable,&probestate);
		}
		if (closesl) {
			dll_remove(serverlist,closesl);
			server_delete(closesl);
			closesl=NULL;
		}
	}
	while(!dll_empty(serverlist))
		dll_remove(serverlist,dll_head(serverlist));
	for(;probestate.probes>=1;probestate.probes--)
		combineavg(&probestate.probe[probestate.probes-1],NULL);
	free(probestate.probe);
	return e;
}

int main(int argc,char *argv[]) {
	const char *e=NULL,*e2=NULL;
	SOCKET s=SOCKET_ERROR;
	const char *progname;
	probeport *pp=NULL;
	const char *probename=SENSORTTY;
	int port=PORT;

	progname=*argv++; argc--;
	if (argc>0) {probename=*argv++; argc--;}

	/* debugging */
	DBG(probename="testdatatty";)
	DBG(port=PORT+1;)
	DBG(TRACE(printf("starting server on port %d\n",port);))

	do {
		pp=probe_new(probename,100);
		if (!pp) {e="cannot open"; e2=probename; break;}
	
		sock_setreuse(1);
		s=sock_getsocketto(NULL,-port,SOCK_STREAM,&e2);
		if (SOCKINV(s)) {e="cannot create socket"; break;}

		e=mainloop(sock_tofd(s),pp);
		if (e) e2=strerror(errno);
	} while(0);
	
	if (pp) probe_delete(pp);
	if (!SOCKINV(s)) sock_close(s);
	
	if (e) {
		if (e2) fprintf(stderr,"%s: %s (%s)\n",progname,e,e2);
		else    fprintf(stderr,"%s: %s\n",progname,e);
		return 1;
	}
	return 0;
}
