/* > i2c-arduino.h */
/* (C) 2005, 2013, 2014 Daniel F. Smith <dfs1122@gmail.com> */
/*
 * This is free software covered by the Lesser GPL.
 * See COPYING or http://gnu.org/licenses/lgpl.txt for details.
 * If you link your binaries with this library you are obliged to provide
 * the source to this library and your modifications to this library.
 */

#ifndef i2c_arduino_h
#define i2c_arduino_h

/* interface level control */
#include <Arduino.h>

typedef struct {
	unsigned char c;	/* SCL pin */
	unsigned char d;	/* SDA pin */
	unsigned int half_clock;/* microseconds for half-clock period */
} i2c_arduino_io;

#if PULL_PUSH /* pull low, push high */
	/* use with 5V tolerent I2C devices */
	#define lineh(p) digitalWrite(p,HIGH),pinMode(p,OUTPUT)
	#define linel(p) digitalWrite(p,LOW),pinMode(p,OUTPUT)
	#define ddin(io) do{pinMode((io)->d,INPUT);lineh((io)->d);}while(0)
#else /* pull low, float high */
	/* use with voltage converters: never drives output HIGH with low impedance */
	#define lineh(p) pinMode(p,INPUT),digitalWrite(p,HIGH)
	#define linel(p) digitalWrite(p,LOW),pinMode(p,OUTPUT)
	#define ddin(io) lineh((io)->d)
#endif

/* read clk line, set clk high, set clk low */
#define clkr(io)  (digitalRead((io)->c)==HIGH)
#define clkh(io)  lineh((io)->c)
#define clkl(io)  linel((io)->c)

/* read data line, set data line high or low */
#define dr(io)    (digitalRead((io)->d)==HIGH)
#define dw(io,x)  ((x)?lineh((io)->d):linel((io)->d))

/* set data direction to inb (read, pull high) or outb (write) */
#define clkout(io) pinMode((io)->c,OUTPUT)
#define clkin(io) pinMode((io)->c,INPUT)

/* wait a half-clock with data I/O */
#define w(io) delayMicroseconds((io)->half_clock)
#define longwait(io) delay(100*(io)->half_clock)

/* generalize misc and port open/close */
#define power(io,on) /* energize 5V line */
#define openport(io) 1
#define closeport(io)
#define portmatch(io1,io2) ((io1)->c==(io2)->c)
#define remainingports() 0

#endif
