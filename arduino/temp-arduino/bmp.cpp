/* > bmp.cpp */
/* BMP180/BMP280/BME280 temperature/pressure/humidity I2C chip handling. */

/* Test decoding by compiling:
$ gcc -c -Wall -DTESTCALCS -DI2CDEBUG i2c.cpp -o i2c-test.o
$ gcc -Wall -DTESTCALCS -DI2CDEBUG bmp.cpp i2c-test.o -lstdc++ -o test && ./test
test temp=15.00degC pressure=69965.00Pa
*/

/* Data sheets
https://cdn-shop.adafruit.com/datasheets/BST-BMP180-DS000-09.pdf
https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp280-ds001.pdf
https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme280-ds002.pdf
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

#if 0
#include "serialtrace.h"
#define TR(x) serialtrace x
#else
#define TR(x)
#endif

/* -- common -- */

typedef u8 bmp_reg; /* BMP register address bus is 8-bit */

static bool readu16(bmp_port *b,bmp_reg base,u16 *reg) {
	long int res=i2c_read16(b->io,b->addr,base); /* res is (*baseaddr<<8 | *(baseaddr+1)) */
	if (res==-1 || res==0 || res==0xFFFF) return false;
	*reg=res;
	return true;
}

static bool readu16le(bmp_port *b,bmp_reg base,u16 *reg) {
	u16 r;
	if (!readu16(b,base,&r)) return false;
	*reg=((r<<8) | ((r>>8)&0xFF)) & 0xFFFF;
	return true;
}

static s16 tosigned(u16 x) {
	if (x<0x8000) return x;
	x=0x8000 - (x & 0x7FFF);
	return 0-x;
}

static bool reads16(bmp_port *b,bmp_reg base,s16 *reg) {
	u16 r;
	if (!readu16(b,base,&r)) return false;
	*reg=tosigned(r);
	return true;
}

static bool reads16le(bmp_port *b,bmp_reg base,s16 *reg) {
	u16 r;
	if (!readu16le(b,base,&r)) return false;
	*reg=tosigned(r);
	return true;
}

static bool readu8(bmp_port *b,bmp_reg base,u8 *reg) {
	int res=i2c_read8(b->io,b->addr,base);
	if (res==-1) return false;
	*reg=res;
	return true;
}

static bool reads8(bmp_port *b,bmp_reg base,s8 *reg) {
	u8 r;
	if (!readu8(b,base,&r)) return false;
	if (r<0x80) *reg=r;
	else {
		r=0x80 - (r&0x7F);
		*reg=0-r;
	}
	return true;
}

/* -- BMP180 -- */

static bool bmp180_wait(bmp_port *b,uint max_ms) {
	int r;
	do {
		wait_ms(b->io,max_ms);
		r=i2c_read8(b->io,b->addr,0xF4);
		if (r<0) return false;
	} while((r & 0x20)!=0);
	return true;
}

bool bmp180_init(bmp_port *b) {
	bmp180_caldata *c=&b->cal180;
	TR(("BMP180 init\n"));
	do {
		c->OSS=3; /* full resolution */
		if (!reads16(b,0xAA,&c->AC1)) break;
		if (!reads16(b,0xAC,&c->AC2)) break;
		if (!reads16(b,0xAE,&c->AC3)) break;
		if (!readu16(b,0xB0,&c->AC4)) break;
		if (!readu16(b,0xB2,&c->AC5)) break;
		if (!readu16(b,0xB4,&c->AC6)) break;
		if (!reads16(b,0xB6,&c->B1)) break;
		if (!reads16(b,0xB8,&c->B2)) break;
		if (!reads16(b,0xBA,&c->MB)) break;
		if (!reads16(b,0xBC,&c->MC)) break;
		if (!reads16(b,0xBE,&c->MD)) break;
		TR(("MD=%d\n",c->MD));
		return true;
	} while(0);
	/* error exit */
	return false;
}

static s32 decodeB5(const bmp180_caldata *c) {
	s32 X1,X2,B5;
	X1=c->UT - c->AC6; X1*=c->AC5; X1/=1L<<15;
	X2=c->MC; X2*=1L<<11; X2/=(X1 + c->MD);
	//TR(("UT=%u x1=%ld x2=%ld MD=%d MC=%d %d/%d\n",
	//	UT,   X1,     X2,c->MD,c->MC,c->MC*1<<11,X1+c->MD));
	B5=X1+X2;
	return B5;
}


static s32 decode180temp(const bmp180_caldata *c) {
	/* mumbo-jumbo from the data sheet: done as written */
	/* returns temp in 0.1 degC units */
	s32 T;
	T=(decodeB5(c)+8)/(1<<4);
	return T;
}

static s32 decode180pressure(const bmp180_caldata *c) {
	/* mumbo-jumbo from the data sheet: done as written */
	/* returns pressure in 1 pascal units */
	/* Note that -ve rounding from integer division on x86_64 does
	 * not match data sheet.
	 */
	s32 X1,X2,X3;
	s32 B3;
	u32 B4;
	s32 B5,B6;
	u32 X4;
	u32 B7;
	s32 P;
	s32 UP=c->UP;
	
	B5=decodeB5(c);
	B6=B5 - 4000;
	TR(("UT=%u UP=%lu B5=%ld B6=%ld\n",c->UT,c->UP,B5,B6));
	
	//X1=(c->B2 * ((B6*B6)/(1<<12))) / (1<<11);
	X1=B6; X1*=B6; X1/=1L<<12; X1*=c->B2; X1/=1L<<11;
	//X2=(c->AC2 * B6) / (1<<11);
	X2=c->AC2; X2*=B6; X2/=1L<<11;
	X3=X1+X2;
	//B3=(((c->AC1*4+X3)<<c->OSS)+2)/4;
	B3=c->AC1; B3*=4; B3+=X3; B3<<=c->OSS; B3+=2; B3/=4;
	TR(("X1=%ld X2=%ld X3=%ld B3=%ld\n",X1,X2,X3,B3));

	//X1=(c->AC3*B6)/(1<<13);
	X1=c->AC3; X1*=B6; X1/=1L<<13;
	//X2=c->B1*((B6*B6)/(1<<12)) / (1<<16);
	X2=B6; X2*=B6; X2/=1L<<12; X2*=c->B1; X2/=1L<<16;
	X3=((X1+X2)+2) / (1<<2);
	X4=X3+32768;
	//B4=(c->AC4*X4) / (1<<15);
	B4=c->AC4; B4*=X4; B4/=1L<<15;
	TR(("B4=%lu AC4=%u X4=%ld\n",B4,c->AC4,X4));
	B7=UP; B7-=B3; B7*=(50000>>c->OSS);
	if (B7<0x80000000) {P=(B7*2)/B4;}
	else               {P=(B7/B4)*2;}
	TR(("X1=%ld X2=%ld X3=%ld B4=%lu B7=%lu P=%ld\n",X1,X2,X3,B4,B7,P));

	X1=(P / (1L<<8)) * (P / (1L<<8));
	X1=(X1*3038)/(1L<<16);
	X2=(-7357 * P) / (1L<<16);
	P+=(X1+X2+3791)/(1<<4);
	return P;
}

bool bmp180_getreading(bmp_port *b) {
	long int x;
	bmp180_caldata *c=&b->cal180;

	c->UT=0;
	c->UP=0;

	/* temperature */
	TR(("BMP180 getreading\n"));
	if (!i2c_write8(b->io,b->addr,0xF4,0x2E)) return false;
	bmp180_wait(b,5);
	x=i2c_read16(b->io,b->addr,0xF6);
	TR(("temp -> %ld\n",x));
	if (x<0) return 0;
	c->UT=x;
	
	/* pressure */
	if (!i2c_write8(b->io,b->addr,0xF4,0x34 | (b->cal180.OSS<<6))) return false;
	bmp180_wait(b,26);
	x=i2c_read24(b->io,b->addr,0xF6);
	TR(("pressure -> %ld\n",x));
	if (x<0) return 0;
	c->UP=x>>(8 - c->OSS);
	
	return true;
}

double bmp180_getlastpressure_pascal(bmp_port *b) {
	bmp180_caldata *c=&b->cal180;
	if (c->UT==0 || c->UP==0) return bmp_badpressure;
	return 1.0*decode180pressure(c);
}

double bmp180_getlasttemp_degc(bmp_port *b) {
	bmp180_caldata *c=&b->cal180;
	if (c->UT==0) return bmp_badtemperature;
	return 0.1*decode180temp(c);
}

/* -- BMP280 / BME280 -- */

static bool bmp280_wait(bmp_port *b,uint max_ms) {
	int r;
	do {
		wait_ms(b->io,max_ms);
		r=i2c_read8(b->io,b->addr,0xF3);
		if (r<0) return false;
	} while(r!=0);
	return true;
}

static bool bmp280_init(bmp_port *b) {
	bme280_caldata *c=&b->cal280;
	TR(("BMP280 init\n"));
	do {
		if (!i2c_write8(b->io,b->addr,0xF5,(0<<5)|(0<<2)|0)) break; /* config 5.4.6 */

		if (!readu16le(b,0x88,&c->T1)) break;
		if (!reads16le(b,0x8A,&c->T2)) break;
		if (!reads16le(b,0x8C,&c->T3)) break;
		if (!readu16le(b,0x8E,&c->P1)) break;
		if (!reads16le(b,0x90,&c->P2)) break;
		if (!reads16le(b,0x92,&c->P3)) break;
		if (!reads16le(b,0x94,&c->P4)) break;
		if (!reads16le(b,0x96,&c->P5)) break;
		if (!reads16le(b,0x98,&c->P6)) break;
		if (!reads16le(b,0x9A,&c->P7)) break;
		if (!reads16le(b,0x9C,&c->P8)) break;
		if (!reads16le(b,0x9E,&c->P9)) break;
		TR(("T[1-3]=%u %d %d\n",c->T1,c->T2,c->T3));
		TR(("P[1-9]=%u %d %d %d %d %d %d %d %d\n",
			c->P1,c->P2,c->P3,c->P4,c->P5,c->P6,c->P7,c->P8,c->P9));
		return true;
	} while(0);
	/* error exit */
	return false;
}

static bool bme280_init(bmp_port *b) {
	bme280_caldata *c=&b->cal280;
	u8 tmp1,tmp2;
	TR(("BME280 init\n"));
	do {
		if (!bmp280_init(b)) break;
		if (!readu8(b,0xA1,&c->H1)) break;
		if (!reads16le(b,0xE1,&c->H2)) break;
		if (!readu8(b,0xE3,&c->H3)) break;
		if (!readu8(b,0xE4,&tmp1)) {break;} c->H4=tmp1; c->H4<<=4;
		if (!readu8(b,0xE5,&tmp1)) {break;} c->H4|=tmp1&0x0F;
		if (!readu8(b,0xE6,&tmp2)) {break;} c->H5=tmp2; c->H5<<=4; c->H5|=tmp1>>4;
		if (!reads8(b,0xE7,&c->H6)) break;
		TR(("H[1-6]=%d %d %d %d %d %d\n",
			c->H1,c->H2,c->H3,c->H4,c->H5,c->H6));
		return true;
	} while(0);
	/* error exit */
	return false;
}

#define C(R) ((s32)c->R)
#define CC(R) ((s64)c->R)

static s32 t_fine(bme280_caldata *c) {
	/* from 4.2.3 */
	s32 var1=( (c->UT>>3) - (C(T1)<<1) ) * ((C(T2)))>>11;
	s32 x=(c->UT>>4) - C(T1);
	s32 var2=(((x * x)>>12) * C(T3))>>14;
	return var1+var2;
}

static s32 decode280temp(bme280_caldata *c) {
	return (t_fine(c)*5 + 128)>>8;
}

static double bme280_getlasttemp_degc(bmp_port *b) {
	bme280_caldata *c=&b->cal280;
	if (c->UT==0) return bmp_badtemperature;
	return decode280temp(c)*0.01;
}

static s64 decode280pressure(bme280_caldata *c) {
	/* from 4.2.3 */
	s64 var1=((s64)t_fine(c)) - 128000;
	s64 var2=var1*var1*CC(P6);
	var2+=(var1*CC(P5))<<17;
	var2+=(CC(P4)<<35);
	var1=((var1*var1*CC(P3))>>8) + ((var1*CC(P2))<<12);
	var1=((((s64)1)<<47)+var1)*CC(P1)>>33;
	if (var1==0) return bmp_badpressure;
	s64 p=1048576 - c->UP;
	p=(((p<<31)-var2)*3125)/var1;
	var1=(CC(P9) * (p>>13) * (p>>13)) >> 25;
	var2=(CC(P8) * p) >> 19;
	p=((p+var1+var2)>>8) + (CC(P7)<<4);
	return p;
}
static double bme280_getlastpressure_pascal(bmp_port *b) {
	bme280_caldata *c=&b->cal280;
	if (c->UT==0 || c->UP==0) return bmp_badpressure;
	return decode280pressure(c)/256.0;
}

static s32 decode280humidity(bme280_caldata *c) {
	/* from 4.2.3 */
	s32 x=t_fine(c)-((s32)76800);
	x=(( 
		(((C(UH)<<14) - (C(H4)<<20) - C(H5)*x))
		+ ((s32)16384))>>15
	) * (
		(((((x*C(H6))>>10) * (((x*C(H3)>>11)
		+ ((s32)32768)))>>10)
	 	+ ((s32)2097152))*C(H2) + 8192)>>14
	);
	x-=(( ((x>>15)*(x>>15))>>7 ) * C(H1))>>4;
	x=(x<0)?0:x;
	x=(x>419430400)?419430400:x;
	return x>>12;
}
static double bme280_getlasthumidity_rel(bmp_port *b) {
	bme280_caldata *c=&b->cal280;
	if (c->UT==0 || c->UP==0) return bmp_badhumidity;
	return decode280humidity(c)/1024.0;
}

static bool bmp280_getreading(bmp_port *b) {
	u8 buf[6];
	const int oss=5;
	bme280_caldata *c=&b->cal280;

	c->UP=0;
	c->UT=0;
	c->UH=0;

	/* request */
	i2c_write8(b->io,b->addr,0xF4,(oss<<5) | (oss<<2) | 1); /* ctrl_meas 4.3.4 */

	/* store */
	bmp280_wait(b,352); /* data sheet does not provide formula */
	if (!i2c_read(b->io,b->addr,0xF7,buf,sizeof(buf))) return false;
	c->UP=buf[0]; c->UP<<=8; c->UP|=buf[1]; c->UP<<=4; c->UP|=buf[2]>>4;
	c->UT=buf[3]; c->UT<<=8; c->UT|=buf[4]; c->UT<<=4; c->UT|=buf[5]>>4;
	TR(("UP=0x%lX UT=0x%lX\n",c->UP,c->UT));
	return true;
}

bool bme280_getreading(bmp_port *b) {
	u8 buf[8];
	const int oss=5;
	bme280_caldata *c=&b->cal280;

	c->UP=0;
	c->UT=0;
	c->UH=0;

	/* request */
	i2c_write8(b->io,b->addr,0xF2,oss); /* ctrl_hum 5.4.3 oversample x 16 */
	i2c_write8(b->io,b->addr,0xF4,(oss<<5) | (oss<<2) | 1); /* ctrl_meas 5.4.4 */

	/* store */
	bmp280_wait(b,114);
	if (!i2c_read(b->io,b->addr,0xF7,buf,sizeof(buf))) return false;
	c->UP=buf[0]; c->UP<<=8; c->UP|=buf[1]; c->UP<<=4; c->UP|=buf[2]>>4;
	c->UT=buf[3]; c->UT<<=8; c->UT|=buf[4]; c->UT<<=4; c->UT|=buf[5]>>4;
	c->UH=buf[6]; c->UH<<=8; c->UH|=buf[7];
	TR(("UP=0x%lX UT=0x%lX UH=0x%X\n",c->UP,c->UT,c->UH));
	return true;
}

/* -- generic BMP -- */

static double bmp_nohumidity(bmp_port *b) {
	return bmp_badhumidity;
}

void bmp_delete(bmp_port *b) {
	if (!b) return;
	i2c_deinit(b->io);
	free(b);
}

bmp_port *bmp_new(const i2c_io *io,u8 offset) {
	bmp_port *b;
	u8 id;
	u8 addr=0x76+offset;

	static const struct bmp_methods_s bmp180={
		.startreading=bmp180_getreading,
		.getlasttemp_degc=bmp180_getlasttemp_degc,
		.getlastpressure_pascal=bmp180_getlastpressure_pascal,
		.getlasthumidity_rel=bmp_nohumidity,
	};
	static const struct bmp_methods_s bmp280={
		.startreading=bmp280_getreading,
		.getlasttemp_degc=bme280_getlasttemp_degc,
		.getlastpressure_pascal=bme280_getlastpressure_pascal,
		.getlasthumidity_rel=bmp_nohumidity,
	};
	static const struct bmp_methods_s bme280={
		.startreading=bme280_getreading,
		.getlasttemp_degc=bme280_getlasttemp_degc,
		.getlastpressure_pascal=bme280_getlastpressure_pascal,
		.getlasthumidity_rel=bme280_getlasthumidity_rel,
	};

	TR(("init\n"));
	if (!i2c_init(io)) return NULL;
	/* send reset */
	TR(("reset\n"));
	if (!i2c_write8(io,addr,0xE0,0xB6)) return NULL;

	b=(bmp_port*)calloc(1,sizeof(*b));
	if (!b) return NULL;
	b->io=io;
	b->addr=addr;
	b->m=NULL;

	/* chip id */
	TR(("id\n"));
	id=i2c_read8(b->io,b->addr,0xD0);
	switch(id) {
	case 0x55:
		if (!bmp180_init(b)) break;
		b->m=&bmp180;
		break;
	case 0x56:
	case 0x57:
	case 0x58:
		if (!bmp280_init(b)) break;
		b->m=&bmp280;
		break;
	case 0x60:
		if (!bme280_init(b)) break;
		b->m=&bme280;
		break;
	}
	if (!b->m) {
		bmp_delete(b);
		return NULL;
	}
	return b;
}

#ifdef TESTCALCS
#include <stdio.h>

int main(void) {
	i2c_debug_state state;
	const i2c_debug_io dio={stdout,&state};
	bmp_port *b;

	/* data sheet test table: calibration values */

	printf("BMP180 decode logic test\n");
	bmp180_caldata ds1={
		0,
		408,-72,-14383,32741,32757,23153,
		6190,4,
		-32768,-8711,2868,
		27898,23843,
	};
	printf("test:       temp=%.2fdegC pressure=%.2fPa\n",
		0.1*decode180temp(&ds1),
		1.0*decode180pressure(&ds1));
	printf("data sheet: temp=%.2fdegC pressure=%.2fPa\n",
		0.1*150,
		1.0*69964);

	printf("BMP280 decode logic test\n");
	bme280_caldata ds2={
		27504,26435,-1000,
		36477,-10685,3024,2855,140,-7,15500,-14600,6000,
		0,0,0,0,0,0,
		0,519888,415148,
	};
	printf("test:       temp=%.2fdegC pressure=%.2fPa\n",
		0.01*decode280temp(&ds2),
		(1/256.0)*decode280pressure(&ds2));
	printf("data sheet: temp=%.2fdegC pressure=%.2fPa\n",
		0.01*2508,
		(1/256.0)*25767236);

	b=bmp_new(&dio,0);
	bmp_delete(b);
	return 0;
}
#endif
