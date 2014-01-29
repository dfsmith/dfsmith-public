#ifndef dfs_tcl_proc_h
#define dfs_tcl_proc_h

struct tcl_handle_s;
typedef struct tcl_handle_s tcl_handle;

extern tcl_handle *tcl_open(const char *desthostname,unsigned int lights,const char **e);
extern int tcl_getlights(tcl_handle *tcl);
extern int tcl_addexithandler(tcl_handle *tcl,void (*exitfunc)(tcl_handle *,void*),void *context);
extern unsigned char *tcl_getrgbbuf(tcl_handle *tcl);
extern int tcl_sendrgb(tcl_handle *tcl);
extern void tcl_close(tcl_handle *tcl);

#endif
