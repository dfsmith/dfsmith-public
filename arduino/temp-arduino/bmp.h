/* > bmp.h */
/** BMP180 I2C functions */

#ifndef dfs_bmp_h
#define dfs_bmp_h

#include "i2c.h"

typedef struct {
	u8 OSS;
	/* calibration data */
	s16 AC1,AC2,AC3;
	u16 AC4,AC5,AC6;
	s16 B1,B2;
	s16 MB,MC,MD;
	/* readings */
	u16 UT;
	u32 UP;
} bmp180_caldata;

typedef struct {
	/* compensation data (table 16 BME280 datasheet) */
	u16 T1;
	s16 T2,T3;
	u16 P1;
	s16 P2,P3,P4,P5,P6,P7,P8,P9;
	u8 H1;
	s16 H2;
	u8 H3;
	s16 H4,H5;
	s8 H6;
	/* readings */
	u16 UH;
	u32 UT;
	u32 UP;
} bme280_caldata;

typedef struct bmp_port_s bmp_port;

struct bmp_methods_s {
	bool (*startreading)(bmp_port*);
	double (*getlasttemp_degc)(bmp_port*);
	double (*getlastpressure_pascal)(bmp_port*);
	double (*getlasthumidity_rel)(bmp_port*);
};

struct bmp_port_s {
	const struct bmp_methods_s *m;
	const i2c_io *io;
	//enum {BMPINV,BMP180,BMP280,BME280} type;
	union {
		bmp180_caldata cal180;
		bme280_caldata cal280;
	};
	i2c_addr addr;
};

#define bmp_badpressure -1000.0
#define bmp_badtemperature -1000.0
#define bmp_badhumidity -1000.0

extern bmp_port *bmp_new(const i2c_io *,u8 offset);
extern void bmp_delete(bmp_port *);

#endif
