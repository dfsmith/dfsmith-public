/* > serialtrace.cpp
/* (C) 2005,2013 Daniel F. Smith <dfs1122@gmail.com> */
/*
 * This is free software covered by the Lesser GPL.
 * See COPYING or http://gnu.org/licenses/lgpl.txt for details.
 * If you link your binaries with this library you are obliged to provide
 * the source to this library and your modifications to this library.
 */

#include <Arduino.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>

void serialtrace(const char *fmt,...) {
	char tmp[80];
	va_list args;
	va_start(args,fmt);
	vsnprintf(tmp,sizeof(tmp),fmt,args);
	va_end(args);
	Serial.print(tmp);
}
