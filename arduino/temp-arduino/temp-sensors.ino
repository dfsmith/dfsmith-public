#include "i2c-arduino.h"
#define i2c_io i2c_arduino_io
#include "sht.h"
#include "bmp.h"

#if 0
#include "serialtrace.h"
#define TR(X) serialtrace X
#else
#define TR(X)
#endif

#define LED_PIN 13
#define INV (-1000.0)
#define NOSTATE 'i'
#define SHTVOLTAGE (5.0)

typedef struct probedata_s probedata;
struct probedata_s {
	/* init part */
	const char *name;
	int number;
	bool started;
	bool (*init)(probedata *p);
	bool (*getreading)(probedata *p);
	union {
		i2c_arduino_io i2c_pins;
		int switch_pin;
	} pins;

	/* runtime part */
	union {
		shtport *sht;
		bmp_port *bmp;
	} port;
	char state;
	double temp_c;
	double rh_pc;
	double pressure_pa;
};

static bool badval(probedata *p) {
	p->temp_c=p->rh_pc=p->pressure_pa=INV;
	p->state=NOSTATE;
	return false;
}

static bool shtinit(probedata *p) {
	p->port.sht=sht_open(&p->pins.i2c_pins,SHTVOLTAGE,NULL);
	return !!p->port.sht;
}

static bool shtgetreading(probedata *p) {
	if (!p->port.sht)
		return badval(p);
	p->state=NOSTATE;
	p->temp_c=sht_gettemp(p->port.sht);
	p->rh_pc=sht_getrh(p->port.sht,p->temp_c);
	p->pressure_pa=INV;
	return true;
}

static bool bmp1init(probedata *p) {
	p->port.bmp=bmp_new(&p->pins.i2c_pins,1);
	return !!p->port.bmp;
}

static bool bmpanyinit(probedata *p) {
	p->port.bmp=bmp_new(&p->pins.i2c_pins,1);
	if (!p->port.bmp)
		p->port.bmp=bmp_new(&p->pins.i2c_pins,0);
	return !!p->port.bmp;
}

static bool bmpgetreading(probedata *p) {
	bmp_port *b=p->port.bmp;
	if (!b || !b->m->startreading(b))
		return badval(p);
	p->state=NOSTATE;
	p->temp_c=b->m->getlasttemp_degc(b);
	p->rh_pc=b->m->getlasthumidity_rel(b);
	p->pressure_pa=b->m->getlastpressure_pascal(b);
	return true;
}

static bool switch_pd_init(probedata *p) {
	/* pulldown switch */
	int pin=p->pins.switch_pin;
	pinMode(pin,INPUT);
	digitalWrite(pin,HIGH);
	return true;
}

static bool switch_pd_getreading(probedata *p) {
	p->state=(digitalRead(p->pins.switch_pin)==LOW)?'1':'0';
	p->temp_c=INV;
	p->rh_pc=INV;
	p->pressure_pa=INV;
	return true;
}

static probedata probe[]={
	{"garage box",    0,false,shtinit,shtgetreading,{A5,A4,200,1,0}},
	{"garage outside",1,false,shtinit,shtgetreading,{A2,A3,200,1,0}},
	{"garage inside", 2,false,bmpanyinit,bmpgetreading,{ 2, 3,200,0,0}},
	{"garage door",   3,false,switch_pd_init,switch_pd_getreading,{4}},
};
#define PROBES (sizeof(probe)/sizeof(*probe))

void setup(void) {
	unsigned int i;
	probedata *p;
	pinMode(LED_PIN,OUTPUT);
	Serial.begin(115200);
	Serial.println("# start SHT temperature and humidity capture");
	Serial.println("# Commands: N (names)");

	for(i=0;i<PROBES;i++) {
		p=&probe[i];
		if (p->started) continue;
		p->started=p->init(p);
		Serial.print("# probe ");
		Serial.print(i);
		Serial.print(" \"");
		Serial.print(p->name);
		Serial.println(p->started?"\" started":"\" no_start");
	}
}

static void printreading(probedata *p) {

	Serial.print("probe ");
	Serial.print(p->number);

	if (p->state!=NOSTATE) {
		Serial.print(" ");
		Serial.print(p->state);
		Serial.print("state");
	}

	if (p->temp_c!=INV) {
		Serial.print(" ");
		Serial.print(p->temp_c);
		Serial.print("degC");
	}

	if (p->rh_pc!=INV) {
		Serial.print(" ");
		Serial.print(p->rh_pc);
		Serial.print("\%rh");
	}

	if (p->pressure_pa!=INV) {
		Serial.print(" ");
		Serial.print(0.01*p->pressure_pa);
		Serial.print("hPa");
	}

	Serial.print("\n");
	return;
}

static void heartbeat(void) {
	TR(("beat\n"));
	digitalWrite(LED_PIN,HIGH);
	delay(100);
	digitalWrite(LED_PIN,LOW);
	delay(100);
	digitalWrite(LED_PIN,HIGH);
	delay(100);
	digitalWrite(LED_PIN,LOW);
	delay(700);
}

void loop(void) {
	unsigned int i;
	probedata *p;

	heartbeat();

	for(i=0;i<PROBES;i++) {
		p=&probe[i];
		if (!p->started) {
			TR(("# restarting %d\n",i));
			p->started=p->init(p);
			continue;
		}
		p->getreading(p);
		printreading(p);
	}

	while(Serial.available()>0) {
		int cmd;
		cmd=Serial.read();
		switch(cmd) {
		case 'N':
			for(i=0;i<PROBES;i++) {
				p=&probe[i];
				Serial.print("# probe ");
				Serial.print(p->number);
				Serial.print(" ");
				Serial.println(p->name);
			}
			break;
		case '?':
			Serial.println("N - probe list");
			break;
		default:
			break;
		}
	}
}
