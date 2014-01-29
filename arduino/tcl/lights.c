#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include "tcl-proc.h"
#include "tcl-patterns.h"

#define lengthof(x) (sizeof(x)/sizeof(*(x)))

int main(int argc,char *argv[]) {
	const char *progname;
	const char *e=NULL;
	const char *dest="xmas.dfsmith.net";
	unsigned int lights=100;
	tcl_handle *tcl=NULL;
	tcl_pattern_data *pattern[20];
	tcl_patternbuf *buf=NULL;
	int patterns=0;
	int errcount;
	int i;
	unsigned long int cms;
	struct timeval start,current;
	unsigned int mode=-1,currentmode,modebase,modes;
	
	progname=*(argv++); argc--;
	if (argc>0) {dest=*(argv++); argc--;}
	if (argc>0) {mode=atoi(*(argv++)); argc--;}
	if (argc>0) {lights=atoi(*(argv++)); argc--;}

	do {
		tcl=tcl_open(dest,lights,&e);
		if (!tcl) {
			fprintf(stderr,"%s: tcl_open(%s)->%s\n",progname,dest,e);
			break;
		}
		buf=tcl_patternbuf_new(lights);
		if (!buf) {
			fprintf(stderr,"%s: tcl_patternbuf_new(%d) failed\n",progname,lights);
			break;
		}
		gettimeofday(&start,NULL);

		/* init */
		pattern[patterns++]=tcl_patternzero(buf);
		
		/* overlays */
		modebase=patterns;
		pattern[patterns++]=tcl_patternsinus(buf);
		pattern[patterns++]=tcl_patterncylon(buf);
		pattern[patterns++]=tcl_patternwave(buf);
		pattern[patterns++]=tcl_patternsparkles(buf);
		pattern[patterns++]=tcl_patternsparticles(buf);
		#if 0
		pattern[patterns++]=tcl_patternvu(buf);
		#elif 0
		pattern[patterns++]=tcl_patternfft(buf);
		#elif 0
		pattern[patterns++]=tcl_patternsshoutpong(buf);
		#endif
		modes=patterns-modebase;
		
		/* post-processing */
		pattern[patterns++]=tcl_pattern_bleed(buf,0.05);
		pattern[patterns++]=tcl_pattern_fade(buf,0.0);
		pattern[patterns++]=tcl_pattern_torgbuchar(buf,tcl_getrgbbuf(tcl));
		pattern[patterns++]=NULL;
		
		if (patterns > lengthof(pattern)) {
			fprintf(stderr,"%s: too many patterns---recompile\n",progname);
			break;
		}

		/* main loop */
		do {
			currentmode=((mode==-1)?rand():mode) % modes;
			printf("Starting mode %u (setmode %d)\n",currentmode,mode);
			
			/* drawing */
			buf->milliseconds=0;
			errcount=0;
			for(i=0;pattern[i];i++) {
				if (pattern[i]) {errcount++; continue;}
				if (i==currentmode+modebase)
					pattern[i]->gain=1.0;
				else
					pattern[i]->gain*=0.98;
				errcount+=pattern[i]->m->overlay(pattern[i]);
			}
			
			/* timing */
			buf->milliseconds+=50;
			gettimeofday(&current,NULL);
			cms=difftime(current.tv_sec,start.tv_sec)*1000;
			cms+=(current.tv_usec - start.tv_usec)/1000;
			if (buf->milliseconds <= cms) buf->milliseconds=cms+1;
			usleep((buf->milliseconds - cms)*1000);

			tcl_sendrgb(tcl);
		} while(errcount==0);
		
	} while(0);

	for(i=0;pattern[i];i++)
		pattern[i]->m->delete(pattern[i]);
	tcl_patternbuf_delete(buf);
	tcl_close(tcl);
	return 0;
}
 