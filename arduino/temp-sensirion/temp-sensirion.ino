#include "i2c-arduino.h"
typedef i2c_arduino_io shtio_t;
#include "sht.h"

#define LED_PIN 13
const i2c_arduino_io iopins[]={
	{A5,A4},
	{A2,A3},
};
shtport *sht[]={NULL,NULL};
#define SHTS (sizeof(sht)/sizeof(*sht))
#define VOLTAGE 5.0

void setup(void) {
	int i;
	pinMode(LED_PIN,OUTPUT);
	Serial.begin(57600);
	Serial.println("# start SHT temperature and humidity capture");
	for(i=0;i<SHTS;i++)
		sht[i]=sht_open(&iopins[i],VOLTAGE,NULL);
}

static void printtemp(int probe,double temp_c,double rh_pc) {
	Serial.print("probe ");
	Serial.print(probe);
	Serial.print(" ");
	if (temp_c!=-1000.0) {
		Serial.print(temp_c);
		Serial.print("degC ");
	}
	else
		Serial.print("no_temp ");
	if (rh_pc!=-1000.0) {
		Serial.print(rh_pc);
		Serial.println("\%rh");
	}
	else
		Serial.println("no_rh");
	return;
}

void loop(void) {
	int i;
	double temp,rh;

	/* heartbeat */
	digitalWrite(LED_PIN,HIGH);
	delay(100);
	digitalWrite(LED_PIN,LOW);
	delay(100);
	digitalWrite(LED_PIN,HIGH);
	delay(100);
	digitalWrite(LED_PIN,LOW);
	delay(700);

	for(i=0;i<SHTS;i++) {
		if (!sht[i]) {
			printtemp(i,-1000.0,-1000.0);
			continue;
		}
		temp=sht_gettemp(sht[i]);
		rh=sht_getrh(sht[i],temp);
		printtemp(i,temp,rh);
	}
}
