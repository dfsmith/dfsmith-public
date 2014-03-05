/* > sensord.c */
/* Daniel F. Smith, 2014 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <poll.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>

#define SOCKLIBINCLUDES
#include "socklib.h"

#define MAX(a,b) ((a)>(b)?(a):(b))
#define lengthof(x) (sizeof(x)/sizeof(*(x)))
#define TRACE(x)

#define TEMPTTY "/dev/arduinotty"
//#define TEMPTTY "testdata"
#define AVGTIME 60
#define MAXPROBES 10
#define PORT 8888

#define DBG(x) x

/* HTTP headers */
#define HEADER_OK_SET_CONTENTLENGTH(H,LEN) sprintf((H)+33,"%8d",LEN)
const char header_ok[]="HTTP/1.1 200 OK\r\nContent-Length: 00000000\r\n"
                       "Access-Control-Allow-Origin: *\r\n"
                       "Content-Type: text/plain\r\n\r\n";
const char header_bad[]="HTTP/1.1 400 Bad request\r\n\r\n";
const char header_notfound[]="HTTP/1.1 404 Not found\r\n\r\n";

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
	uint havetemp:1;
	uint haverh:1;
	uint havepressure:1;
} measurement;
#define MEASUREMENT_ZERO {0.0,0.0,0.0, 1,1,1}
const measurement measurement_null={0.0,0.0,0.0, 0,0,0};
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
};
const struct tempavg_s tempavg_zero={NULL,0,MEASUREMENT_ZERO,MEASUREMENT_ZERO};

typedef struct {
	/* not a list yet: struct probeport_s *next,*prev; */
	int fd;
	char *line;
	int current;
	int max;
	struct tempdata_s data;
} probeport;

static void measurementop(measurement *dest,char op,const measurement *arg) {
	#define DIVOP(READING,OP) do{if (arg->have##READING && arg->READING!=0.0) {dest->READING /= arg->READING;} else {dest->have##READING=0;}}while(0)
	#define UNIOP(READING,OP) do{if (arg->have##READING) {dest->READING OP;}}while(0)
	#define REGOP(READING,OP) do{if (arg->have##READING) {dest->READING OP arg->READING;}}while(0)
	#define DOOPS(DOOP,OP) do{DOOP(temp,OP); DOOP(rh,OP); DOOP(pressure,OP);}while(0)
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

static char *measurement_string(const measurement *m,int u) {
	static char line[100],*l,*e;
	l=line;
	e=&line[sizeof(line)-1];

	if (!m) {
		if (u) snprintf(l,e-l,"?temp ?humidity ?pressure");
		else   snprintf(l,e-l,"? ? ?");
		return line;
	}

	#define PRINTM(MM,UN,SP) \
		if (m->have##MM) l+=snprintf(l,e-l,"%f%s%s",m->MM,UN,SP); \
		else             l+=snprintf(l,e-l,"?%s%s",UN,SP);
	PRINTM(temp,u?"degC":""," ");
	PRINTM(rh,u?"%rh":""," ");
	PRINTM(pressure,u?"hPa":"","");
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
	return pp;
}

static int decodedata(probeport *p) {
	/* decode a complete line from the probe port */
	/* returns number of new data items (1 or 0) */
	char *l=p->line,*e;
	double x;
	
	//DBG(printf("decodedata:in \"%s\"\n",l);)
	
	if (strncmp("# ",l,2)==0) {
		/* comment */
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
			if (strcmp("hPa", l)==0) {p->data.m.pressure=x; p->data.m.havepressure=1;}
			if (strcmp("degC",l)==0) {p->data.m.temp    =x; p->data.m.havetemp=1;}
			if (strcmp("%rh", l)==0) {p->data.m.rh      =x; p->data.m.haverh=1;}

			if (!e) break;
			l=e;
		}
		gettimeofday(&p->data.when,NULL);
		DBG(printf("decodedata:out %s\n",measurement_string(&p->data.m,1));)
		return 1;
	}
	return 0; /* malformed */
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
		//DBG(printf("readmoredata: got %zd bytes\n",len);)
		if (len==-1) {err="bad read"; break;}
		if (len==0)  {err="no more data"; break;}
		p->current+=len;
		p->line[p->current]='\0';

		end=strchr(p->line,'\n');
		if (!end && p->current+1 >= p->max) {
			/* implicit '\n' at end of line */
			end=&p->line[p->current];
		}
		if (!end) {err="incomplete data"; break;}

		*end='\0';
		if (decodedata(p)) r=&p->data;
		else err="bad data decode";
		
		/* eat off decoded data */
		len=&p->line[p->current] - (end+1);
		memmove(p->line,end+1,len);
		p->current=len;
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
}

static char *probe_string(struct tempavg_s *p,int u) {
	measurement avg;
	
	if (!p) return measurement_string(NULL,u);
	avg=p->sum;
	measurementop(&avg,'/',&p->n);
	return measurement_string(&avg,u);
}

static struct tempavg_s *processdata(struct tempavg_s *probe,int *probes,struct tempdata_s *d) {
	if (!d) return probe;
	if (!probe) *probes=0;
	if (d->probe >= MAXPROBES) return probe;
	if (d->probe >= *probes) {
		probe=realloc(probe,sizeof(*probe)*(d->probe+1));
		if (!probe) {*probes=0; return NULL;}
		while(*probes <= d->probe)
			probe[(*probes)++]=tempavg_zero;
	}
	combineavg(&probe[d->probe],d);
	DBG(printf("processdata: %s\n",probe_string(&probe[d->probe],1));)
	return probe;
}

/* -- web server stuff -- */

struct serverlist_s {
	struct serverlist_s *next,*prev;
	int socket;
	enum {serverlist_reading,serverlist_writing,serverlist_processing,serverlist_closing} state;
	int count,buflen;
	#define URISPEC "%199s" /* stringify sizeof uri */
	char uri[200];
	char buf[3800];
};

static struct serverlist_s *server_new(int socket,struct serverlist_s *sl) {
	do {
		if (sl) socket=sl->socket;
		if (socket==-1) break;
		if (!sl) sl=calloc(1,sizeof(*sl));
		if (!sl) break;

		sl->socket=socket;
		sl->state=serverlist_reading;
		sl->count=0;
		sl->buflen=0;
		memset(sl->buf,0,sizeof(sl->buf));
	} while(0);
	return sl;
}

/* -- do stuff -- */

static const char *mainloop(int listenfd,probeport *pp) {
	fd_set readfds,writefds;
	int maxfd=0;
	struct tempavg_s *probe=NULL;
	int probes=0;
	struct timeval timeout;
	struct serverlist_s *serverlist=NULL,*sl,*closesl=NULL;
	int tmp;
	const char *e=NULL;
	
	if (pp->fd==-1) return "bad data tty";
	if (listenfd==-1) return "bad listening socket";
	
	for(;;) {
		/* set up select() */
		timeout.tv_sec=AVGTIME;
		timeout.tv_usec=0;
		FD_ZERO(&readfds); maxfd=0;
		FD_SET(pp->fd,&readfds); maxfd=MAX(maxfd,pp->fd);
		FD_SET(listenfd,&readfds); maxfd=MAX(maxfd,listenfd);
		for_dll(serverlist,sl) {
			switch(sl->state) {
			case serverlist_reading: FD_SET(sl->socket,&readfds); break;
			case serverlist_writing: FD_SET(sl->socket,&writefds); break;
			default: /* needs attention */
				timeout.tv_sec=0; break;
			}
			maxfd=MAX(maxfd,sl->socket);
		}
		tmp=select(maxfd+1,&readfds,NULL,NULL,&timeout);
		if (tmp==-1) {e="select failed"; break;}
		if (tmp==0) continue;

		/* process select() results */
		if (FD_ISSET(listenfd,&readfds)) {
			int ss;
			ss=accept(listenfd,NULL,NULL);
			sl=server_new(ss,NULL);
			if (sl) dll_add(serverlist,sl);
		}
		/* process select()'s probelist */
		if (FD_ISSET(pp->fd,&readfds)) {
			struct tempavg_s *p=probe;
			probe=processdata(probe,&probes,readmoredata(pp,&e));
			if (e) break;
			if (!probe && p) {e="nomem"; break;}
		}
		/* process select()'s serverlist */
		for_dll(serverlist,sl) {
			char *crlf;
			switch(sl->state) {
			case serverlist_reading:
				if (FD_ISSET(sl->socket,&readfds)) {
					ssize_t r;
					r=read(sl->socket,sl->buf+sl->buflen,sizeof(sl->buf)-sl->buflen-1);
					if (r==-1) {sl->state=serverlist_closing; break;}
					sl->buflen+=r;
					crlf=strstr(sl->buf,"\r\n\r\n"); /* buf is always terminated */
					if (sl->buflen>=sizeof(sl->buf)-1 || r==0 || crlf) {
						/* note: this may fail for multiple-request operations */
						TRACE(printf("GOT: %*s\n",sl->buflen,sl->buf);)
						sl->state=serverlist_processing;
						break;
					}
					break;
				}
				break;
			case serverlist_writing:
				if (FD_ISSET(sl->socket,&writefds)) {
					ssize_t w,send;
					send=sl->buflen - sl->count;
					TRACE(printf("SEND: %*s\n",sl->count,sl->buf);)
					w=write(sl->socket,sl->buf+sl->count,send);
					if (w==-1) sl->state=serverlist_closing;
					else if (w==0) sl->state=serverlist_closing;
					else if (w==send) sl=server_new(-1,sl); /* reset server */
					else sl->count+=w;
				}
				break;
			case serverlist_processing:
				#define OUTADD(...) do { \
					if (sl->buflen < sizeof(sl->buf)) \
						sl->buflen+=snprintf(sl->buf+sl->buflen,sizeof(sl->buf)-sl->buflen,__VA_ARGS__); \
					} while(0)
				#define OUT_OK() \
					HEADER_OK_SET_CONTENTLENGTH(sl->buf,sl->buflen-sizeof(header_ok)+1);
				#define OUT(...) \
					do { \
						sl->buflen=0; \
						OUTADD(__VA_ARGS__); \
						sl->state=serverlist_writing; \
					} while(0)

				/* buf must start with requst-line */
				if (sl->buflen<4 || sscanf(sl->buf,"GET " URISPEC,sl->uri)!=1) {
					TRACE(printf("FAIL: %s\n",sl->buf);)
					sl->state=serverlist_closing;
					break;
				}
				TRACE(printf("SENDING: %s\n",sl->uri);)
				if (strcmp(sl->uri,"/")==0) {
					OUT("%s# temperature server\r\n# probe degC %%rh\r\n",header_ok);
					OUT_OK();
					break;
				}
				if (strcmp(sl->uri,"/probes")==0) {
					int i;
					OUT("%s",header_ok);
					for(i=0;i<probes;i++) {
						OUTADD("%d %s\r\n",i,probe_string(&probe[i],0));
					}
					OUT_OK();
					break;
				}
				if (strcmp(sl->uri,"/probeline")==0) {
					int i;
					long long int t;
					struct timeval tv;
					gettimeofday(&tv,NULL);
					t=tv.tv_sec;
					OUT("%s%lld",header_ok,t);
					for(i=0;i<probes;i++)
						OUTADD(" %s",probe_string(&probe[i],0));
					OUTADD("\r\n");
					OUT_OK();
					break;
				}
				if (sscanf(sl->uri,"/probe/%d",&tmp)==1) {
					if (tmp<0 || tmp>=probes) {
						OUT("%s",header_bad);
					}
					else {
						struct tempdata_s *d;
						OUT("%s",header_ok);
						d=dll_head(probe[tmp].list);
						OUTADD("%s\r\n",measurement_string(&d->m,0));
						OUT_OK();
					}
					break;
				}
				/* otherwise... */
				OUT("%s",header_notfound);
				break;
			case serverlist_closing:
				/* will eventually get all of them */
				TRACE(printf("CLOSING\n");)
				closesl=sl;
				break;
			}
		}
		if (closesl) {
			close(closesl->socket);
			dll_remove(serverlist,closesl);
			free(closesl);
			closesl=NULL;
		}
	}
	while(!dll_empty(serverlist))
		dll_remove(serverlist,dll_head(serverlist));
	for(tmp=0;tmp<probes;tmp++)
		combineavg(&probe[tmp],NULL);
	free(probe);
	return e;
}

int main(int argc,char *argv[]) {
	const char *e=NULL,*e2=NULL;
	SOCKET s=SOCKET_ERROR;
	const char *progname;
	probeport *pp=NULL;
	const char *probename=TEMPTTY;
	int port=PORT;

	/* debugging */
	DBG(probename="testdata";)
	DBG(port=PORT+1;)
	
	progname=*argv++; argc--;

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
