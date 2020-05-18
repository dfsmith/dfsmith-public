#ifdef I2CDEBUG
#include "i2c-debug.h"
#define i2c_io i2c_debug_io
#else
#include "i2c-arduino.h"
#define i2c_io i2c_arduino_io
#endif
#include "i2c.h"

#define TRACING 0
#if TRACING
#include "serialtrace.h"
#define TR(x) serialtrace x
#else
#define TR(x)
#endif

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
	#if TRACING
	u8 c=b;
	#endif
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
	TR(("receive (ack=%d) 0x%X\n",ack,b));
	return b;
}

bool i2c_read(const i2c_io *io,i2c_addr addr,u8 reg,u8 *buf,size_t n) {
	bool rc=false;
	do {
		i2c_start(io);
		if (i2c_send(io,addr<<1 | 0)!=0) break;
		if (i2c_send(io,reg)!=0) break;
		//i2c_restart(io);
		i2c_stop(io);
		w(io);w(io); w(io);w(io);
		i2c_start(io);
		if (i2c_send(io,addr<<1 | 1)!=0) break;
		for(;n>0;n--)
			*(buf++)=i2c_receive(io,!(n>1));
		rc=true;
	} while(0);
	i2c_stop(io);
	TR(("read(%d)->%d\n",(int)n,rc));
	return rc;
}

int i2c_read8(const i2c_io *io,i2c_addr addr,u8 reg) {
	u8 buf[1];
	if (!i2c_read(io,addr,reg,buf,sizeof(buf))) return -1;
	return buf[0];
}

long int i2c_read16(const i2c_io *io,i2c_addr addr,u8 reg) {
	u8 buf[2];
	long int r;
	if (!i2c_read(io,addr,reg,buf,sizeof(buf))) return -1;
	       r =buf[0];
	r<<=8; r|=buf[1];
	return r;
}

long int i2c_read24(const i2c_io *io,i2c_addr addr,u8 reg) {
	u8 buf[3];
	long int r;
	if (!i2c_read(io,addr,reg,buf,sizeof(buf))) return -1;
	       r =buf[0];
	r<<=8; r|=buf[1];
	r<<=8; r|=buf[2];
	return r;
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
