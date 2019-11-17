LIBDIR=$(HOME)/c/lib
CFLAGS=-Wall -Wextra -O2 -I$(LIBDIR)

sensord: sensord.o json.o

clean:
	rm -f *.o sensord

testdatarun: sensord
	if [ ! -p testdatatty ]; then mkfifo testdatatty; fi
	./sensord &
	while true; do sleep 1; (while read line; do \
		echo >&2 "$$line"; echo "$$line"; \
		sleep 1; done) <testdata; done >testdatatty
