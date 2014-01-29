#define _POSIX_C_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

typedef unsigned char u8;
typedef unsigned int uint;
const char *midifilename="/dev/snd/midiC1D0";

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
sysstatus_t *sysstatus=NULL;
#define UPDATE() (sysstatus->set.count++)
#define QSET(field,val) (sysstatus->set.field=val)
#define SET(field,val) do {QSET(field,val); UPDATE();} while(0)

#define iscmd(c) ((c)&0x80)
#define lengthof(x) (sizeof(x)/sizeof(*(x)))

static void addvalue(int index,int amount) {
	uint *v;
	v=&sysstatus->set.data[index];
	if (amount<0 && -amount>*v) {*v=0; return;}
	if (amount>0 && amount>=16484-*v) {*v=16383; return;}
	*v+=amount;
	/* UPDATE(); */
}

static void parsebutton(int chan,int button,int attack) {
	char row[8],press[8];
	int newmode=-1;
	int b=button%8;
	switch(button/8) {
		case 2: sprintf(row,"first"); newmode=b; break;
		case 3: sprintf(row,"second"); newmode=8+b; break;
		case 4: sprintf(row,"knob"); QSET(data[8+b],0); break;
		case 5: sprintf(row,"preset"); break;
		case 6: sprintf(row,"encA"); break;
		case 8: sprintf(row,"encB"); break;
		case 11: sprintf(row,"pad"); SET(source,b-3); break;
		case 13: sprintf(row,"slider"); break;
		default: sprintf(row,"%02d-",button/8); break;
	}
	switch(attack) {
		case 0x00: sprintf(press,"up"); break;
		case 0x7F: sprintf(press,"down"); break;
		default: sprintf(press,"0x%02X",attack); break;
	}
	printf("button %s%d %s\n",row,b,press);
	if (newmode>=0 && attack==0x7F) SET(mode,newmode);
}

static int setsliders(FILE *midi) {
	static uint syscurrentcount=0;
	uint i,val;
	u8 data[3*8];

	printf("knobs:  ");
	for(i=0;i<8;i++)
		printf(" %05d",sysstatus->current.data[8+i]);
	printf("\n");
		
	printf("sliders:");
	for(i=0;i<8;i++) {
		val=sysstatus->current.data[i];
		data[3*i+0]=0x80 | 0x60 | i;
		data[3*i+1]=val&0x7F;
		data[3*i+2]=(val>>7)&0x7F;
		printf(" %05d",val);
	}
	val=(syscurrentcount!=sysstatus->current.count);
	printf("%s\n",val?" write":"");
	if (val) {
		return write(fileno(midi),data,sizeof(data));
	}
	return 0;
}

#if 0
static int setpan(FILE *midi,int chan,int a1,int a2) {
	u8 data[3];
	data[0]=0x80 | 0x06 | (chan&7);
	data[1]=0x0A;
	data[2]=a2&0x40?0x10:0x70;
	return write(fileno(midi),data,sizeof(data));
}
#endif

static void parsecmd(FILE *midi,int cmd,int chan,u8 *data,uint len) {
	uint val,arg;
	switch(cmd) {
	case 1:
		parsebutton(chan,data[0],data[1]);
		break;
	case 3:
		val=data[0]&7;
		arg=data[1]&0x40;
		printf("knob%d -> %s\n",val,arg?"ccw":"cw");
		/* addvalue(val,arg?-128:+128); */
		addvalue(8+val,arg?-128:+128);
		break;
	case 6:
		val=data[1];
		val=val<<7 | data[0];
		printf("slider%d -> %d\n",chan,val);
		QSET(data[chan],val);
		/* if (chan>0) addvalue(chan-1,-100); */
		/* if (chan<7) addvalue(chan+1,-100); */
		break;
	default:
		printf("unknown cmd=%d chan=%d len=%d data:",cmd,chan,len);
		for(val=0;val<len;val++) printf(" 0x%02X",data[val]);
		printf("\n");
		break;
	}
	setsliders(midi);
}

static void parsemidi(FILE *midi) {
	int cmdclasslen[8]={0,0,0,0,0,0,0,0};
	u8 data[16];
	int c;
	int cmd,chan,count,len;;
	
	for(c=fgetc(midi);c!=EOF;c=fgetc(midi)) {
		if (!iscmd(c)) continue;
		
		cmd=(c>>4)&7;
		chan=c&15;
		
		len=cmdclasslen[cmd];
		c=fgetc(midi);
		for(count=0;c!=EOF;c=fgetc(midi)) {
			if (iscmd(c)) {ungetc(c,midi); break;}
			data[count++]=c;
			if (len!=0 && count>=len) break;
		}
		if (count > cmdclasslen[cmd]) cmdclasslen[cmd]=count;
		parsecmd(midi,cmd,chan,data,count);
	}
}

int main(void) {
	FILE *midi;

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
		if (!sysstatus) {perror("no shared memory"); return 1;}
		close(fd);
		if (!sysstatus) sysstatus=calloc(1,sizeof(*sysstatus));
		if (!sysstatus) {perror("no memory"); return 1;}
		SET(mode,0);
	}
	
	midi=fopen(midifilename,"r+b");
	if (!midi) {perror(midifilename); return 1;}
	
	while(!feof(midi)) {
		parsemidi(midi);
	}
	
	fclose(midi);
	return 0;
}
