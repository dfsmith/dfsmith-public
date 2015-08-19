CFLAGS=-O2 -Wall
LFLAGS=-lm
CDEFS=

BINDIR=$(HOME)/bin
BINARIES=rhtoah minmax
TARGETS=$(BINARIES:%=$(BINDIR)/%)

$(BINDIR)/%: %.c
	$(CC) $(CFLAGS) $(CDEFS) $< -o $@ $(LFLAGS)
	
all: $(TARGETS)
