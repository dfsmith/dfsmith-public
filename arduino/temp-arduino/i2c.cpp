#ifdef I2CDEBUG
#include "i2c-debug.h"
#define i2c_io i2c_debug_io
#else
#include "i2c-arduino.h"
#define i2c_io i2c_arduino_io
#endif
#include "i2c.h"

//#include "serialtrace.h"
//#define TR(x) serialtrace x
#define TR(x)

void i2c_deinit(const i2c_io *io) {
	closeport(io);
	power(io,0);
}

bool i2c_init(const i2c_io *io) {
	power(io,1);
	if (!openport(io)) return 0;
	ddin(io);
	return 1;
}

void i2c_stop(const i2c_io *io) {
	                    w(io);
	clkh(io);           w(io);
	          dw(io,1); w(io);
}

void i2c_restart(const i2c_io *io) {
	          dw(io,1); w(io);
	clkh(io);           w(io);
	          dw(io,0);
}

void i2c_start(const i2c_io *io) {
	clkh(io); dw(io,1); w(io);
	          dw(io,0); w(io);
	clkl(io);
}

bool i2c_send(const i2c_io *io,u8 b) {
	bool ack;
	int i;
u8 c=b;
	for(i=0;i<8;i++,b<<=1) {
		dw(io,b&128);
		w(io);
		clkh(io);
		w(io);
		clkl(io);
	}
	ddin(io);
	w(io);
	clkh(io);
	w(io);
	ack=dr(io);
	clkl(io);
TR(("send 0x%X ack=%d\n",c,ack));
	return ack;
}

u8 i2c_receive(const i2c_io *io,bool ack) {
	u8 b;
	int i;
	for(i=0;i<8;i++) {
		w(io);
		clkh(io);
		w(io);
		b = (b<<1) | dr(io);
		clkl(io);
	}
	w(io);
	dw(io,ack);
	clkh(io);
	w(io);
	clkl(io);
	ddin(io);
	return b;
}

static long int readN(const i2c_io *io,i2c_addr addr,u8 reg,int n) {
	long int x=-1;
	do {
		i2c_start(io);
		if (i2c_send(io,addr<<1 | 0)!=0) break;
		if (i2c_send(io,reg)!=0) break;
//		i2c_restart(io);
i2c_stop(io);
w(io);w(io); w(io);w(io);
i2c_start(io);
		if (i2c_send(io,addr<<1 | 1)!=0) break;
		for(x=0;n>0;n--)
			x=(x<<8) | i2c_receive(io,!(n>1));
	} while(0);
	i2c_stop(io);
	TR(("read reg=0x%X -> 0x%lX\n",reg,x));
	return x;
}

int i2c_read8(const i2c_io *io,i2c_addr addr,u8 reg) {
	return readN(io,addr,reg,1);
}

long int i2c_read16(const i2c_io *io,i2c_addr addr,u8 reg) {
	return readN(io,addr,reg,2);
}

long int i2c_read24(const i2c_io *io,i2c_addr addr,u8 reg) {
	return readN(io,addr,reg,3);
}

bool i2c_write8(const i2c_io *io,i2c_addr addr,u8 reg,u8 val) {
	bool r=0;
	do {
		i2c_start(io);
		if (i2c_send(io,addr<<1 | 0)!=0) break;
		if (i2c_send(io,reg)!=0) break;
		i2c_send(io,val);
		r=1;
	} while(0);
	i2c_stop(io);
	return r;
}
