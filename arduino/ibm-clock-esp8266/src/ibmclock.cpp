/* > ibmclock.cpp */
/* Daniel F. Smith, 2018, 2019 */
/**\File
 * Arduino for ESP8266 program to control an IBM/Simplex slave clock.
 * Build environment PlatformIO/Atom.
 */

#include <Arduino.h>
#include <FS.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <time.h>                       // time() ctime()
#include <sys/time.h>                   // struct timeval
#include <coredecls.h>                  // settimeofday_cb()
#include <SSD1306Wire.h>                // display

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
int textx=64; /* where to draw text on screen (centered, half width of display) */
int texty=32; /* line height of text (half height of display) */

/* settable parameters */
#define PARAMS() \
  PARAM(timeserver,"NTP server name","pool.ntp.org",128)      \
  PARAM(timezone,"Timezone description","PST8PDT",32)         \
  PARAM(triggertime,"Trigger at MM:SS","56:57",6)             \
  PARAM(triggermilli,"Trigger millisecond offset","400",4)    \
  PARAM(triggeroscillator,"Trigger audio frequency","3300",6) \
  /* end */
const char *configname="/config.json";
#define PARAM(NAME,DESC,DEFAULT,SIZE) \
  String NAME=DEFAULT;
PARAMS()
#undef PARAM

/* clock parameters */
struct {
  int min;            /* minute when the sync trigger starts */
  int sec;            /* second when the sync trigger starts */
  long int microsec;  /* additional delay for trigger */
  int hz;             /* resonant frequency of the trigger oscillator */
} ibmclock;

static void showmessage(const char *line1,const char *line2=nullptr) {
  display.clear();
  display.drawString(textx,0+(line2==nullptr)?texty/2:0,line1);
  if (line2) display.drawString(textx,32,line2);
  display.display();
}

static bool loadconfig() {
  File config=SPIFFS.open(configname,"r");
  if (!config) {
    showmessage("no config file");
    return false;
  }
  DynamicJsonDocument doc(1024);
  DeserializationError de=deserializeJson(doc,config);
  config.close();
  if (de) {
    showmessage("bad config file",de.c_str());
    return false;
  }

  Serial.println("Config file parsed JSON:");
  serializeJson(doc,Serial);
  Serial.println();

  #define PARAM(NAME,DESC,DEFAULT,SIZE) \
    {const char *tmp=doc[#NAME]; NAME=tmp;}
  PARAMS()
  #undef PARAM
  return true;
}

static bool saveconfig() {
  DynamicJsonDocument doc(1024);
  #define PARAM(NAME,DESC,DEFAULT,SIZE) \
    doc[#NAME]=NAME;
  PARAMS()
  #undef PARAM
  Serial.println("built json config:");
  serializeJson(doc,Serial);
  Serial.println();

  File config=SPIFFS.open(configname,"w");
  if (!config) {
    showmessage("cannot save config");
    return false;
  }
  serializeJson(doc,config);
  config.close();
  return true;
}

static bool saveconfig_flag=false;
static void setsaveconfig() {saveconfig_flag=true;}

static void showconfigmessage(WiFiManager *wm) {
  showmessage("config SSID",wm->getConfigPortalSSID().c_str());
  Serial.println("WifiManager configuration mode");
  Serial.println(WiFi.softAPIP());
  Serial.println(wm->getConfigPortalSSID());
}

static void getwificonfig(bool force) {
  bool hasconfig=loadconfig();
  WiFiManager wm;
  wm.setSaveConfigCallback(setsaveconfig);
  wm.setAPCallback(showconfigmessage);
  #define PARAM(NAME,DESC,DEFAULT,SIZE) \
    WiFiManagerParameter wm_desc##NAME(DESC); \
    if (DESC) wm.addParameter(&wm_desc##NAME); \
    WiFiManagerParameter wm_##NAME(#NAME,DEFAULT,NAME.c_str(),SIZE); \
    wm.addParameter(&wm_##NAME);
  PARAMS()
  #undef PARAM

  Serial.println("starting wifimanager");
  showmessage("Finding","network");
  const char *apname="IBM Clock";
  if (!((force)?wm.startConfigPortal(apname):wm.autoConnect(apname))) {
    showmessage("no config");
    Serial.println("WiFiManager failed: rebooting");
    delay(2000);
    ESP.reset();
  }

  #define PARAM(NAME,DESC,DEFAULT,SIZE) \
    NAME=wm_##NAME.getValue();
  PARAMS()
  #undef PARAM

  if (saveconfig_flag || !hasconfig) {
    showmessage(saveconfig()?"saved":"cannot save","settings");
  }
  else {
    showmessage("connected",WiFi.SSID().c_str());
  }
}

enum correction_type {
  correction_null=0,
  correction_test=2,
  correction_hourly=8,  /* from XX:57:54 to XX:58:02*/
  correction_daily=14,  /* from YY:57:54 to YY:58:08 where YY=05 or 17 */
};

static timeval timevalid;

static void time_is_set (void) {
  /* keep time valid for 13 hours */
  tzset();
  gettimeofday(&timevalid,nullptr);
  timevalid.tv_sec+=13*60*60;
  Serial.println("NTP time set");
}

static void led(bool on) {
  digitalWrite(LED_ONBOARD,on?LOW:HIGH);
}

static bool button(void) {
  return digitalRead(TESTPIN)==LOW;
}

void setup() {
  /* hardware */
  pinMode(LED_ONBOARD,OUTPUT);
  led(false);
  pinMode(RESET,INPUT); /* allow programming when not sleeping */
  digitalWrite(INDUCTOR,LOW);
  pinMode(INDUCTOR,OUTPUT);
  pinMode(TESTPIN,INPUT_PULLUP);
  Serial.begin(115200);

  /* display */
  display.init();
  textx=display.width()/2;
  texty=display.height()/2;
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_24);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  showmessage("Waiting for","network");

  if (!SPIFFS.begin()) {
    Serial.println("SPIFFS failed: cannot load/store settings");
  }
  /* reset? */
  int buttonhold;
  for(buttonhold=0;button();buttonhold++) {
    if (buttonhold<5)
      showmessage("clear net",(String(5-buttonhold)+" seconds").c_str());
    else if (buttonhold<10)
      showmessage("clear all",(String(10-buttonhold)+" seconds").c_str());
    else
      break;
    delay(1000);
  }
  if (buttonhold>=5) {
    showmessage("clearing","config");
    Serial.println("clearing settings");
    if (buttonhold>=10) SPIFFS.remove(configname);
    WiFiManager wm;
    wm.resetSettings();
    delay(3000);
    ESP.reset();
    ESP.restart();
    for(;;);
  }

  /* settings */
  getwificonfig(buttonhold>0);
  SPIFFS.end();
  settimeofday_cb(time_is_set);
  configTime(0,0,timeserver.c_str());
  setenv("TZ",timezone.c_str(),1);
  ibmclock.min=triggertime.toInt();
  triggertime.remove(0,triggertime.indexOf(':')+1);
  ibmclock.sec=triggertime.toInt();
  ibmclock.microsec=1000L*triggermilli.toInt();
  ibmclock.hz=triggeroscillator.toInt();

  Serial.println("Starting");
  Serial.println(WiFi.localIP());
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
    tone(INDUCTOR,ibmclock.hz,duration);
    Serial.print("Started correction signal for ");
    Serial.println(duration);
    break;
  }
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

static bool correction_calc(struct tm *tm,time_t *next,long int *next_us,correction_type *nexttype) {
  /* calculate next correction update */
  /* takes tm and returns rm, next and nexttype */
  tm->tm_hour+=(tm->tm_min > ibmclock.min || (tm->tm_min==ibmclock.min && tm->tm_sec > ibmclock.sec))?+1:0;
  tm->tm_min=ibmclock.min;
  tm->tm_sec=ibmclock.sec;
  *next_us=ibmclock.microsec;
  *next=mktime(tm); /* mktime (3) spec normalizes tm back in to range (tm_hour) */
  *nexttype=((tm->tm_hour % 12)==5) ? correction_daily : correction_hourly;
  return true;
}

void loop() {
  timeval tv;
  time_t now,next;
  long int next_us;
  correction_type nexttype;
  struct tm tmloc,*tm;
  double wait;
  char tbuf[64];
  wl_status_t wifi;

  wifi=WiFi.status();
  correction_start(correction_null);
  if (button()) {
    correction_start(correction_test);
  }

  /* is the time known? */
  if (timevalid.tv_sec==0) {
    Serial.println("no time available");
    return donothing();
  }
  gettimeofday(&tv,nullptr);
  if (tv.tv_sec > timevalid.tv_sec) {
    Serial.println("time expired");
    return donothing();
  }

  /* main body */
  wait=1;
  do {
    /* time for display */
    now=tv.tv_sec;
    if (tv.tv_usec > 600000) now+=1;
    tm=localtime_r(&now,&tmloc);
    if (!tm) {Serial.println("invalid time"); break;}

    display.clear();
    if (wifi!=WL_CONNECTED) {
      display.setFont(ArialMT_Plain_16);
      Serial.println("waiting for network");
    }
    else {
      display.setFont(ArialMT_Plain_24);
      Serial.println(WiFi.localIP());
    }
    snprintf(tbuf,sizeof(tbuf),"%02d:%02d:%02d",tm->tm_hour,tm->tm_min,tm->tm_sec);
    display.drawString(64,(tm->tm_min&1)?40:0,tbuf);
    display.setFont(ArialMT_Plain_16);
    snprintf(tbuf,sizeof(tbuf),"%04d-%02d-%02d",1900+tm->tm_year,1+tm->tm_mon,tm->tm_mday);
    display.drawString(64,((tm->tm_min&1)?0:25) + ((tm->tm_sec/2) % 24),tbuf);
    display.display();

    Serial.print(ctime(&now));
    Serial.print("Fractional seconds ");
    Serial.println(1e-6*tv.tv_usec,3);

    /* calculate how much to wait until next correction */
    gettimeofday(&tv,nullptr);
    now=tv.tv_sec;
    tm=localtime_r(&now,&tmloc);
    if (!correction_calc(tm,&next,&next_us,&nexttype)) {
      next=now+30*60;
      next_us=0;
      nexttype=correction_null;
    }
    Serial.print("Next action at ");
    Serial.print(ctime(&next));
    wait=difftime(next,now); /* accurate to seconds */
    wait-=1e-6*tv.tv_usec; /* accurate to subseconds */
    wait+=1e-6*next_us;

    /* correction missed? */
    if (wait < -0.25) {
      Serial.print("Missed deadline by ");
      Serial.println(wait,3);
      wait=0;
      break; /* don't correct */
    }

    /* do correction if close*/
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
