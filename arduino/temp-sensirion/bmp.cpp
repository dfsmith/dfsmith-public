/* > bmp.cpp */
/* BM180 temperature/pressure I2C chip handling. */

/* Test decoding by compiling:
$ gcc -c -Wall -DTESTCALCS -DI2CDEBUG i2c.cpp -o i2c-test.o
$ gcc -Wall -DTESTCALCS -DI2CDEBUG bmp.cpp i2c-test.o -lstdc++ -o test && ./test
test temp=15.00degC pressure=69965.00Pa
*/


#ifndef TESTCALCS
#include "i2c-arduino.h"
#define i2c_io i2c_arduino_io
#else
#include "i2c-debug.h"
#define i2c_io i2c_debug_io
#endif
#include <stdlib.h>
#include "bmp.h"

typedef union {
	s16 s;
	u16 u;
} su16;

typedef u8 bmp_reg;

static s16 i2ctos16(u16 u) {
	/* BMP180 I2C two's complement to host signed (u==MSB<<8 | LSB) */
	if (u<0x8000) return u;
	u=0x8000 - (u&0x7FFF);
	return 0-u;
}

static u16 i2ctou16(u16 u) {
	return u;
}

static bool readreg(bmp_port *b,bmp_reg base,su16 *reg,bool sgned) {
	long int res;
	res=i2c_read16(b->io,b->addr,base); /* res is (baseaddr<<8 | (base+1)addr) */
	if (res==-1 || res==0 || res==0xFFFF) return 0;
	if (sgned) reg->s=i2ctos16(res);
	else       reg->u=i2ctou16(res);
	return 1;
}

static bool bmpready(bmp_port *b) {
	if ((i2c_read8(b->io,b->addr,0xF4) & 0x32)==0) return 1;
	return 0;
}

static void bmpwait(bmp_port *b,uint max_us) {
	do {
		wait_us(b->io,max_us);
	} while(!bmpready(b));
}

void bmp_delete(bmp_port *b) {
	if (b) i2c_deinit(b->io);
	free(b);
}

bmp_port *bmp_new(const i2c_io *io) {
	bmp_port *b;
	su16 reg;
	do {
		b=(bmp_port*)malloc(sizeof(*b));
		if (!b) break;
		b->io=io;
		b->addr=0x77;
printf("init\n");
		if (!i2c_init(io)) break;

		/* send reset */
printf("reset\n");
		if (!i2c_write8(b->io,b->addr,0xE0,0xB6)) break;
		
		/* chip id */
printf("id\n");
		if (i2c_read8(b->io,b->addr,0xD0)!=0x55) break;

		if (!readreg(b,0xAA,&reg,1)) break; b->cal.AC1=reg.s;
		if (!readreg(b,0xAC,&reg,1)) break; b->cal.AC2=reg.s;
		if (!readreg(b,0xAE,&reg,1)) break; b->cal.AC3=reg.s;
		if (!readreg(b,0xB0,&reg,0)) break; b->cal.AC4=reg.u;
		if (!readreg(b,0xB2,&reg,0)) break; b->cal.AC5=reg.u;
		if (!readreg(b,0xB4,&reg,0)) break; b->cal.AC6=reg.u;

		if (!readreg(b,0xB6,&reg,1)) break; b->cal.B1=reg.s;
		if (!readreg(b,0xB8,&reg,1)) break; b->cal.B2=reg.s;

		if (!readreg(b,0xBA,&reg,1)) break; b->cal.MB=reg.s;
		if (!readreg(b,0xBC,&reg,1)) break; b->cal.MC=reg.s;
		if (!readreg(b,0xBE,&reg,1)) break; b->cal.MD=reg.s;
		
		b->cal.OSS=3; /* full resolution */
		return b;
	} while(0);
	/* error exit */
	bmp_delete(b);
	return NULL;
}

static s32 decodeB5(bmp_caldata *c,u16 UT) {
	s32 X1,X2,B5;
	X1=(UT - c->AC6) * c->AC5/(1<<15);
	X2=(c->MC * 1<<11) / (X1 + c->MD);
//printf("x1 %d x2 %d MD %d MC %d %d/%d\n",X1,X2,c->MD,c->MC,
//	c->MC*1<<11,X1+c->MD);
	B5=X1+X2;
	return B5;
}


static s32 decodetemp(bmp_caldata *c,u16 UT) {
	/* mumbo-jumbo from the data sheet: done as written */
	/* returns temp in 0.1 degC units */
	s32 B5;
	s32 T;

	B5=decodeB5(c,UT);
	T=(B5+8)/(1<<4);
	return T;
}

static s32 decodepressure(bmp_caldata *c,u16 UT,u32 UP) {
	/* mumbo-jumbo from the data sheet: done as written */
	/* returns pressure in 1 pascal units */
	/* Note that -ve rounding from integer division on x86_64 does
	 * not match data sheet.
	 */
	s32 X1,X2,X3;
	s32 B3,B4,B5,B6;
	u32 X4;
	u32 B7;
	s32 P;
	
	B5=decodeB5(c,UT);
	B6=B5 - 4000;
	X1=(c->B2 * ((B6*B6)/(1<<12))) / (1<<11);
	X2=(c->AC2 * B6) / (1<<11);
	X3=X1+X2;
	B3=(((c->AC1*4+X3)<<c->OSS)+2)/4;
	X1=(c->AC3*B6)/(1<<13);
	X2=c->B1*((B6*B6)/(1<<12)) / (1<<16);
	X3=((X1+X2)+2) / (1<<2);
	X4=X3+32768;
	B4=(c->AC4*X4) / (1<<15);
	B7=(UP-B3);
	B7=B7 * (50000>>c->OSS);
	if (B7<0x80000000) {P=(B7*2)/B4;}
	else               {P=(B7/B4)*2;}
	X1=(P / (1<<8)) * (P / (1<<8));
	X1=(X1*3038)/(1<<16);
	X2=(-7357 * P) / (1<<16);
	P+=(X1+X2+3791)/(1<<4);
	return P;
}

bool bmp_getreading(bmp_port *b) {
	long int x;

	b->UT=0;
	b->UP=0;

	/* temperature */
	if (!i2c_write8(b->io,b->addr,0xF4,0x2E)) return 0;
	bmpwait(b,4500);
	x=i2c_read16(b->io,b->addr,0xF6);
	if (x<0) return 0;
	b->UT=x;
	
	/* pressure */
	if (!i2c_write8(b->io,b->addr,0xF4,0x34 + (b->cal.OSS<<6))) return 0;
	bmpwait(b,25500);
	x=i2c_read24(b->io,b->addr,0xF6);
	if (x<0) return 0;
	b->UP=x>>(8-b->cal.OSS);
	
	return 1;
}

double bmp_getlastpressure_pascal(bmp_port *b) {
	if (b->UT==0 || b->UP==0) return bmp_badpressure;
	return 1.0*decodepressure(&b->cal,b->UT,b->UP);
}

double bmp_getlasttemp_degc(bmp_port *b) {
	if (b->UT==0) return bmp_badtemperature;
	return 0.1*decodetemp(&b->cal,b->UT);
}

#ifdef TESTCALCS
#include <stdio.h>

int main(void) {
	/* data sheet test table: calibration values */
	bmp_caldata ds={
		408,-72,-14383,32741,32757,23153,
		6190,4,
		-32768,-8711,2868,
		0 /* OSS */
	};
	printf("test:       temp=%.2fdegC pressure=%.2fPa\n",
		0.1*decodetemp(&ds,27898),
		1.0*decodepressure(&ds,27898,23843));
	printf("data sheet: temp=%.2fdegC pressure=%.2fPa\n",
		0.1*150,
		1.0*69964);
		
	i2c_debug_state state;
	const i2c_debug_io dio={stdout,&state};
	bmp_port *b;
	b=bmp_new(&dio);
	bmp_delete(b);
	return 0;
}
#endif
