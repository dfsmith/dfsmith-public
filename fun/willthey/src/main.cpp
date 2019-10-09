#include <Arduino.h>

class meteroutput {
  /* mostly constant */
  int meterchan;
  int meterpin;
  int meterbits;
  int metermax; // 1<<meterbits;

  int current;

public:
  meteroutput(int channel=0,int pin=12,int bits=8,int max=222):
    meterchan(channel),
    meterpin(pin),
    meterbits(bits),
    metermax(max),
    current(0)
  {
    ledcSetup(meterchan,12000,meterbits);
    ledcAttachPin(meterpin,meterchan);
    current=max;
    to(0,max);
  }

  int getmax() {
    return metermax;
  }

  int to(int to,int speed=10) {
    int dir;
    int d=150/speed;
    dir = (to < current) ? -1 : 1;

    while(current != to) {
      current+=dir;
      ledcWrite(meterchan,current);
      delay(d);
    }
    Serial.println(current);
    return current;
  }

  int tomax(int speed=1) {
    to(metermax,speed);
    return current;
  }

};

class ledoutput {
  int pin;
  bool inverted;
public:
  ledoutput(int pin=LED_BUILTIN,bool inverted=true):
    pin(pin),inverted(inverted) {
      off();
      pinMode(pin,OUTPUT);
    }

  void on() {
    digitalWrite(pin,(inverted)?LOW:HIGH);
  }
  void off() {
    digitalWrite(pin,(inverted)?HIGH:LOW);
  }
  void blink(int times=1,int onms=100,int offms=200) {
    for(;times>0;times--) {
      on();
      delay(onms);
      off();
      delay(offms);
    }
  }
};

meteroutput meter;
ledoutput led;

void gad() {
  int x=random(0,meter.getmax());
  led.blink(1);
  meter.to(x);
  led.blink(3);
  delay(random(2,7)*1000);
}

void panic() {
  for(int i=0;i<25;i++) {
    if (i%2==0) led.on(); else led.off();
    meter.to(random(0,meter.getmax()),100);
    delay(500);
  }
}

void setup() {
  meter.to(0);
  led.off();
  Serial.begin(115200);
}

void loop() {
  static bool init=false;
  if (!init) {
    digitalWrite(LED_BUILTIN,LOW);
    meter.tomax(40);
    delay(2000);
    digitalWrite(LED_BUILTIN,HIGH);
    meter.to(0,40);
    delay(2000);
    init=true;
  }
  if (random(0,10)==0)
    panic();
  else
    gad();
}
