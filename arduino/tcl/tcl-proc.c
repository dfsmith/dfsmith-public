#define _XOPEN_SOURCE 600
#include "tcl-proc.h"
#define SOCKLIBINCLUDES
#include "socklib.h"

struct tcl_exitchain;

typedef struct {
	SOCKET s;
	unsigned int delay_ms;
	unsigned int lights;
	struct tcl_exitchain *exitchain;
} tcl_handle;

struct tcl_exitchain {
	struct tcl_exitchain *next;
	void *context;
	void (*exitfunc)(tcl_handle *tcl,void *context);
};

int tcl_getlights(tcl_handle *tcl) {
	if (tcl) return tcl->lights;
	return -1;
}

int tcl_sendrgb(tcl_handle *tcl,void *buffer) {
	int nowarn;
	nowarn=sock_write(tcl->s,buffer,3*tcl->lights);
	if (nowarn!=3*tcl->lights) return -1;
	return 0;
}

int tcl_addexithandler(tcl_handle *tcl,void (*exitfunc)(tcl_handle *,void*),void *context) {
	struct tcl_exitchain *c;
	if (!tcl) return -1;
	c=calloc(1,sizeof(*c));
	if (!c) return -1;
	
	c->next=tcl->exitchain;
	c->context=context;
	c->exitfunc=exitfunc;
	
	tcl->exitchain=c;
	return 0;
}

tcl_handle *tcl_open(const char *desthostname,unsigned int lights,const char *e) {
	tcl_handle *tcl;
	
	tcl=calloc(1,sizeof(*tcl);
	if (!tcl) return NULL;

	do {
		tcl->s=sock_getsocketto(host,8888,SOCK_DGRAM,&e);
		if (SOCKINV(s)) break;

		tcl->delay_ms=50;
		tcl->exitchain=NULL;
		return tcl;
	} while(0);
	
	free(tcl);
	return NULL;
}

void tcl_close(tcl_handle *tcl) {
	if (!tcl) return;
	while(tcl->exitchain) {
		struct tcl_exitchain *c;
		c=tcl->exitchain;
		tcl->exitchain=c->next;
		
		c->exitfunc(tcl,c->context);
	}
	sock_close(tcl->s);
}
