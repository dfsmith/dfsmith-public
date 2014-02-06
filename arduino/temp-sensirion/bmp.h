/* > bmp.h */
/** BMP180 I2C functions */

#ifndef dfs_bmp_h
#define dfs_bmp_h

#include "i2c.h"

typedef struct {
	/* calibration data */
	s16 AC1,AC2,AC3;
	u16 AC4,AC5,AC6;
	s16 B1,B2;
	s16 MB,MC,MD;
	u8 OSS;
} bmp_caldata;

typedef struct {
	const i2c_io *io;
	bmp_caldata cal;
	i2c_addr addr;
	
	/* last readings (raw) */
	u16 UT;
	u32 UP;
} bmp_port;

#define bmp_badpressure -1000.0
#define bmp_badtemperature -1000.0

extern bmp_port *bmp_new(const i2c_io *);
extern void bmp_delete(bmp_port *);

extern bool bmp_getreading(bmp_port *);
extern double bmp_getlasttemp(bmp_port *);
extern double bmp_getlastpressure(bmp_port *);

#endif
