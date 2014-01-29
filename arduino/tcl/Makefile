CFLAGS+=-Wall -I..
LIBS=../lib/socklib.o ../lib/fft.o ../lib/complex.o

all: sendTCLudp lights midics

clean:
	rm -f sendTCLudp lights midics *.o

sendTCLudp: sendTCLudp.o $(LIBS) -lm

lights: lights.o tcl-patterns.o tcl-proc.o
