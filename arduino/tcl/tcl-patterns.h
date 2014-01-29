#ifndef dfs_tcl_patterns_h
#define dfs_tcl_patterns_h

#define TCL_CHANNELS 3 /* for R, G, B */

typedef union {
	float chan[TCL_CHANNELS];
	struct {
		float r,g,b;
	};
} tcl_pixelfloat;
#define TCL_PIXELFLOAT_ZERO {{0.0,0.0,0.0}}

typedef struct {
	long unsigned int milliseconds;
	unsigned int leds;
	tcl_pixelfloat *led;
} tcl_patternbuf;

struct tcl_pattern_data_s;
typedef struct tcl_pattern_data_s tcl_pattern_data;

typedef struct {
	int (*overlay)(tcl_pattern_data *);
	void (*delete)(tcl_pattern_data *);
} tcl_pattern_api;

struct tcl_pattern_data_s {
	tcl_pattern_api *m;
	tcl_patternbuf *buf;
	float gain;
	int params;
	float *param;
	void *data;
};

extern tcl_patternbuf *tcl_patternbuf_new(int lights);
//extern void tcl_patternbuf_start(tcl_patternbuf *);
extern void tcl_patternbuf_delete(tcl_patternbuf *);

/* generating */
extern tcl_pattern_data *tcl_patternzero(tcl_patternbuf *buf);
extern tcl_pattern_data *tcl_patternsinus(tcl_patternbuf *buf);
extern tcl_pattern_data *tcl_patterncylon(tcl_patternbuf *buf);
extern tcl_pattern_data *tcl_patternwave(tcl_patternbuf *buf);
extern tcl_pattern_data *tcl_patternsparkles(tcl_patternbuf *buf);
extern tcl_pattern_data *tcl_patternsparticles(tcl_patternbuf *buf);
extern tcl_pattern_data *tcl_patterntest1(tcl_patternbuf *buf);
extern tcl_pattern_data *tcl_patternvu(tcl_patternbuf *buf);
extern tcl_pattern_data *tcl_patternshoutpong(tcl_patternbuf *buf);
extern tcl_pattern_data *tcl_patternfft(tcl_patternbuf *buf);

/* processing */
extern tcl_pattern_data *tcl_pattern_fade(tcl_patternbuf *buf,double retain);
extern tcl_pattern_data *tcl_pattern_bleed(tcl_patternbuf *buf,double bleed);
extern tcl_pattern_data *tcl_pattern_torgbuchar(tcl_patternbuf *,unsigned char *buf);

#endif
