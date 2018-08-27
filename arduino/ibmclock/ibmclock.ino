#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <time.h>                       // time() ctime()
#include <sys/time.h>                   // struct timeval
#include <coredecls.h>                  // settimeofday_cb()
#include <SSD1306Wire.h>                // display

#define WIFIMULTIOBJECT WiFiMulti
ESP8266WiFiMulti WIFIMULTIOBJECT;

/* NodeMCU v3 12E pins */
/* Mapping of Dx label -> GPIOx (btu/d=boot option pulled up or down):
 * 	D0->16		D1->5		D2->4		D3->0(btu/flash)
 *	D4->2(btu/TX1)	D5->14		D6->12		D7->13(RX2)
 *	D8->15(btd/TX2)	D9->3(RX0)	D10->1(TX0)
 * GPIO map:
 * 	   0 -> D3: boot mode select, pull high
 * 	   1 -> D10: TX0 to USB serial
 * 	   2 -> D4: boot mode select, pull high (output at boot)
 * 	   3 -> D9: RX0 to USB serial
 * 	   4 -> D2: SDA
 *	   5 -> D1: SCL
 *	6-11 -> NC: flash
 * 	  12 -> D6: MISO
 * 	  13 -> D7: MOSI
 * 	  14 -> D5: SPI_CLK
 * 	  15 -> D8: boot mode select, pull down
 * 	  16 -> D0: wake (connect to RST)
 */
#define RESET 16 /* D0 tied to RST line */
#define LED_ONBOARD 2 /* D4 */
#define INDUCTOR 14 /* D5 connected to 440ohm resistor to 120ohm coil for 6mA */
#define TESTPIN 0 /* D3 can be pulled low to test */
#define I2C_ADDR_SDA_SCL (0x78/2),5,4
SSD1306Wire display(I2C_ADDR_SDA_SCL);

bool cbtime_set = false;

enum correction_type {
  correction_null=0,
  correction_test=2,
  correction_hourly=8,  /* from XX:57:54 to XX:58:02*/
  correction_daily=14,  /* from YY:57:54 to YY:58:08 where YY=05 or 17 */
};

static void time_is_set (void) {
  tzset();
  cbtime_set = true;
  Serial.println("------------------ settimeofday() was called ------------------");
}

static void led(bool on) {
  digitalWrite(LED_ONBOARD,on?LOW:HIGH);
}

void setup() {
  pinMode(LED_ONBOARD,OUTPUT);
  led(false);
  pinMode(RESET,INPUT); /* allow programming when not sleeping */
  digitalWrite(INDUCTOR,LOW);
  pinMode(INDUCTOR,OUTPUT);
  pinMode(TESTPIN,INPUT_PULLUP);

  Serial.begin(115200);

  display.init();
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_24);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.clear();
  display.drawString(64,0,"Waiting for");
  display.drawString(64,32,"time sync");
  display.display();
  
  settimeofday_cb(time_is_set);
  configTime(0,0,"timeserver.dfsmith.net");
  setenv("TZ","PST8PDT",1);
  WiFi.mode(WIFI_STA);
  #include "smithnetac.h"
  
  Serial.println("Starting");
}

static void correction_start(correction_type c) {
  unsigned long int ms=1000,now;
  static unsigned long int startat=0,duration=0;

  now=millis() - startat;
  
  switch(c) {
  case correction_null:
    /* polling */
    if (duration==0) break;
    if (now > duration) {
      digitalWrite(INDUCTOR,LOW); /* ensure no DC across coil */
      duration=0;
      led(false);
      Serial.println("Stopped correction");
      /* notone() */
    }
    break;
  default:
    startat=millis();
    duration=ms*c;
    led(true);
    tone(INDUCTOR,3300 /*Hz*/,duration);
    Serial.print("Started correction signal for ");
    Serial.println(duration);
    break;
  }
}

static void longwait(double seconds) {
}

static void donothing(void) {
  static int screensaver=0;
  if (screensaver++ > 10) {
    display.clear();
    display.display();
    if (screensaver > 15) screensaver=0;
  }
  
  led(true);
  delay(100);
  led(false);
  delay(2000);
}

static void wait_ms(long int ms) {
  long int deepsleep=20*1000;
  if (ms > deepsleep) {
    ms-=deepsleep;
    Serial.print("Deep sleep for ");
    Serial.println(1e-3*ms,3);
    ESP.deepSleep(1e3*ms);
    /* never gets here... */
    delay(deepsleep);
    return;
  }
  delay(ms);
}

void loop() {
  static timeval nextaction;
  timeval tv;
  time_t now,next;
  correction_type nexttype;
  struct tm tmloc,*tm;
  double wait;
  char tbuf[64];

  if (WIFIMULTIOBJECT.run()!=WL_CONNECTED) {
    Serial.println("waiting for network");
    return donothing();
  }
  Serial.println(WiFi.localIP());
  correction_start(correction_null);

  if (!cbtime_set) {
    Serial.println("no time callback");
    return donothing();
  }

  if (digitalRead(TESTPIN)==LOW) {
	correction_start(correction_test);
  }

  wait=1;
  do {
    /* time for display */
    gettimeofday(&tv, nullptr);
    now = tv.tv_sec;
    if (tv.tv_usec > 600000) now+=1;
    tm=localtime_r(&now,&tmloc);
    if (!tm) {Serial.println("invalid time"); break;}

    display.clear();
    display.setFont(ArialMT_Plain_24);
    snprintf(tbuf,sizeof(tbuf),"%02d:%02d:%02d",tm->tm_hour,tm->tm_min,tm->tm_sec);
    display.drawString(64,(tm->tm_min&1)?40:0,tbuf);
    display.setFont(ArialMT_Plain_16);
    snprintf(tbuf,sizeof(tbuf),"%04d-%02d-%02d",1900+tm->tm_year,1+tm->tm_mon,tm->tm_mday);
    display.drawString(64,((tm->tm_min&1)?0:25) + ((tm->tm_sec/2) % 24),tbuf);
    display.display();
    
    Serial.print(ctime(&now));
    Serial.print("Fractional seconds ");
    Serial.println(1e-6*tv.tv_usec,3);

    /* time for correction */
    gettimeofday(&tv,nullptr);
    now=tv.tv_sec;
    tm=localtime_r(&now,&tmloc);
    next=now+30*60;
    nexttype=correction_null;

    /* calculate next correction update */
    #define SETMIN 57 /* should be 57 */
    #define SETSEC 57 /* should be 54 */
    tm->tm_hour+=(tm->tm_min > SETMIN || (tm->tm_min==SETMIN && tm->tm_sec > SETSEC))?+1:0;
    tm->tm_min=SETMIN;
    tm->tm_sec=SETSEC;
    next=mktime(tm); /* mktime (3) spec normalizes tm back in to range (tm_hour) */
    nexttype=((tm->tm_hour % 12)==5) ? correction_daily : correction_hourly;
    Serial.print("Next action at ");
    Serial.print(ctime(&next));
    wait=difftime(next,now); /* accurate to seconds */
    wait-=1e-6*tv.tv_usec; /* accurate to subseconds */
    
    /* Correction missed? */
    if (wait < -0.25) {
      Serial.print("Missed deadline by ");
      Serial.println(wait,3);
      wait=0;
      break;
    }

    if (wait < 1.5) {
      if (wait>0) wait_ms(1000*wait);
      wait=0;
      correction_start(nexttype);
      Serial.println("Correction started");
    }

  } while(0);

  Serial.print("Waiting for ");
  Serial.println(wait,3);
  double frac=wait - floor(wait);
  if (frac>0.0) {wait_ms(1000*frac); wait-=frac;}
  if (frac>0.9) wait=0; /* waited enough */
  wait_ms((wait<1)?1000*wait:1000);
  Serial.println();
}

