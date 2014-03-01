#
gcc -c -Wall -DTESTCALCS -DI2CDEBUG i2c.cpp -o i2c-test.o
gcc -Wall -DTESTCALCS -DI2CDEBUG bmp.cpp i2c-test.o -lstdc++ -o test && ./test
