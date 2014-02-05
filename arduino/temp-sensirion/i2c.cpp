#include "i2c-arduino.h"
#define i2c_io i2c_arduino_io
#include "i2c.h"

void i2c_deinit(const i2c_io *io) {
	closeport(io);
	power(io,0);
}

bool i2c_init(const i2c_io *io,int hc_us) {
	power(io,1);
	if (!openport(io)) return 0;
	ddin(io);
	return 1;
}

void i2c_start(const i2c_io *io) {
	clkh(io);
	dw(io,1);
	w(io);
	dw(io,0);
	w(io);
}

void i2c_restart(const i2c_io *io) {
	clkh(io);
	dw(io,1);
	w(io);
	clkl(io);
	w(io);
	dw(io,0);
}

void i2c_stop(const i2c_io *io) {
	clkh(io);
	w(io);
	dw(io,1);
}

bool i2c_send(const i2c_io *io,u8 b) {
	bool ack;
	int i;
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
	return ack;
}

u8 i2c_receive(const i2c_io *io,bool ack) {
	u8 b;
	int i;
	for(i=0;i<8;i++,b<<=1) {
		w(io);
		clkh(io);
		w(io);
		b|=dr(io);
		clkl(io);
	}
	w(io);
	dw(io,ack);
	clkh(io);
	w(io);
	clkl(io);
	dw(io,0);
}

int i2c_read8(const i2c_io *io,i2c_addr addr) {
	int x;
	i2c_start(io);
	if (!i2c_send(io,addr<<1 | 1)) return -1;
	x=i2c_receive(io,0);
	i2c_stop(io);
	return x;
}

long int i2c_read16(const i2c_io *io,i2c_addr addr) {
	int x;
	i2c_start(io);
	if (!i2c_send(io,addr<<1 | 1)) return -1;
	x=         i2c_receive(io,1);
	x=(x<<8) | i2c_receive(io,0);
	i2c_stop(io);
	return x;
}

long int i2c_read24(const i2c_io *io,i2c_addr addr) {
	u32 x;
	i2c_start(io);
	if (!i2c_send(io,addr<<1 | 1)) return -1;
	x=         i2c_receive(io,1);
	x=(x<<8) | i2c_receive(io,1);
	x=(x<<8) | i2c_receive(io,0);
	i2c_stop(io);
	return x;
}

bool i2c_write8(const i2c_io *io,i2c_addr addr,u8 val) {
	i2c_start(io);
	if (!i2c_send(io,addr<<1 | 0)) return 0;
	return i2c_send(io,val);
}
