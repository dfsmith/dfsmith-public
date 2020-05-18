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

/* some Arduino.h defines cause trouble */
#undef B1
#undef B10
#undef B11

typedef struct {
	unsigned char c;	/* SCL pin */
	unsigned char d;	/* SDA pin */
	unsigned int half_clock;/* microseconds for half-clock period */
	unsigned int ppc:1;	/* push-pull SCL */
	unsigned int ppd:1;	/* push-pull SDA */
} i2c_arduino_io;

/* use with 5V tolerent I2C devices */
#define pplineh(p) digitalWrite(p,HIGH),pinMode(p,OUTPUT)
#define pplinel(p) digitalWrite(p,LOW),pinMode(p,OUTPUT)

/* use with voltage converters: never drives output HIGH with low impedance */
#define phlineh(p) pinMode(p,INPUT),digitalWrite(p,HIGH)
#define phlinel(p) digitalWrite(p,LOW),pinMode(p,OUTPUT)

/* read data line, set data line high or low */
#define dr(io)   (digitalRead((io)->d)==HIGH)
#define dh(io)   ((io)->ppd?pplineh((io)->d):phlineh((io)->d))
#define dl(io)   ((io)->ppd?pplinel((io)->d):phlinel((io)->d))
#define dw(io,x) ((x)?dh(io):dl(io))

/* read clk line, set clk high, set clk low */
#define clkr(io)  (digitalRead((io)->c)==HIGH)
#define clkh(io)  ((io)->ppc?pplineh((io)->c):phlineh((io)->c))
#define clkl(io)  ((io)->ppc?pplinel((io)->c):phlinel((io)->c))

/* set data direction to inb (read, pull high) or outb (write) */
#define clkout(io) pinMode((io)->c,OUTPUT)
#define clkin(io) pinMode((io)->c,INPUT)
#define ddin(io) do{pinMode((io)->d,INPUT);dh(io);}while(0)

/* wait a half-clock with data I/O */
#define w(IO) delayMicroseconds((IO)->half_clock)
#define wait_us(IO,US) delayMicroseconds(US)
#define wait_ms(IO,MS) delay(MS)

/* generalize misc and port open/close */
#define power(io,on) /* energize 5V line */
#define openport(io) 1
#define closeport(io)
#define portmatch(io1,io2) ((io1)->c==(io2)->c)
#define remainingports() 0

#endif
