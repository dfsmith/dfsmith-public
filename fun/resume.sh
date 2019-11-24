#!/bin/bash

header=`ls -1 -t spidercheck-*.h | head -1`
cp -v $header spidercheck.h
gcc -Wall -Wextra -O3 spider.c && ./a.exe
