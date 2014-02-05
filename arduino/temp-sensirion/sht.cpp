/* > sht-arduino.c */
/* (C) 2005, Daniel F. Smith <dfs1122@gmail.com> */
/*
 * This is free software covered by the Lesser GPL.
 * See COPYING or http://gnu.org/licenses/lgpl.txt for details.
 * If you link your binaries with this library you are obliged to provide
 * the source to this library and your modifications to this library.
 */

/* Controls the Sensirion SHT1x/SHT7x temperature / relative humidity probe */
/* See http://sensirion.com for details */

#include <stdlib.h>
#include <stdarg.h>
#ifndef TRACE
//#define TRACE(x) serialtrace x
#define TRACE(x)
#endif
#ifndef NULL
#define NULL ((void*)0)
#endif
#ifndef SCOPE
#define SCOPE(x,y) /* virtual  oscilloscope trace */
#endif

#include "i2c-arduino.h"
#define i2c_io i2c_arduino_io
#include "sht.h"
/* -- SHT functions --- */

struct shtport_s {
	const i2c_io *io;
	/* open */
	shtport *next;        /* used when we have a list of SHT devices */
	unsigned long int lastdata; /* the last data bits read from this SHT (not CRC) */
	unsigned int lastack; /* the last ack bits (0=good, shifted in from R) */
	unsigned int crc;     /* the last CRC calculation/syndrome (0=good) */
	unsigned int tmp;     /* temporary storage */
	double tempoffset;    /* linear value to add to temperature reading */
	double lasttemp;      /* last temp value read */
	double lastrh;        /* last RH value read */
	}; /* typedef is in header */

/* sht commands */
#define sht_cgettemp   0x03
#define sht_cgetrh     0x05
#define sht_cgetstatus 0x07
#define sht_csetstatus 0x06
#define sht_csetreset  0x1E

#define sht_statusval 0x00 /* what we expect the status register/flags to be */
#define sht_statuslowbatt 0x40 /* status flag reporting low battery */
#define sht_clocktimeus 50 /* half-clock cycle time in microseconds */
#define sht_readingclocksmax (320000/(sht_clocktimeus)) /* timeout for wait */

/* functions that might be exported sometime */

static void serialtrace(char *fmt,...) {
	char tmp[80];
	va_list args;
	va_start(args,fmt);
	vsnprintf(tmp,sizeof(tmp),fmt,args);
	va_end(args);
	Serial.print(tmp);
}

/* -- CRC8 and bit reverse -- */

typedef unsigned char u8;

static int crc8(int crc,int x) {
	static u8 *table=NULL;
	if (crc<0 || x<0) {
		TRACE(("crc8 cleanup\n"));
		if (table) free(table);
		table=NULL;
		return -1;
		}
	if (!table) {
		/* front-load calculation */
		const unsigned int gen=0x131;
		unsigned int i,j,r,s;
		table=(u8*)calloc(256,sizeof(*table));
		TRACE(("Creating crc8 table%s\n",table?"":" (fallback mode)"));
		if (!table) {i=crc&0xFF; r=x;} else {i=0; r=0;}
		while(i<256) {
			s=i<<1;
			for(j=0;j<8;j++) {
				r<<=1; if ((s^r)&0x100) r^=gen;
				s<<=1;
				}
			if (!table) return r;
			table[i++]=r;
			r=0;
			}
		if (!table) return -1; /* can't get here */
		}
	return table[(crc ^ x)&0xFF];
	}

static int rev8(int x) {
	static u8 *table=NULL;
	if (x<0) {
		TRACE(("rev8 cleanup\n"));
		if (table) free(table);
		table=NULL;
		return -1;
		}
	if (!table) {
		/* front-load calculation */
		int i,j,r,s;
		table=(u8*)calloc(256,sizeof(*table));
		TRACE(("Creating rev8 table%s\n",table?"":" (fallback mode)"));
		for(i=table?0:x;i<256;i++) {
			r=0; s=i;
			for(j=0;j<8;j++) {
				r<<=1; r|=s&1;
				s>>=1;
				}
			if (!table) return r;
			table[i]=r;
			}
		}
	return table[x&0xFF];
	}

/* conversions and constants */

static double sht_temp14offset(double voltage) {
	if      (voltage>=4.0) return -39.75 - 0.25*(voltage-4.0);
	else if (voltage>=3.5) return -39.66 - 0.18*(voltage-3.5);
	else if (voltage>=3.0) return -39.60 - 0.12*(voltage-3.0);
	else                   return -39.55 - 0.10*(voltage-2.5);
	}

static double sht_temp14(shtport *s) {
	/* convert 14-bit temperature value to celsius with (milli) voltage correction */
	const double d2=0.01;
	if (!s) return d2;
	if (s->lastack || s->crc) return sht_badtemp;
	return (s->lastdata & 0xFFFF)*d2 + s->tempoffset;
	}

static double sht_rh12(shtport *s,double temp) {
	/* convert 12-bit rh value to % rh with temperature correction */
	const double c1=-4.0,c2=0.0405,c3=-2.8e-6;
	const double t1=0.01,t2=0.00008;
	double srh,rh;
	if (!s) return c2;
	if (s->lastack || s->crc) return sht_badrh;
	srh=(s->lastdata & 0xFFFF);
	rh=c1 + c2*srh + c3*srh*srh;
	if (temp>=-40.0 && temp!=sht_badtemp) /* temp correction */
		rh+=(temp-25.0)*(t1 + t2*srh);
	return rh;
	}

/* -- low level I/O -- */

static void sht_out8(shtport *s,unsigned int x) {
	/* send 8 bits of x to every device on SHT list, lastack&1 will be 0 if acked */
	shtport *ss;
	unsigned int count;
	const i2c_io *io=s->io;
	TRACE(("out8 %02X\n",x));

	dw(io,1);
	w(io);
	clkl(io);
	/* data is good on rising edge of clk */
	for(count=0; count<8;count++,x<<=1) {
		for(ss=s; ss; ss=ss->next) dw(ss->io,x&128);
		w(io); clkh(io); w(io); clkl(io);
		}
	/* ack */
	ddin(io);
	w(io);
	for(ss=s; ss; ss=ss->next) {
		ss->crc=crc8(ss->crc,(x>>8)&0xFF);
		ss->lastack=(ss->lastack<<1) | dr(ss->io);
		TRACE(("ac=%d crc=0x%X x=%X\n",ss->lastack,ss->crc,x));
		}
	clkh(io); w(io); clkl(io);
	}

static void sht_in8(shtport *s,int last,int crc) {
	/* read 8 bits of data from SHT list, set last to prevent ack, crc indicates CRC input */
	int i;
	shtport *ss;
	const i2c_io *io=s->io;

	ddin(io);
	clkl(io);
	w(io);
	for(i=0;i<8;i++) {
		for(ss=s; ss; ss=ss->next) ss->tmp=(ss->tmp<<1) | dr(ss->io);
		clkh(io); w(io); clkl(io); w(io);
		}

	/* ack and CRC calculation */
	for(ss=s; ss; ss=ss->next) {
		dw(ss->io,last);
		ss->tmp&=0xFF;
		TRACE(("in8 data=0x%02X\n",ss->tmp));
		if (crc)
			ss->crc=crc8(ss->crc,rev8(ss->tmp));
		else {
			ss->crc=crc8(ss->crc,ss->tmp);
			ss->lastdata=(ss->lastdata<<8)|ss->tmp;
			}
		}
	w(io); clkh(io); w(io); clkl(io);
	ddin(io);
	w(io);
	}

static void sht_wait(shtport *s) {
	/* wait for all SHTs to finish reading */
	/* the lastack will be 0 if waiting succeeded */
	int t=0;
	int tracking=0;
	shtport *ss;
	const i2c_io *io=s->io;

	TRACE(("waiting..."));
	ddin(io);
	clkl(io);
	for(ss=s; ss; ss=ss->next) {
		ss->tmp=!(ss->lastack&1); /* 0 indicates finished/error */
		tracking+=ss->tmp;
		}
	for(t=0;t<sht_readingclocksmax;t++) {
		w(io);
		for(ss=s; ss; ss=ss->next) {
			if (!ss->tmp) continue;
			if (dr(ss->io)!=0) continue;
			ss->tmp=0;
			tracking--;
			}
		if (tracking<=0) break;
		}
	for(ss=s; ss; ss=ss->next)
		ss->lastack=(ss->lastack<<1) | ss->tmp;
	TRACE((" %d clocks%s\n",t,t>=sht_readingclocksmax?" (timeout) ":""));
	}

static int sht_start(shtport *s,int fast) {
	/* send reset communications and then start sequence to sht */
	const u8 startseq[]={ /* clk is b0, data is b1 */
		4,
		2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3, /* comm. reset */
		16, /* stop fast skip */
		4, /* ddout */
		2,3,1,0,1,3,2, /* start */
		8,128 /* finish sequence */};
	const u8 *q;
	shtport *ss;
	const i2c_io *io=s->io;
	int out=0;

	/* initialize SHT list */
	for(ss=s; ss; ss=ss->next) {
		if (ss->lastack) fast=0; /* errors prevalent---don't do fast start */
		ss->crc=rev8(sht_statusval&0x0F);
		ss->lastack=0;
		}

	TRACE(("start\n"));
	ddin(io);
	for(q=startseq;;q++) {
		if ((*q)&128) break;
		if ((*q)&16) {fast=0; continue;}
		if (fast) {continue;}
		if ((*q)&8) {ddin(io); out=0; continue;}
		if ((*q)&4) {out=1; continue;}

		/* set clk from b0 and data from b1 */
		for(ss=s; ss; ss=ss->next) {if (out) dw(ss->io,(*q)&2);}
		if ((*q)&1) clkh(io); else clkl(io);
		w(io);
		}
	return 0;
	}

static void sht_powercycle(shtport *s) {
	const i2c_io *io=s->io;
	power(io,0);
	longwait(io);
	power(io,1);
	longwait(io);
	}

static void sht_init(shtport *s) {
	const i2c_io *io=s->io;
	/* at this point the shtport is usable */
	clkout(io);
	clkl(io); /* clk should be low at power on */
	dw(io,0);
	power(io,1); /* ensure power on: raise 5V line */
	w(io);
	}

static void sht_deinit(shtport *s) {
	const i2c_io *io=s->io;
	ddin(io);
	clkin(io);
	}

static int sht_portmatch(shtport *s,shtport *ss) {
	return portmatch(s->io,ss->io);
	}

/* -- mid-level commands -- */

static void sht_cmdwrite(shtport *s,int cmd,...) {
	/* send commands and bytes to SHT list (with comm. reset) */
	va_list va;
	TRACE(("write cmd=%u\n",cmd));
	sht_start(s,0);  SCOPE(-1,-1);
	sht_out8(s,cmd); SCOPE(-1,-1);
	sht_wait(s);     SCOPE(-1,-1);
	va_start(va,cmd);
	while((cmd=va_arg(va,int))>=0) {
		sht_out8(s,cmd); SCOPE(-1,-1);
		}
	va_end(va);
	/* lastack&3 should be 0 */
	}	

static void sht_cmdread(shtport *s,int cmd,int n) {
	/* send a command and input a byte from SHT list */
	TRACE(("read1 cmd=0x%02X n=%d\n",cmd,n));
	sht_start(s,1);  SCOPE(-1,-1);
	sht_out8(s,cmd); SCOPE(-1,-1);
	sht_wait(s);     SCOPE(-1,-1);
	while(n-->0) {
		sht_in8(s,0,0); SCOPE(-1,-1);
		}
	sht_in8(s,1,1);   SCOPE(-1,-1); /* CRC */
	/* lastack&3 should be 0, crc should be 0 */
	}

/* -- exported functions -- */

void sht_close(shtport *s) {
	/* close down the sht data structures */
	if (s) {
		closeport(s);
		free(s);
		}

	/* clean up internal tables if nothing left */
	if (!remainingports()) {
		crc8(-1,-1);
		rev8(-1);
		}
	}

shtport *sht_open(const i2c_io *io,double voltage,int *errcode) {
	/* get a handle to sht device at given I/O address and bit location */
	/* parallel port must be statically available (global) */
	shtport *s=NULL;
	int errc=-1; /* error code */
	
	TRACE(("open\n"));
	do {
		/* make new sht object */
		s=(shtport*)calloc(1,sizeof(*s));
		if (!s) {errc=1; break;}
		s->io=io;
		s->next=NULL;
		s->tempoffset=sht_temp14offset(voltage);
		s->lasttemp=sht_badtemp;
		s->lastrh=sht_badrh;
		s->lastack=1; /* force communication reset */
		if (!openport(io)) {
			TRACE(("could not open port\n"));
			errc=2;
			break;
			}

		sht_init(s);

		for(;;) {
			/* check status */
			sht_cmdread(s,sht_cgetstatus,1);
			TRACE(("sht_open sht_getstatus ack=0x%X status=0x%02X crc=0x%02X\n",
				s->lastack,(s->lastdata&0xFF),s->crc));
			if (s->lastack) {errc=3; break;}
			if ((s->lastdata&0x0F)==(sht_statusval&0x0F)) {
				/* CRC only correct with expected status value */
				if (s->crc) {errc=4; break;}
				}
			if (s->lastdata & sht_statuslowbatt) {
				/* check low battery */
				if (errc>0) break;
				TRACE(("low battery\n"));
				sht_powercycle(s);
				errc=5;
				continue;
				}
			if ((s->lastdata&0xFF)!=sht_statusval) {
				/* status is unusual---reset device, set status and try again */
				if (errc>0) break;
				TRACE(("status strange: resetting\n"));
				sht_cmdwrite(s,sht_csetreset,-1);
				if (s->lastack) {errc=3; break;}
				sht_cmdwrite(s,sht_csetstatus,sht_statusval,-1);
				if (s->lastack) {errc=3; break;}
				errc=6;
				continue;
				}
			/* must be okay (recovered) if we got here */
			errc=0;
			break;
			}
		if (errc!=0) break;
		/* good exit */
		SCOPE(-1,-1);
		return s;
		} while(0);
	/* cannot find SHT: error */
	if (errcode) *errcode=errc;
	SCOPE(-1,-1);
	sht_deinit(s);
	sht_close(s);
	return NULL;
	}

int sht_readmany(shtport *s,...) {
	/* read temp and RH from many SHT devices with a common clock */
	int errs=0;
	shtport *ss;
	va_list va;
	
	if (!s) return -1;
	/* link shtport list */
	ss=s;
	va_start(va,s);
	do {
		ss->next=va_arg(va,shtport*);
		ss=ss->next;
		if (!ss) break;
		if (!sht_portmatch(s,ss)) {errs=-1; break;}
		} while(ss);
	va_end(va);
	if (errs) return errs;

	/* read temps */
	TRACE(("readmany: temp\n"));
	sht_cmdread(s,sht_cgettemp,2);
	for(ss=s; ss; ss=ss->next) {
		ss->lasttemp=sht_temp14(ss);
		if (ss->lasttemp==sht_badtemp) errs++;
		}

	/* read RHs */
	TRACE(("readmany: rh\n("));
	sht_cmdread(s,sht_cgetrh,2);
	for(ss=s; ss; ss=ss->next) {
		ss->lastrh=sht_rh12(ss,ss->lasttemp);
		if (ss->lastrh==sht_badrh) errs++;
		}
	return errs;
	}
	
void sht_getgranularity(shtport *s,double *temp,double *rh) {
	if (temp) *temp=sht_temp14(NULL);
	if (rh) *rh=sht_rh12(NULL,sht_badtemp);
	}
void sht_getlasttemprh(shtport *s,double *temp,double *rh) {
	if (!s) {if (temp) *temp=sht_badtemp; if (rh) *rh=sht_badrh;}
	if (temp) *temp=s->lasttemp;
	if (rh) *rh=s->lastrh;
	}
double sht_getlasttemp(shtport *s) {return s?s->lasttemp:sht_badtemp;}
double sht_gettemp(shtport *s) {
	if (!s) return sht_badtemp;
	s->next=NULL;
	sht_cmdread(s,sht_cgettemp,2);
	return s->lasttemp=sht_temp14(s);
	}
double sht_getlastrh(shtport *s) {return s?s->lastrh:sht_badtemp;}
double sht_getrh(shtport *s,double temp) {
	if (!s) return sht_badrh;
	s->next=NULL;
	sht_cmdread(s,sht_cgetrh,2);
	return s->lastrh=sht_rh12(s,temp);
	}
