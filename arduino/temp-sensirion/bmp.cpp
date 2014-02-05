/* > bmp.cpp */
#include "bmp.h"

typedef union {
	s16 s;
	u16 u;
} su16;

#define OSS 3 /* 8x oversampling */

static s16 i2ctos16(u16 u) {
	/* BMP180 I2C two's complement to host signed (u==MSB<<8 | LSB) */
	if (u<0x8000) return u;
	u=0x8000 - (u&0x7FFF);
	return 0-u;
}

static u16 i2ctou16(u16 u) {
	return u;
}

static bool readreg(bmp_port *b,bmp_reg base,su16 *reg,bool signed) {
	u16 res;
	res=i2c_read16(b->io,base); /* res is (baseaddr<<8 | (base+1)addr) */
	if (res==0U || res==0xFFFFU) return 0;
	if (signed) reg->s=i2ctos16(res);
	else        reg->u=i2ctou16(res);
}

static bool bmpready(bmp_port *b) {
	if ((i2c_read8(b->io,0xF4) & 0x32)==0) return 1;
	return 0;
}

static void bmpwait(bmp_port *b,uint max_us_notused) {
	do {
		longwait(b->io);
	} while(!bmpready(b);
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
		if (!i2c_init(io)) break;
		
		/* send reset */
		if (!i2c_write8(b->io,0xE0,0xB6)) break;
		
		/* chip id */
		if (i2c_read8(b->io,0xD0)!=0x55) break;

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
		
		return b;
	} while(0);
	/* error exit */
	bmp_delete(b);
	return NULL;
}

static s32 decodeB5(bmp_calibration *c,u16 UT) {
	s32 X1,X2,B5;
	X1=(UT - c->AC6) * c->AC5/(1<<15);
	X2=(c->MC * 1<<11) / (X1 + c->MD));
	B5=X1+X2;
	return B5;
}


static s32 decodetemp(bmp_calibration *c,u16 UT) {
	/* mumbo-jumbo from the data sheet: done as written */
	/* returns temp in 0.1 degC units */
	s32 B5;
	s32 T;

	B5=decodeB5(c,UT);
	T=(B5+8)/(1<<4);
	return T;
}

static s32 decodepressure(bmp_calibration *c,u16 UT,u32 UP) {
	/* mumbo-jumbo from the data sheet: done as written */
	/* returns pressure in 1 pascal units */
	s32 X1,X2,X3;
	s32 B3,B4,B5;
	u32 X4;
	u32 B7;
	s32 P;
	
	B5=decodeB5(c,UT);
	B6=B5 - 4000;
	X1=(B2 * ((B6*B6)/(1<<12))) / (1<<11);
	X2=(c->AC2 * B6) / (1<<11);
	X3=X1+X2;
	B3=(((c->AC1*4+X3)<<OSS)+2)/4;
	X1=(c->AC3*B6)/(1<<13)
	X2=(c->B1*((B6*B6)/(1<<12)) / (1<<16);
	X3=((X1+X2)+2) / (1<<2);
	X4=X3+32768;
	B4=(AC4*X4) / (1<<15);
	B7=(UP-B3);
	B7=B7 * (50000>>OSS);
	if (B7<0x80000000) {P=(B7*2)/B4;}
	else               {P=(B7/B4)*2;}
	X1=(P / (1<<8)) * (P / (1<<8));
	X1=(X1*3038)/(1<<16);
	X2=(-7357 * P) / (1<<16);
	P+=(X1+X2+3791)/(1<<4)
	return P;
}

bool bmp_getreading(bmp_port *b) {
	long int x;

	b->UT=0;
	b->UP=0;

	/* temperature */
	if (!i2c_write(b->io,0xF4,0x2E)) return 0;
	bmpwait(b,4500);
	x=i2c_read16(b->io,0xF6);
	if (x<0) return 0;
	b->UT=x;
	
	/* pressure */
	if (!i2c_write(b->io,0xF4,0x34 + (OSS<<6))) return 0;
	bmpwait(b,25500);
	x=i2c_read24(b->io,0xF6);
	if (x<0) return 0;
	b->UP=x>>(8-OSS);
	
	return 1;
}

double bmp_getlastpressure_pascal(bmp_port *b) {
	if (b->UT==0 || b->UP==0) return bmp_badpressure;
	return 1.0*decodepressure(b->cal,b->UT,b->UP);
}

double bmp_getlasttemp_degc(bmp_port *b) {
	if (b->UT==0) return bmp_badtemperature;
	return 0.1*decodetemp(b->cal,b->UT);
}