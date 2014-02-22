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

struct tempdata_s {
	struct tempdata_s *next,*prev;
	struct timeval when;
	int probe;
	double temp;
	double rh;
	double pressure;
};

struct tempavg_s {
	struct tempdata_s *list;
	struct tempdata_s sum; /* sum of items in list */
	int n; /* number of items in list */
	int sumreset; /* when 0, recalculate sum */
};
const struct tempavg_s tempavg_zero={NULL,{},0,0};

static struct tempdata_s *decodedata(char *line) {
	static struct tempdata_s d;
	if (sscanf(line,"probe %d %lfdegC %lf%%rh %lfhPa",
		&d.probe,&d.temp,&d.rh,&d.pressure)!=4) {
		return NULL;
	}
	gettimeofday(&d.when,NULL);
	return &d;
}

static struct tempdata_s *readmoredata(int fd,const char **error) {
	static int counter=0;
	static char line[60]={}; /* keep last char '\0' */
	char *c;
	ssize_t len;
	
	len=read(fd,&line[counter],sizeof(line)-1-counter);
	if (len==-1) {if (error) *error="bad read"; return NULL;}
	c=strchr(line,'\n');
	if (len==0 && !c) {if (error) *error="no more data"; return NULL;}
	counter+=len;

	if (c || counter>=sizeof(line)-1) {
		struct tempdata_s *d;

		if (!c) c=&line[sizeof(line)-1];
		*c='\0';
		d=decodedata(line);
		/* eat off decoded data */
		c++;
		len=&line[counter]-c;
		for(counter=0;len>0;len--) {
			line[counter++]=*c++;
		}
		return d;
	}
	return NULL;
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
		p->sum.temp=0.0;
		p->sum.rh=0.0;
		for_dll(p->list,d) {
			p->sum.temp+=d->temp;
			p->sum.rh+=d->rh;
		}
	}

	/* expire old values */
	while(!dll_empty(p->list)) {
		d=dll_tail(p->list);
		if (tvdiff(&tv,&d->when) < AVGTIME) break;
		dll_remove(p->list,d);
		p->sum.temp-=d->temp;
		p->sum.rh-=d->rh;
		p->n--;
		free(d);
	}

	/* add new value */
	d=calloc(1,sizeof(*d));
	if (!d) return;
	*d=*newdata;
	p->sum.temp+=d->temp;
	p->sum.rh+=d->rh;
	p->n++;
	dll_add(p->list,d);
}

static char *probe_string(struct tempavg_s *p) {
	static char line[100];
	if (p->n < 1)
		snprintf(line,sizeof(line),"? ?");
	else
		snprintf(line,sizeof(line),"%.3f %.2f",
			p->sum.temp/p->n,
			p->sum.rh/p->n);
	return line;
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
	return probe;
}

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

static const char *mainloop(int fd,int lfd) {
	fd_set readfds,writefds;
	int maxfd=0;
	struct tempavg_s *probe=NULL;
	int probes=0;
	struct timeval timeout;
	struct serverlist_s *serverlist=NULL,*sl,*closesl=NULL;
	int tmp;
	const char *e=NULL;
	
	if (fd==-1) return "bad data tty";
	if (lfd==-1) return "bad listening socket";
	
	for(;;) {
		/* set up select() */
		timeout.tv_sec=AVGTIME;
		timeout.tv_usec=0;
		FD_ZERO(&readfds); maxfd=0;
		FD_SET(fd,&readfds); maxfd=MAX(maxfd,fd);
		FD_SET(lfd,&readfds); maxfd=MAX(maxfd,lfd);
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
		if (FD_ISSET(fd,&readfds)) {
			struct tempavg_s *p=probe;
			probe=processdata(probe,&probes,readmoredata(fd,&e));
			if (e) break;
			if (!probe && p) {e="nomem"; break;}
		}
		if (FD_ISSET(lfd,&readfds)) {
			int ss;
			ss=accept(lfd,NULL,NULL);
			sl=server_new(ss,NULL);
			if (sl) dll_add(serverlist,sl);
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
						OUTADD("%d %s\r\n",i,probe_string(&probe[i]));
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
						OUTADD(" %s",probe_string(&probe[i]));
					OUTADD("\r\n");
					OUT_OK();
					break;
				}
				if (sscanf(sl->uri,"/probe/%d",&tmp)==1) {
					if (tmp<0 || tmp>=probes)
						OUT("%s",header_bad);
					else {
						struct tempdata_s *d;
						OUT("%s",header_ok);
						d=dll_head(probe[tmp].list);
						if (d)
							OUTADD("%f degC %f %%rh\r\n",d->temp,d->rh);
						else
							OUTADD("? degC ? %%rh\r\n");
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
	int tfd=-1;
	const char *e=NULL,*e2=NULL;
	SOCKET s=SOCKET_ERROR;
	const char *progname;
	
	progname=*argv++; argc--;

	do {
		tfd=open(TEMPTTY,O_RDONLY);
		if (tfd==-1) {e="cannot open"; e2=TEMPTTY; break;}
	
		sock_setreuse(1);
		s=sock_getsocketto(NULL,-PORT,SOCK_STREAM,&e2);
		if (SOCKINV(s)) {e="cannot create socket"; break;}

		e=mainloop(tfd,sock_tofd(s));
		if (e) e2=strerror(errno);
	} while(0);
	
	if (tfd!=-1) close(tfd);
	if (!SOCKINV(s)) sock_close(s);
	
	if (e) {
		if (e2) fprintf(stderr,"%s: %s (%s)\n",progname,e,e2);
		else    fprintf(stderr,"%s: %s\n",progname,e);
		return 1;
	}
	return 0;
}
