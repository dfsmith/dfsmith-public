LIBDIR=$(HOME)/c/lib
CFLAGS=-Wall -Wextra -O2 -I$(LIBDIR)

sensord: sensord.o stack-json/src/json.c

testsensord: testsensord.o stack-json/src/json.c
	gcc -Wall -g -O0 -o $@ $^

testsensord.o: sensord.c
	gcc -Wall -c -g -O0 -o $@ -DTEST=1 $^

stack-json/src/json.c:
	git clone https://github.com/dfsmith/stack-json

clean:
	rm -f *.o sensord

testdatarun: testsensord
	if [ ! -p testdatatty ]; then mkfifo testdatatty; fi
	valgrind ./testsensord &
	while sleep 1; do ( \
		while read line; do \
			echo >&2 "$$line"; echo "$$line"; \
			sleep 1; done) <testdata; \
		done >testdatatty
