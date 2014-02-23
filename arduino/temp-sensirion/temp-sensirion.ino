#include "i2c-arduino.h"
#define i2c_io i2c_arduino_io
#include "sht.h"
#include "bmp.h"

#define LED_PIN 13
#define INV (-1000.0)
#define SHTVOLTAGE (5.0)

typedef struct probedata_s probedata;
struct probedata_s {
	/* init part */
	const char *name;
	int number;
	i2c_arduino_io pins;
	bool (*init)(probedata *p);
	bool (*getreading)(probedata *p);

	/* runtime part */
	union {
		shtport *sht;
		bmp_port *bmp;
	} port;
	double temp_c;
	double rh_pc;
	double pressure_pa;
};

static bool badval(probedata *p) {
	p->temp_c=p->rh_pc=p->pressure_pa=INV;
	return false;
}

static bool shtinit(probedata *p) {
	p->port.sht=sht_open(&p->pins,SHTVOLTAGE,NULL);
	return !!p->port.sht;
}

static bool shtgetreading(probedata *p) {
	if (!p->port.sht)
		return badval(p);
	p->temp_c=sht_gettemp(p->port.sht);
	p->rh_pc=sht_getrh(p->port.sht,p->temp_c);
	p->pressure_pa=INV;
	return true;
}

static bool bmpinit(probedata *p) {
	p->port.bmp=bmp_new(&p->pins);
	return !!p->port.bmp;
}

static bool bmpgetreading(probedata *p) {
	if (!p->port.bmp || !bmp_getreading(p->port.bmp))
		return badval(p);
	p->temp_c=bmp_getlasttemp_degc(p->port.bmp);
	p->rh_pc=INV;
	p->pressure_pa=bmp_getlastpressure_pascal(p->port.bmp);
	return true;
}

static probedata probe[]={
	{"garage box",    0,{A5,A4,200},shtinit,shtgetreading},
//	{"garage outside",1,{A2,A3,200},shtinit,shtgetreading},
//	{"garage inside", 2,{ 2, 3,200},bmpinit,bmpgetreading},
};
#define PROBES (sizeof(probe)/sizeof(*probe))

void setup(void) {
	int i;
	bool r;
	probedata *p;
	pinMode(LED_PIN,OUTPUT);
	Serial.begin(57600);
	Serial.println("# start SHT temperature and humidity capture");
	
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
}
