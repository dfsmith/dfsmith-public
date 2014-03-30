#include "i2c-arduino.h"
#define i2c_io i2c_arduino_io
#include "sht.h"
#include "bmp.h"

#define LED_PIN 13
#define INV (-1000.0)
#define NOSTATE 'i'
#define SHTVOLTAGE (5.0)

typedef struct probedata_s probedata;
struct probedata_s {
	/* init part */
	const char *name;
	int number;
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

static bool bmpinit(probedata *p) {
	p->port.bmp=bmp_new(&p->pins.i2c_pins);
	return !!p->port.bmp;
}

static bool bmpgetreading(probedata *p) {
	if (!p->port.bmp || !bmp_getreading(p->port.bmp))
		return badval(p);
	p->state=NOSTATE;
	p->temp_c=bmp_getlasttemp_degc(p->port.bmp);
	p->rh_pc=INV;
	p->pressure_pa=bmp_getlastpressure_pascal(p->port.bmp);
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
	{"garage box",    0,shtinit,shtgetreading,{A5,A4,200,1,0}},
	{"garage outside",1,shtinit,shtgetreading,{A2,A3,200,1,0}},
	{"garage inside", 2,bmpinit,bmpgetreading,{ 2, 3,200,0,0}},
	{"garage door",   3,switch_pd_init,switch_pd_getreading,{4}},
};
#define PROBES (sizeof(probe)/sizeof(*probe))

void setup(void) {
	int i;
	bool r;
	probedata *p;
	pinMode(LED_PIN,OUTPUT);
	Serial.begin(57600);
	Serial.println("# start SHT temperature and humidity capture");
	Serial.println("# Commands: N (names)");
	
	for(i=0;i<PROBES;i++) {
		p=&probe[i];
		r=p->init(&probe[i]);
		Serial.print("# probe ");
		Serial.print(i);
		Serial.print(" \"");
		Serial.print(p->name);
		Serial.println(r?"\" started":"\" no_start");
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
	int i;
	probedata *p;

	heartbeat();

	for(i=0;i<PROBES;i++) {
		p=&probe[i];
		p->getreading(p);
		printreading(p);
	}

	if (Serial.available()>0) {
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
		default:
			break;
		}
	}
}
