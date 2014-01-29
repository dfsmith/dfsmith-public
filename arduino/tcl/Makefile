LIBDIR=$(HOME)/c/lib
CFLAGS+=-Wall -I$(LIBDIR)
LIBS=socklib.o fft.o complex.o

all: sendTCLudp lights midics

clean:
	rm -f sendTCLudp lights midics *.o

sendTCLudp: sendTCLudp.o $(LIBS:%=$(LIBDIR)/%) -lm

lights: lights.o tcl-patterns.o tcl-proc.o
