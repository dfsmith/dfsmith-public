#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <errno.h>
#include "tcl-patterns.h"
#include "lib/fft.h"

typedef unsigned int uint;
typedef unsigned char u8;
typedef unsigned char bool;
#define FALSE 0
#define TRUE (!FALSE)

#define ABS(x) (((x)<0)?-(x):(x))

static FILE *opensound(int source,char **e) {
	static FILE *in=NULL;
	static int (*close)(FILE *)=NULL;
	static int insrc=-1;
	const char *sources[]={	/* pactl list short sources */
		"pamon --format=u8 --channels=2 -d alsa_output.pci-0000_00_1b.0.analog-stereo.monitor",
		"pamon --format=u8 --channels=2 -d alsa_input.pci-0000_00_1b.0.analog-stereo",
		"pamon --format=u8 --channels=2 -d jack_in",
	};
	
	if (source==insrc) return in;
	
	/* close old, and open new */
	if (close && in) close(in);
	insrc=source;
	in=popen(sources[source],"rb");
	if (in) close=pclose;
	if (!in && e) *e=strerror(errno);
	return in;
}

static float prorate(float f) {
	if (f<0.0) return 0.0;
	if (f>1.0) return 1.0;
	return f;
}

static uint translate(int full) {
	static uint *table=NULL;
	if (!table) {
		uint x=0,i,stepping=0;
		table=malloc(41*sizeof(*table));
		if (!table) return full;
		for(i=0;i<40;i++) {
			table[i]=x>255?255:x;
			stepping=1U<<(i/8);
			x+=stepping;
		}
		table[i++]=255;
	}
	if (full<-40) full=40; /* some ints can't be negativeized */
	if (full<0) full=-full;
	if (full>40) full=40;
	return table[full];
}

static void patdel(tcl_pattern_data *pd) {
	if (pd->data) free(pd->data);
	free(pd);
}

static tcl_pattern_data *newpatterndata(tcl_patternbuf *buf,size_t extra,tcl_pattern_api *m,int params,...) {
	tcl_pattern_data *pd;
	va_list ap;
	int i;
	
	pd=calloc(1,sizeof(*pd) + params*sizeof(double));
	if (!pd) return NULL;
	if (extra>0) {
		pd->data=calloc(1,extra);
		if (!pd->data) {free(pd); return NULL;}
	}
	pd->m=m;
	pd->buf=buf;
	pd->gain=1.0;
	pd->params=params;
	pd->param=(void*)(pd+1);
	va_start(ap,params);
	for(i=0;i<params;i++)
		pd->param[i]=va_arg(ap,double);
	va_end(ap);
}

/* -- zero -- */

static int zero(tcl_pattern_data *pd) {
	int i,j;
	tcl_patternbuf *b=pd->buf;
	for(i=0;i<b->leds;i++) {
		for(j=0;j<TCL_CHANNELS;j++)
			b->led[i].chan[j]=0.0;
	}
	return 0;
}

tcl_pattern_data *tcl_pattern_zero(tcl_patternbuf *buf) {
	static tcl_pattern_api m={zero,patdel};
	return newpatterndata(buf,0,&m,0);
}

/* -- fade -- */

static int fade(tcl_pattern_data *pd) {
	int i,j;
	tcl_patternbuf *b=pd->buf;
	for(i=0;i < b->leds;i++) {
		for(j=0;j < TCL_CHANNELS;j++)
			b->led[i].chan[j] *= pd->param[0];
	}
	return 0;
}

tcl_pattern_data *tcl_pattern_fade(tcl_patternbuf *buf,double retain) {
	static tcl_pattern_api m={fade,patdel};
	return newpatterndata(buf,0,&m,1,retain);
}

/* -- bleed -- */

static int bleed(tcl_pattern_data *pd) {
	int i;
	double bleed;
	tcl_patternbuf *b=pd->buf;
	bleed=pd->param[0];
	for(i=0;i < b->leds;i++) {
		int j;
		for(j=0;j < TCL_CHANNELS;j++) {
			if (i>0)         b->led[i].chan[j] += b->led[i-1].chan[j] * bleed;
			if (i<b->leds-1) b->led[i].chan[j] += b->led[i+1].chan[j] * bleed;
		}
	}
	return 0;
}

tcl_pattern_data *tcl_pattern_bleed(tcl_patternbuf *buf,double bleedval) {
	static tcl_pattern_api m={bleed,patdel};
	return newpatterndata(buf,0,&m,1,bleedval);
}

/* -- sinus -- */

static int sinus(tcl_pattern_data *pd) {
	tcl_pixelfloat *p;
	double r,g,b;
	int i;
	#define N(x) sqrt((x+1.0)*0.5)
	
	r=pd->param[0]; pd->param[0]+=pd->param[6];
	g=pd->param[1]; pd->param[1]+=pd->param[7];
	b=pd->param[2]; pd->param[2]+=pd->param[8];
	for(i=0;i < pd->buf->leds; i++) {
		p=&pd->buf->led[i];
		p->r += N(sin(r)); r+=pd->param[3];
		p->g += N(sin(g)); g+=pd->param[4];
		p->b += N(sin(b)); b+=pd->param[5];
	}
	return 0;
}	

tcl_pattern_data *tcl_patternsinus(tcl_patternbuf *buf) {
	static tcl_pattern_api m={sinus,patdel};
	return newpatterndata(buf,0,&m,9,0.0,0.0,0.0, 0.66,0.62,0.74, 0.1,0.11,0.13);
}	

/* -- cylon -- */

static int cylon(tcl_pattern_data *pd) {
	tcl_pixelfloat *p1=NULL,*p2=NULL;
	double pos;
	int p;
	int leds;
	/* param[0] -> last milliseconds */
	/* param[1] -> position */
	/* param[2] -> speed of movement */
	/* param[3...] -> color of pixel */
	
	leds=pd->buf->leds;

	pd->param[1]+=(pd->buf->milliseconds - pd->param[0])*pd->param[1] * pd->buf->;
	pd->param[0]=pd->buf->milliseconds;
	if (pd->param[1] >= leds) pd->param[2]=-ABS(pd->param[2]);
	if (pd->param[1] <  0.0 ) pd->param[2]=+ABS(pd->param[2]);
	pos=pd->param[1];

	p=floor(pos);
	pos-=p;
	if (p>=0 && p < leds) p1=&pd->buf->led[p];
	if (      p+1 < leds) p2=&pd->buf->led[p+1];

	/* split between pixels */
	for(j=0;j<TCL_CHANNELS;j++) {
		if (p1) p1->chan[j] += pd->param[2+j] * prorate(1.0-pos);
		if (p2) p2->chan[j] += pd->param[2+j] * prorate(   +pos);
	}
	return 0;
}

tcl_pattern_data *tcl_patterncylon(tcl_patternbuf *buf) {
	static tcl_pattern_api m={cylon,patdel};
	return newpatterndata(buf,&m,3+TCL_CHANNELS, 0,0,+0.1, 1.0,2.0,1.0);
}	

/* -- wave -- */

typedef struct {
	float y,yv;
} particle;

static particle *wave(particle *src,float kom,uint len,particle **doublebuf) {
	uint i;
	particle *dest,*p;
	float acc;
	dest=*doublebuf;
	for(i=0;i<len;i++) {
		p=&src[i];
		acc=-kom*(p[0].y - 0.5*(p[1].y + p[-1].y));
		dest[i].yv =0.5*(p[0].yv+acc);
		dest[i].y  =p[0].y +p[0].yv ;
	}
	*doublebuf=src;
	return dest;
}

static void patternwave(SOCKET s,uint len) {
	particle *march,*red,*green,*blue,*buf;
	uint i;
	u8 *packet;
	
	/* set up memory, note green offset */
	packet=calloc(3*len,sizeof(*packet));
	if (!packet) return;
	buf=calloc(4*(len+2),sizeof(*buf));
	if (!buf) {free(packet); return;}

	gvset(1.0,1.0,1.0,0,0,0,0,0, 0,0,0,0,0,0,0,0);

	march=buf+1;
	red  =&march[1*(len+2)];
	green=&march[2*(len+2)];
	blue =&march[3*(len+2)];
	
	for(i=-1;i<len+2;i++) {
		red[i].y=red[i].yv=0.0;
		green[i].y=green[i].yv=0.0;
		blue[i].y=blue[i].yv=0.0;
	}
	red[1*len/4].y=0.0;
	green[2*len/4].y=0.0;
	blue[3*len/4].y=0.0;
	
	for(;;) {
		red=wave(red,0.1,len,&march);
		green=wave(green,0.3,len,&march);
		blue=wave(blue,0.2,len,&march);
	
		/* transfer to buffer and send */
		red[-1].y=(gv2(0)-1.0)*40;
		green[-1].y=(gv2(1)-1.0)*40;
		blue[-1].y=(gv2(2)-1.0)*40;
		for(i=0;i<len;i++) {
			uint c;
			packet[3*i+0]=translate(red[i].y);
			packet[3*i+1]=translate(green[i].y);
			packet[3*i+2]=translate(blue[i].y);
			c=ceil(fabsf(green[i].y>100?100:green[i].y));
			putchar('0'+((c>9)?'X'-'0':c));
			/*if (i<30) printf("%20f %20f %u\n",green[i].y,green[i].yv,c);*/
		}
		putchar('\n');
		if (sendwait(s,packet,len)<0) break;
	}
	
	free(buf);
	free(packet);
}

/* -- sparkles -- */

static void patternsparkles(SOCKET s,uint len) {
	pixelfloat *backing,*buf;
	int i,c;
	#define HALLOWEEN 1
	#if HALLOWEEN
	const pixelfloat orange={{1,0.3,0.1}},green={{0,1,0}},purple={{1.0,0,0.8}};
	const pixelfloat weight[]={
		orange,orange,orange,orange,orange,orange,orange,
		green,
		purple,purple,purple,
	};
	#endif
	
	backing=calloc(len,sizeof(*backing));
	if (!backing) return;
	buf=calloc(len,sizeof(*buf));
	if (!buf) {free(backing); return;}
	
	/* create_density,bleed,fade_speed */
	gvset(1.7,0.0,0.08,0,0,0,0,0, 0,0,0,0,0,0,0,0);
	for(;;) {
		if (rand()%(101-(int)(50*gv2(0)))==0) {
			i=rand()%len;
			#if HALLOWEEN
			c=rand()%lengthof(weight);
			buf[i]=weight[c];
			#elif 1
			/* RGB saturated */
			c=1+rand()%7;
			buf[i].r=(c&1)?1:0;
			buf[i].g=(c&2)?1:0;
			buf[i].b=(c&4)?1:0;
			#endif
		}
		else i=-1;

		if (mergesendwait(s,buf,backing,len,0.25*gv2(1),0.5*gv2(2))<0) break;
		if (i>=0) buf[i].r=buf[i].g=buf[i].b=0;
	}
	
	free(buf);
	free(backing);
}

/* -- sparticles -- */

typedef struct {
	float x,xv;
	float r,g,b;
} sparticle;

static void patternsparticles(SOCKET s,uint len) {
	pixelfloat *backing,*buf;
	int i,c;
	const uint maxsparticles=100;
	sparticle *sp;
	
	backing=calloc(len+1,sizeof(*backing));
	if (!backing) return;
	buf=calloc(len+1,sizeof(*buf));
	if (!buf) {free(backing); return;}
	sp=calloc(maxsparticles,sizeof(*sp));
	if (!sp) {free(backing); free(buf); return;}
	
	gvset(1.0,0.2,0.8,0,0,0,0,1.0, 0,0,0,0,0,0,0,0);
	for(;;) {
		do {
			int n,pos;
			if (rand()%(101-(int)(50*gv2(0)))!=0) break;
			/* new sparticles */
			pos=rand()%len;
			for(n=1+rand()%10;n>0;n--) {
				for(i=0;i<maxsparticles;i++) {
					if (sp[i].x<0) break;
				}
				if (i>=maxsparticles) break;
				c=1+rand()%7;
				sp[i].x=pos;
				sp[i].xv=((rand()%31)-15)*(gv2(7)*0.1);
				sp[i].r=(c&1)?1:0;
				sp[i].g=(c&2)?1:0;
				sp[i].b=(c&4)?1:0;
			}
		} while(0);
		
		for(i=0;i<len;i++)
			buf[i].r=buf[i].g=buf[i].b=0.0;
		for(i=0;i<maxsparticles;i++) {
			sparticle *s=&sp[i];
			int x;
			float p,q;
			x=floor(s->x);
			if (x>=len || x<0) {/* kill sparticle */ s->x=-1; s->xv=0; continue;}
			q=s->x - x;
			p=1.0-q;
			buf[x].r+=p*s->r; buf[x+1].r+=q*s->r;
			buf[x].g+=p*s->g; buf[x+1].g+=q*s->g;
			buf[x].b+=p*s->b; buf[x+1].b+=q*s->b;
			/* update sparticle */
			s->r-=0.05; if (s->r<0) s->r=0;
			s->g-=0.05; if (s->g<0) s->g=0;
			s->b-=0.05; if (s->b<0) s->b=0;
			if (s->r==0 && s->g==0 && s->b==0) {s->x=-1; continue;}
			s->x+=s->xv;
			s->xv*=0.95;
		}
	
		if (mergesendwait(s,buf,backing,len,0.5*gv2(1),0.5*gv2(2))<0) break;
	}
	
	free(buf);
	free(backing);
}

/* -- test -- */

static void patterntest1(SOCKET s,uint len) {
	u8 *packet;
	uint i,col;
	packet=malloc(3*len);
	if (!packet) return;
	for(col=0;col<4;col++) {
		for(i=0;i<3*len;i++) {
			packet[i]=(i<123 && i%3==col)?translate(i/3):0;
			if (i%3==col) printf("%u %u\n",i,packet[i]);
		}
		if (sendwait(s,packet,len)<0) break;
		usleep(1000000);
	}
	free(packet);
}

/* -- vu -- */

static void vu(FILE *in,float *lsum,float *rsum,float *lpeak,float *rpeak) {
	const uint samples=1024;
	int i=0;
	float left,right;
	*lsum=*rsum=*lpeak=*rpeak=0.0;
	for(i=0;!feof(in) && i<samples;i++) {
		left=abs(fgetc(in)-0x80);	right=abs(fgetc(in)-0x80);
		*rsum+=right*right;		*lsum+=left*left;
		if (right>*rpeak) *rpeak=right;	if (left>*lpeak) *lpeak=left;
		
	}
	*lpeak=-20*log10f(*lpeak/128);	*rpeak=-20*log10f(*rpeak/128);
	*lsum=sqrt(*lsum/samples)/128;	*rsum=sqrt(*rsum/samples)/128;
	*lsum=20*log10f(*lsum);		*rsum=20*log10f(*rsum);
	*lsum-=30*(1.0-gv2(0));		*rsum-=30*(1.0-gv2(1));
}

static void patternvu(SOCKET s,uint len) {
	FILE *in;
	float lsum,rsum,lpeak,rpeak;
	float dot,ldot=0,prop;
	uint i;
	pixelfloat *backing,*buf;
	
	backing=calloc(len,sizeof(*backing));
	if (!backing) return;
	buf=calloc(len,sizeof(*buf));
	if (!buf) {free(backing); return;}
	in=opensound(NULL);
	if (!in) {free(backing); free(buf); return;}

	gvset(1,1,0,0,0,0,0,0, 0,0,0,0,0,0,0,0);
	syssleeptime=0;
	
	while(!feof(in)) {
		vu(in,&lsum,&rsum,&lpeak,&rpeak);
		printf("left %5.1fdB    %5.1fdB right\n",lsum,rsum);
		dot=(lpeak>rpeak)?rpeak:lpeak;
		dot=(ldot<dot)?ldot:dot;
		for(i=0;i<len;i++) {
			prop=prorate(len-dot-i+1) - prorate(len-dot-i);
			buf[i].r=prorate(len-i+lsum) + prop;
			buf[i].g=prorate(    i+rsum) + prop;
			buf[i].b=                    + prop;
			if (len-i==dot) buf[i].r=buf[i].g=buf[i].b=1.0;
		}
		ldot=dot+0.2;
		if (mergesendwait(s,buf,backing,len,0.1,0.4)<0) break;
	}
	opensound(in);
	free(backing);
	free(buf);
}

/* -- shoutpong -- */

static void patternshoutpong(SOCKET s,uint len) {
	FILE *in;
	float lsum,rsum,lpeak,rpeak;
	float dot,dotv=+0.5,prop;
	float pl,pr;
	uint i;
	pixelfloat *backing,*buf;
	
	backing=calloc(len,sizeof(*backing));
	if (!backing) return;
	buf=calloc(len,sizeof(*buf));
	if (!buf) {free(backing); return;}
	in=opensound(NULL);
	if (!in) {free(backing); free(buf); return;}

	gvset(1,1,0,0,0,0,0,0, 0,0,0,0,0,0,0,0);
	syssleeptime=0;
	dot=len/2;
	
	while(!feof(in)) {
		vu(in,&lsum,&rsum,&lpeak,&rpeak);
		dot+=dotv;
		if (dot<1.0) dot=1.0;
		if (dot>len-1) dot=len-1.5;
		pl=len + lsum;
		pr=    - rsum;
		if (dotv<0.0 && pl>dot) {dot=pl; dotv=-dotv;}
		if (dotv>0.0 && pr<dot) {dot=pr; dotv=-dotv;}
		printf("left %5.1f  <%5.1f>  %5.1f right\n",pl,dot,pr);
		for(i=0;i<len;i++) {
			prop=prorate(len-dot-i+1) - prorate(len-dot-i);
			buf[i].r=prorate(    i+lsum) + prop;
			buf[i].g=prorate(len-i+rsum) + prop;
			buf[i].b=                    + prop;
		}
		if (mergesendwait(s,buf,backing,len,0.1,0.4)<0) break;
	}
	opensound(in);
	free(backing);
	free(buf);
}

/* -- fft -- */

static float smerge(cfloat *data,uint group,uint i) {
	int j;
	float sum=0.0;
	for(j=0;j<group;j++) {
		sum+=data[i*group+j];
	}
	return sum/group;
}

static void patternfft(SOCKET s,uint len) {
	FILE *in;
	const uint samples=2048;
	cfloat left[samples],right[samples],*samp,peak,scale;
	uint i,li,ri;
	pixelfloat *backing,*buf;
	
	backing=calloc(len,sizeof(*backing));
	if (!backing) return;
	buf=calloc(len,sizeof(*buf));
	if (!buf) {free(backing); return;}
	in=opensound(NULL);
	if (!in) {free(backing); free(buf); return;}
	
	gvset(1,1,1,0,0,0,0,1, 0,0,0,0,0,0,0,0);
	syssleeptime=0;
	
	li=0;
	ri=samples/2;
	while(!feof(in)) {
		left[li++]=fgetc(in)-0x80;
		right[ri++]=fgetc(in)-0x80;
		samp=NULL;
		if (ri>=samples) {samp=right; ri=0;}
		if (li>=samples) {samp=left; li=0;}
		if (!samp) continue;
		
		fft_real(samp,samples,+1);
		peak=fft_power((void*)samp,samples/2)/16;
		fft_window(samp,samples/2);
		peak=3200000;
		scale=10*gv2(7)/peak;
		#if 0
		for(i=0;i<20;i++)
			printf("%f\n",samp[i]);
		printf("\n");
		#endif

		for(i=0;i<len;i++) {
			buf[i].r=gv2(0)*smerge(samp+8,2,i)*scale;
			buf[i].g=gv2(1)*smerge(samp+4,1,i)*scale;
			buf[i].b=gv2(2)*smerge(samp+16,4,i)*scale;
		}
		if (mergesendwait(s,buf,backing,len,0.0,0.3)<0) break;
		i=0;
	}
	opensound(in);
	free(backing);
	free(buf);
}
