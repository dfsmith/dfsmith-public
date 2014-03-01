/* > i2c.h */

/** Simple single-threaded foreground I2C bus utility. */

#ifndef dfs_i2c_h
#define dfs_i2c_h

#include "types.h"
typedef unsigned int i2c_addr;

/* open/close (hc->half-clock time) */
extern bool i2c_init(const i2c_io *io);
extern void i2c_deinit(const i2c_io *);

/* low level */
extern void i2c_start(const i2c_io *p);
extern bool i2c_send(const i2c_io *p,u8 b);
extern u8 i2c_receive(const i2c_io *p,bool ack);
extern void i2c_restart(const i2c_io *p);
extern void i2c_stop(const i2c_io *p);

/* compound read/write */
extern      int i2c_read8 (const i2c_io *p,i2c_addr,u8 reg);
extern long int i2c_read16(const i2c_io *p,i2c_addr,u8 reg);
extern long int i2c_read24(const i2c_io *p,i2c_addr,u8 reg);
extern     bool i2c_write8(const i2c_io *p,i2c_addr,u8 reg,u8 val);

#endif
