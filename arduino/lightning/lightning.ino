#include <WiFi.h>
#include <WiFiMulti.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

WiFiMulti wifiMulti;

#define DUSK_PIN 19
#define PIR_PIN 18
#define LIGHTNING_PIN 4
#define LED_PIN 2

#define HI(P) digitalWrite(P,HIGH)
#define LO(P) digitalWrite(P,LOW)

static void lightning(const unsigned char *pattern) {
  const int p=LIGHTNING_PIN;
  for(bool on=false;*pattern;pattern++) {
    if (!on) {HI(p); on=true;} else {LO(p); on=false;}
    delay(*pattern);
  }
  LO(p);
}
static void blinkinput(int pin,int ms,bool on) {
  pinMode(pin,OUTPUT);
  if (on) LO(pin); else HI(pin);
  delay(ms/4);
  if (on) HI(pin); else LO(pin);
  delay(ms/2);
  if (on) LO(pin); else HI(pin);
  delay(ms/4);
  LO(pin);
  pinMode(pin,INPUT); /* no pull */
}
static void blinkred(int ms) {blinkinput(DUSK_PIN,ms,false);}
static void blinkgreen(int ms) {blinkinput(PIR_PIN,ms,true);}

void setup() {
  Serial.begin(115200);

  pinMode(LIGHTNING_PIN,OUTPUT);
  digitalWrite(LIGHTNING_PIN,LOW);
  for(int i=0;i<1;i++) {
    Serial.println("red");
    blinkred(4000);
    Serial.println("green");
    blinkgreen(500);
  }

  Serial.println("Finding network");
  #include "smithnetac.h"
  if(wifiMulti.run() == WL_CONNECTED) {
    Serial.println("WiFi connected");
  }
  // ArduinoOTA.setPort(3232);
  // ArduinoOTA.setPassword("admin");

  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

  ArduinoOTA.begin();

  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

/* lightning flashes on/off in ms, artistically interpreted from
 * Diendorfer, Gerhard et al, "Detailed brightness versus lightning current correlation
 * of flashes to the Gaisberg Tower".  IC on Lightning Protection, 2002.
 */
const static unsigned char f1[]={ 10, 90,   10,70,  20,50,  150, 0        };
const static unsigned char f2[]={190, 50,  100, 0                         };
const static unsigned char f3[]={ 10,120,   10,50,  10,50,   20,50,  20, 0};
const static unsigned char f4[]={ 10,130,   10,50,  10, 0                 };
const static unsigned char *pattern[]={f1,f2,f3,f4,NULL};

void loop() {
  static unsigned long int shots=0;
  wifiMulti.run();
  ArduinoOTA.handle();

  static bool lastdark,lastboo;
  bool boo,dark;
  dark=digitalRead(DUSK_PIN);
  boo=digitalRead(PIR_PIN);
  if (boo && boo!=lastboo) {
    /* simple debounce */
    delay(200);
    boo=digitalRead(PIR_PIN);
  }

  if (lastdark!=dark || lastboo!=boo) {
    Serial.print(shots);
    Serial.print((dark)?" dark  ":" light ");
    Serial.println((boo)?"boo":".");
    if (dark && boo) {
      shots++;
      static int p=0;
      lightning(pattern[p]);
      p++;
      if (!pattern[p]) p=0;
    }
    lastdark=dark;
    lastboo=boo;
  }
}

