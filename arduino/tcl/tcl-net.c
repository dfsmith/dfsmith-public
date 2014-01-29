#define _XOPEN_SOURCE 600
#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#define SOCKLIBINCLUDES
#include "socklib.h"
#include "lib/fft.h"

#define lengthof(x) (sizeof(x)/sizeof(*(x)))
#define MAX(a,b) ((a)>(b)?(a):(b))

typedef unsigned int uint;
typedef unsigned char u8;

typedef struct {
	/* changed externally */
	struct {
		uint count;
		uint mode;
		uint source;
		uint info;
		uint data[16];
	} set;
	/* changed internally */
	struct {
		uint count;
		uint mode;
		uint source;
		uint info;
		uint data[16];
	} current;
} sysstatus_t;
#define SETCURRENT(field,val) do {if (!sysstatus) break;\
	sysstatus->current.field=val;\
	sysstatus->current.count++;\
	}while(0)
static sysstatus_t *sysstatus=NULL;
static int syssetcount=0;
static uint syssleeptime=10000;

typedef struct {
	u8 r,g,b;
} pixel;
const pixel zero={0,0,0};

#define CHANNELS 3
typedef union {
	float chan[CHANNELS];
	struct {
		float r,g,b;
	};
} pixelfloat;
const pixelfloat zerof={{0.0,0.0,0.0}};

static float gv2(uint val) {
	float r;
	if (!sysstatus) return 1.0;
	if (val>lengthof(sysstatus->set.data)) return 1.0;
	r=(2.0/16384)*sysstatus->set.data[val];
	SETCURRENT(data[val],sysstatus->set.data[val]);
	return r;
}
static void gvset(
	float a0,float a1,float a2,float a3,
	float a4,float a5,float a6,float a7,
	float b0,float b1,float b2,float b3,
	float b4,float b5,float b6,float b7) {
	if (!sysstatus) return;
	sysstatus->set.data[ 0]=sysstatus->current.data[0]=8192*a0;
	sysstatus->set.data[ 1]=sysstatus->current.data[1]=8192*a1;
	sysstatus->set.data[ 2]=sysstatus->current.data[2]=8192*a2;
	sysstatus->set.data[ 3]=sysstatus->current.data[3]=8192*a3;
	sysstatus->set.data[ 4]=sysstatus->current.data[4]=8192*a4;
	sysstatus->set.data[ 5]=sysstatus->current.data[5]=8192*a5;
	sysstatus->set.data[ 6]=sysstatus->current.data[6]=8192*a6;
	sysstatus->set.data[ 7]=sysstatus->current.data[7]=8192*a7;
	sysstatus->set.data[ 8]=sysstatus->current.data[8]=8192*b0;
	sysstatus->set.data[ 9]=sysstatus->current.data[9]=8192*b1;
	sysstatus->set.data[10]=sysstatus->current.data[10]=8192*b2;
	sysstatus->set.data[11]=sysstatus->current.data[11]=8192*b3;
	sysstatus->set.data[12]=sysstatus->current.data[12]=8192*b4;
	sysstatus->set.data[13]=sysstatus->current.data[13]=8192*b5;
	sysstatus->set.data[14]=sysstatus->current.data[14]=8192*b6;
	sysstatus->set.data[15]=sysstatus->current.data[15]=8192*b7;
}

static FILE *opensound(FILE *close) {
	FILE *r;
	uint source=0;
	/* pactl list short sources */
	const char *sources[]={
		"pamon --format=u8 --channels=2 -d alsa_output.pci-0000_00_1b.0.analog-stereo.monitor",
		"pamon --format=u8 --channels=2 -d alsa_input.pci-0000_00_1b.0.analog-stereo",
		"pamon --format=u8 --channels=2 -d jack_in",
	};
	
	if (close) {pclose(close); return NULL;}
	
	if (sysstatus && sysstatus->set.source < lengthof(sources))
		source=sysstatus->set.source;
	r=popen(sources[source],"r");
	if (!r) perror("opensound");
	else SETCURRENT(source,source);
	return r;
}

static int sendwait(SOCKET s,u8 *buf,uint len) {
	int nowarn;
	nowarn=sock_write(s,buf,3*len);
	if (nowarn!=3*len) usleep(1);
	if (syssetcount!=sysstatus->set.count) return -1;
	usleep(syssleeptime);
	return len;
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

static int mergesendwait(SOCKET s,const pixelfloat *p,pixelfloat *backing,uint len,
	float bleed,float retain) {
	static u8 *buf=NULL;
	static uint buflen=0;
	float e,b;
	uint i,j;
	if (len>buflen) {
		size_t bs;
		bs=3*len*sizeof(*buf);
		buf=realloc(buf,bs);
		if (!buf) {buflen=0; return -1;}
		memset(buf,0,bs);
		buflen=len;
	}
	for(i=0;i<len;i++) {
		for(j=0;j<CHANNELS;j++) {
			e=p[i].chan[j];
			if (e>1.0) e=1.0;
			b=backing[i].chan[j];
			if (b>10.0) b=10.0;
			if (b<0.0) b=0.0;
			backing[i].chan[j] = (1.0-retain)*b + e;
			if (i>0)     backing[i-1].chan[j] += bleed*b;
			if (i<len-1) backing[i+1].chan[j] += bleed*b;
			
			buf[3*i+j]=translate(39.0*backing[i].chan[j]);
		}
	}
	return sendwait(s,buf,len);
}

static void patternsinus(SOCKET s,uint len) {
	double r,g,b;
	u8 *buffer;
	uint i;
	#define N(x) translate(39*sqrt((x+1.0)*0.5))
	
	buffer=malloc(3*len);

	gvset(0.55,0.62,0.74,0.1,0.11,0.13,0,0, 0,0,0,0,0,0,0,0);
	
	r=g=b=0.0;
	for(;;) {
		double nr=r,ng=g,nb=b;
		for(i=0;i<len;i++) {
			buffer[i*3+0]=N(sin(nr));
			buffer[i*3+1]=N(sin(ng));
			buffer[i*3+2]=N(sin(nb));
			nr+=gv2(0); ng+=gv2(1); nb+=gv2(2);
		}
		r+=gv2(3); g+=gv2(4); b+=gv2(5);
		if (sendwait(s,buffer,len)<0) break;
	}
	free(buffer);
}	

static u8 decay(u8 *src,uint step,uint i,uint max,float d,float f) {
	float ff,a=0.0;
	ff=d-2*f;
	if (i>0)     a+= f*src[step*(i-1)];
	             a+=ff*src[step*(i+0)];
	if (i<max-1) a+= f*src[step*(i+1)];
	if (a>255) a=255;
	return (u8)a;
}

static void patterncylon(SOCKET s,uint len) {
	u8 *src,*dst,*tmp;
	uint i,finish=0;
	#define setbuf(buffer,pos,r,g,b) do {\
		u8 *pp=&buffer[3*(pos)];\
		*pp++=r; *pp++=g; *pp=b; } while(0)
	#define setmaxbuf(buffer,pos,r,g,b) do {\
		u8 *pp=&buffer[3*(pos)];\
		*pp=MAX(r,*pp); pp++;\
		*pp=MAX(g,*pp); pp++;\
		*pp=MAX(b,*pp); pp++;\
		} while(0)

	src=malloc(3*len); memset(src,0,3*len);
	dst=malloc(3*len); memset(dst,0,3*len);

	gvset(0.2,0.8,0.4,0,0,0,0,0, 1.5,1.8,1.5,0,0,0,0,0);
	/* gvset(0.9,0.2,0.2,0,0,0,0,0, 1.8,1.8,1.2,0,0,0,0,0); */

	for(;;) {
		memset(dst,0,3*len);
		for(i=0;i<len;i++) {
			dst[3*i+0]=decay(src+0,3,i,len,0.5*gv2(8),0.25*gv2(0));
			dst[3*i+1]=decay(src+1,3,i,len,0.5*gv2(9),0.25*gv2(1));
			dst[3*i+2]=decay(src+2,3,i,len,0.5*gv2(10),0.25*gv2(2));
		}
		if (sendwait(s,dst,len)<0 && finish==0) finish=400;;
		tmp=src; src=dst; dst=tmp;
		
		if (finish>0) {
			if (--finish==0) break;
			continue;
		}

		do {
			static uint count=0;
			uint p;
			count++;
			p=count/4;
			if (p>=2*len) count=p=0;
			if (p>=len) p=2*len-p-1;
			setbuf(src,p,255,255,255);
		} while(0);
		
		if (rand()%80==0) {
			i=rand()%8;
			setmaxbuf(src,rand()%len,(i&1)?255:0,(i&2)?255:0,(i&4)?255:0);
		}
	}
	free(src);
	free(dst);
}	

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

int main(int argc,char *argv[]) {
	SOCKET s;
	int mode=-1;
	const char *progname;
	int currentmode;
	const char *e=NULL;
	const char *host="xmas.dfsmith.net";
	uint lights=100;
	
	progname=*(argv++); argc--;
	if (argc>0) {host=*(argv++); argc--;}
	if (argc>0) {mode=atoi(*(argv++)); argc--;}
	if (argc>0) {lights=atoi(*(argv++)); argc--;}

	if (!sysstatus) {
		int fd;
		mode_t um;
		sysstatus_t newstatus;
		const char *filename="/tmp/lightbar1";
		memset(&newstatus,0,sizeof(newstatus));
		um=umask(0);
		fd=open(filename,O_RDWR|O_CREAT,S_IRUSR|S_IWUSR|S_IROTH|S_IWOTH);
		umask(um);
		if (fd==-1) {perror(filename); return 1;}
		if (write(fd,&newstatus,sizeof(newstatus))!=sizeof(newstatus)) {
			perror("write"); return 1;
		}
		sysstatus=mmap(NULL,sizeof(*sysstatus),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
		if (sysstatus==MAP_FAILED) sysstatus=NULL;
		if (!sysstatus) {perror(progname); return 1;}
		close(fd);
		if (sysstatus->set.mode==0) sysstatus->set.mode=mode;
		SETCURRENT(mode,mode);
	}
	
	s=sock_getsocketto(host,8888,SOCK_DGRAM,&e);
	if (SOCKINV(s)) {
		printf("%s\n",e);
		return 1;
	}

	do {
		if (syssetcount!=sysstatus->set.count) {
			mode=sysstatus->set.mode;
			syssetcount=sysstatus->set.count;
		}
		
		SETCURRENT(mode,mode);
		syssleeptime=10000;
		currentmode=(mode==-1)?rand()%15:mode;
		printf("Starting mode %u (setmode %d)\n",currentmode,mode);
		switch(currentmode) {
		case 0:	patternsinus(s,lights); break;
		case 1:	patterncylon(s,lights); break;
		case 2:	patternwave(s,lights); break;
		case 3:	patternsparkles(s,lights); break;
		case 4:	patternsparticles(s,lights); break;
		/* case 5:	patternbees(s,lights); break; */
		case 7:	patterntest1(s,lights); mode=~0U; break;
		
		case 8:	patternvu(s,lights); break;
		case 9:	patternfft(s,lights); break;
		case 10: patternshoutpong(s,lights); break;
		case 15: mode=~0U; break;
		default: mode=0; break;
		}
	} while(mode!=~0U);

	sock_close(s);
	if (sysstatus) munmap(sysstatus,sizeof(*sysstatus));
	return 0;
}
