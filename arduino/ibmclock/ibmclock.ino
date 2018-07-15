#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <time.h>                       // time() ctime()
#include <sys/time.h>                   // struct timeval
#include <coredecls.h>                  // settimeofday_cb()
#include <SSD1306Wire.h>                // display

#define WIFIMULTIOBJECT WiFiMulti
ESP8266WiFiMulti WIFIMULTIOBJECT;

/* NodeMCU v3 12E pins */
#define RESET 16 /* D0 tied to RST line */
#define LED_ONBOARD 2 /* D4 */
#define INDUCTOR 0 /* D3 connected to 440ohm resistor to 120ohm coil for 6mA */
#define I2C_ADDR_SDA_SCL (0x78/2),5,4
SSD1306Wire display(I2C_ADDR_SDA_SCL);

bool cbtime_set = false;

enum correction_type {
  correction_null=0,
  correction_hourly=8,  /* starts at XX:57:54 */
  correction_daily=14,  /* starts at 05:57:54 */
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
  Serial.print("Deep sleep for ");
  Serial.println(seconds,1);
  ESP.deepSleep(1e6*seconds);
}

static void donothing(void) {
  static int screensaver=0;
  led(true);
  delay(100);
  led(false);
  delay(2000);
  if (screensaver++ > 60) {
    display.clear();
    display.display();
    screensaver=0;
  }
}

void loop() {
  static timeval nextaction;
  timeval tv;
  time_t now,next;
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
    if (tm->tm_min <= 58 || (tm->tm_min == 58 && tm->tm_sec < 54)) {
      tm->tm_sec=54;
      tm->tm_min=58;
      next=mktime(tm);
      Serial.print("Next action at ");
      Serial.print(ctime(&next));
    }

    wait=difftime(next,now); /* accurate to seconds */
    wait-=1e-6*tv.tv_usec; /* accurate to subseconds */
    
    if (wait<-0.1) /* missed it... */ {wait=0; break;}
    #if 0
    if (wait > 200) {
      longwait(wait-200);
      break;
    }
    #endif

    Serial.print("Waiting for ");
    Serial.println(wait,3);
    if (wait < 1.5) {
      if (wait>0) delay(1000*wait);
      wait=0;
      switch(tm->tm_hour) {
      case 5:
      case 17:
        correction_start(correction_daily);
        break;
      default:
        correction_start(correction_hourly);
        break;
      }
    }

  } while(0);

  double frac=wait - floor(wait);
  if (frac>0.0) {delay(1000*frac); wait-=frac;}
  if (frac>0.9) wait=0; /* waited enough */
  delay((wait<1)?1000*wait:1000);
  Serial.println();
}

