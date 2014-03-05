/* > i2c-debug.h */
/* (C) 2005, 2013, 2014 Daniel F. Smith <dfs1122@gmail.com> */
/*
 * This is free software covered by the Lesser GPL.
 * See COPYING or http://gnu.org/licenses/lgpl.txt for details.
 * If you link your binaries with this library you are obliged to provide
 * the source to this library and your modifications to this library.
 */

#ifndef i2c_debug_h
#define i2c_debug_h

//#error "I2C debug code: Are you sure?"

#include <stdio.h>

typedef struct {
	int p;
	char sda[100];
	char scl[100];
	bool d,c;
} i2c_debug_state;

typedef struct {
	FILE *out;
	i2c_debug_state *state;
} i2c_debug_io;
#define i2c_debug_initstate(IO) ( \
	(IO)->state->p=0, \
	(IO)->state->c=1, \
	(IO)->state->d=1, \
	(IO)->state->sda[0]='^', \
	(IO)->state->scl[0]='^' \
	)

#define HI(last) (((last)=='/'  || (last)=='^')? '^':'/')
#define LO(last) (((last)=='\\' || (last)=='_')? '_':'\\')

#define ddin(io)

/* read clk line, set clk high, set clk low */
#define clkr(io) 1
#define clkh(io) ((io)->state->c=1)
#define clkl(io) ((io)->state->c=0)

/* read data line, set data line high or low */
#define dr(io)   1
#define dw(io,x) ((io)->state->d=(x))

/* set data direction to inb (read, pull high) or outb (write) */
#define clkout(io)
#define clkin(io)

/* wait a half-clock with data I/O */
#define w(IO) do {i2c_debug_state *s=(IO)->state; char *l; uint p=s->p++; bool stop=0;\
	if (p+2>=sizeof(s->sda)) p--; \
	l=&s->sda[p]; \
	l[1]=(s->d)?HI(*l):LO(*l); \
	if (l[0]=='_' && l[1]=='/') stop=1; \
	l=&s->scl[p]; \
	l[1]=(s->c)?HI(*l):LO(*l); \
	if (stop && l[0]=='/' && l[1]=='^') { \
		fprintf((IO)->out,"SDA:%.*s\nSCL:%.*s\n",p+2,s->sda,p+2,s->scl); \
		i2c_debug_initstate(io); \
	} \
	} while(0)
#define wait_us(IO,US) fprintf((IO)->out,"\n");

/* generalize misc and port open/close */
#define power(io,on)
#define openport(io) (i2c_debug_initstate(io),1)
#define closeport(io)
#define portmatch(io1,io2) 0
#define remainingports() 0

#endif
