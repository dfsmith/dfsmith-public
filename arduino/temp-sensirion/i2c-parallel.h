/* > sht.h */
/* (C) 2005, Daniel F. Smith <dfs1122@gmail.com> */
/* This is free software covered by the LGPL */

#ifndef dfs_sht_h
#define dfs_sht_h

#ifdef __cplusplus
//extern "C" {
#endif

/* convenience only */
#ifndef PARALLEL_PORT_IO_ADDR_0
#define PARALLEL_PORT_IO_ADDR_0 0x03BC
#define PARALLEL_PORT_IO_ADDR_1 0x0378 /* standard /dev/lp0 and LPT1 */
#define PARALLEL_PORT_IO_ADDR_2 0x0278

#define PARALLEL_PORT_NSTB 0x01
#define PARALLEL_PORT_NAUTOLF 0x02
#define PARALLEL_PORT_NINIT 0x04
#define PARALLEL_PORT_NSELECT 0x08
#endif

struct shtport_s;
typedef struct shtport_s shtport;

/* note:
 * All temperatures (temp) are celsius
 * All humidities (RH) are relative %
 */
#define sht_badtemp (-1000.0)
#define sht_badrh   (-1000.0)

/* -- open/close -- */

shtport *sht_open(const shtio_t *,double voltage,int *errorcode);
	/* open SHT and confirm/set status register to 14-bit temp, 12-bit RH */
	/* parallelportIOaddr is usually 0x3BC, 0x378 or 0x278 depending on setup */
	/* dmask is the data bit that the SHT is connected to */
	/*        e.g., if on line D0 -> dmask=1, D5 -> dmask=1<<5 */
	/* cmask is the clock bit that the SHT is connected to: use PARALLEL_PORT_NSTB etc. */
	/* voltage is the voltage applied to the Vdd line of the SHT */
	/* errorcode contains a reason code if this call fails */
	/* returns shtport object on success, or NULL on fail */
void sht_close(shtport *s);

/* -- fast functions: returns data stored in shtport object -- */

double sht_getlasttemp(shtport *s); /* returns previous temperature */
double sht_getlastrh(shtport *s); /* returns previous (corrected) relative humidity */
void sht_getlasttemprh(shtport *s,double *temp,double *rh); /* returns both */
void sht_getgranularity(shtport *s,double *temp,double *rh);
	/* returns the approximate granularity of temperature and rh readings */

/* -- slow functions: perform temperature/relative humidity measurements -- */

double sht_gettemp(shtport *s);
double sht_getrh(shtport *s,double temp);
	/* reads and returns current temp or RH from specified probe */
	/* rh has temperature correction option: use sht_badtemp if temp unknown */
	/* these also set the result for sht_getlast* functions */

int sht_readmany(shtport *s,...);
	/* read temp and RH simultaneously from many SHTs connected to the same port */
	/* terminate list of shtport* with NULL */
	/* sht_getlast* may be used to retrieve results */
	/* returns how many temp and rh readings failed or -1 for bad args (0=good) */

/* -- typical usage -- */

/* two SHT7x devices on port 0x378,
 * data lines on D0 and D1, clk on -strobe, 5.0V supply:
 * 
 * shtport *one=NULL,*two=NULL;
 * do {
 *	double temp,rh;
 	parallel_io io={PARALLEL_PORT_IO_ADDR_1,1<<0,1<<1};
 *
 *	one=sht_open(&io,5.0,NULL);
 *	if (!one) {printf("no SHT on D0 found\n"); break;}
 *	two=sht_open(PARALLEL_PORT_IO_ADDR_1,1<<1,5.0,NULL);
 *	if (!two) {printf("no SHT on D1 found\n"); break;}
 *
 *	temp=sht_gettemp(one);
 *	if (temp==sht_badtemp) {printf("Bad temp reading\n"); break;}
 *	rh=sht_getrh(one,temp);
 *	if (rh==sht_badrh) {printf("Bad RH reading\n"); break;}
 *	printf("one: temp=%.2fC rh=%.1f%%\n",temp,rh);
 *
 *	if (sht_readmany(one,two,NULL)) {printf("Reading failed\n"); break;}
 *	printf("Temps %.2fC %.2fC\n",sht_getlasttemp(one),sht_getlasttemp(two));
 *	printf("RH    %.1f%% %.1f%%\n",sht_getlastrh(one),sht_getlastrh(two));
 *	} while(0);
 * if (two) sht_close(two);
 * if (one) sht_close(one);
 */

#ifdef __cplusplus
//}
#endif

#endif /* defined dfs_sht_h */
