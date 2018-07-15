/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 by Daniel Eichhorn
 * Copyright (c) 2016 by Fabrice Weinberg
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */
#include <time.h>
#include "SSD1306.h"
#include "OLEDDisplayUi.h"
#include "palladio_b.h"
#include "images.h"

#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>

#define lengthof(X) (sizeof(X)/sizeof(*(X)))
#define DEGREES "\u00B0"

WiFiMulti wifiMulti;
SSD1306 oleddisplay(0x3c,5,4);
OLEDDisplayUi ui(&oleddisplay);
static const int screenbottom=DISPLAY_HEIGHT;
static const int screenright=DISPLAY_WIDTH;

static float CtoF(float degC) {
  return 32.0+(9.0/5)*degC;
}

static String nextword(String& line) {
  /* remove the first floating point number from line */
  line.trim();
  int num=line.indexOf(' ');
  if (num<1) num=line.length();
  String f=line.substring(0,num);
  line.remove(0,num+1);
  return f;
}

static float nextfloat(String& line) {
  /* remove the first floating point number from line */
  do {
    String f;
    f=nextword(line);
    if (f.length()<1) break;
    if (f.equals("?")) break;
    return f.toFloat();
  } while(0);
  return nan("");
}

class minilog {
  public:
  
  typedef uint16_t ytype;
  const ytype LOGNAN=65535;
  int interval; /* time_t units */
  ytype y[DISPLAY_WIDTH-12];
  int logoffset; /* next empty y slot */
  time_t lastlog;
  time_t lastrescale;
  float min,max;

  minilog(int seconds) {
    clear();
    interval=seconds;
  }

  int entries() {
    return lengthof(y);
  }

  void clear() {
    logoffset=0;
    lastlog=0;
    min=max=nan("");
    for(int i=0;i<entries();i++)
      y[i]=LOGNAN;    
  }

  void minmax(float x) {
    if (!isnormal(x)) return;
    if (!isnormal(min)) min=x; else if (x<min) min=x;
    if (!isnormal(max)) max=x; else if (x>max) max=x;
  }

  void reminmax() {
    min=max=nan("");
    for(int i=0;i<entries();i++) minmax(decode(y[i]));
  }

  ytype encode(float x) {
    if (!isnormal(x)) return LOGNAN;
    float y=(x+128.0)*128.0;
    if (y<0) return 0;
    if (y>LOGNAN-1) return LOGNAN-1;
    return y;
  }

  float decode(ytype y) {
    if (y==LOGNAN) return nan("");
    return (y/128.0)-128.0;
  }

  void add(float x,time_t when) {
    if (difftime(when,lastlog) > entries()*interval) {clear(); lastlog=when-1;}
    if (difftime(when,lastrescale) > 5*entries()*interval) {reminmax(); lastrescale=when;}

    int xx=encode(x);
    minmax(x);

    for(;when > lastlog;lastlog+=interval) {
      y[logoffset++] = (when > lastlog+interval) ? LOGNAN : xx;
      if (logoffset >= entries()) logoffset=0;
    }
  }

  float operator[](int index) {
    do {
      if (index >= entries()) break;
      if (index < 0) break;
      int i=index+logoffset;
      if (i>=entries()) i-=entries();
      return decode(y[i]);
    } while(0);
    return nan("");
  }
  
};

static minilog outside(2*60);

typedef struct probeval_s {
  float degC;
  float relh;
  float hPa;
  float state;
  const char *msg;
} probeval;

class probeinfo {
  public:

  bool alert;
  time_t captime;
  probeval val[4];

  probeinfo() {clear();}
  
  int probes() {return lengthof(val);}

  void clearalert() {alert=false;}
  void setalert() {alert=true;}

  void clear() {
    clearalert();
    captime=-1;
    for(int i=0;i<probes();i++) {
      val[i].degC=val[i].relh=val[i].hPa=val[i].state=nan("");
      val[i].msg=NULL;
    }
  }

  void print() {
    Serial.printf("Captime %ld\n",captime);
    for(int i=0;i<probes();i++) {
      Serial.printf("%d: %f %f %f %f\n",i,val[i].degC,val[i].relh,val[i].hPa,val[i].state);
    }
  }

  void checkalert() {
    /* log outside temp to chart */
    if (captime>0 && isnormal(val[1].degC)) {
      //float deg=val[1].degC;
      float deg=CtoF(val[1].degC);
      outside.add(deg,captime);
    }
      
    do {
      /* break to flash the screen! */
      if (isnormal(val[0].relh ) && val[0].relh  > 45.0) break;
      if (isnormal(val[3].state) && val[3].state < 0.99) {
          /* change garage header */
          val[2].msg="Open";
          break;
      }

      val[2].msg="Closed";
      if (alert) Serial.println("Alert cleared");
      clearalert();
      return;
    } while(0);
    if (!alert) Serial.println("Alert set");
    setalert();
  }

  probeinfo(String result) {
    /* create probeinfo from http "measurementlines" string */
    Serial.println(result);
    clear();
  
    while(result.length() > 0) {
      /* pull the first next line from result (surprisingly complex, might not be robust) */
      int cr=result.indexOf('\n');
      if (cr<0) cr=result.length();
      String line=result.substring(0,cr+1);
      result.remove(0,cr+1);
      line.trim();
      if (line.length()<1) continue;
  
      /* parse */
      int colon=line.indexOf(':');
      if (colon<1) continue;
      String type=line.substring(0,colon);
      line.remove(0,colon+1);
      line.trim();
      
      if (type.equals("seconds")) {
        captime=nextword(line).toInt(); /* drops decimal (floor) */
        if (captime <= 0) continue;
        captime+=nextword(line).toInt(); /* toInt() is 0 if unreadable */
        continue;
      }
      if (type.equals("degC")) {
        for(int i=0; i<probes(); i++)
          val[i].degC=nextfloat(line);
        continue;
      }
      if (type.equals("%rh")) {
        for(int i=0; i<probes(); i++)
          val[i].relh=nextfloat(line);
        continue;
      }
      if (type.equals("hPa")) {
        for(int i=0; i<probes(); i++)
          val[i].hPa=nextfloat(line);
        continue;
      }
      if (type.equals("state")) {
        for(int i=0; i<probes(); i++)
          val[i].state=nextfloat(line);
        continue;
      }
    }
    checkalert();
  }

};

static probeinfo probes;
static String probename[lengthof(probes.val)];
static bool overlayswap;

void drawoverlay(OLEDDisplay *display, OLEDDisplayUiState* state) {
  static char lbuf[16],rbuf[16];
  const char *left,*right;
  if (probes.captime <= 0) {
    sprintf(rbuf,"%d",WiFi.status());
    left="No connection";
    right=rbuf;
  }
  else {
    struct tm tm;
    localtime_r(&probes.captime,&tm);

    /* set brightness from ambient light estimation */
    int minutes=tm.tm_hour*60 + tm.tm_min;
    if      (minutes <  6*60) display->setContrast(0);
    else if (minutes < 12*60) display->setContrast(255*(minutes- 6*60)/6*60);
    else if (minutes < 18*60) display->setContrast(255);
    else                      display->setContrast(255*(24*60-minutes)/6*60);
    
    strftime(lbuf,sizeof(lbuf),"%Y-%m-%d",&tm);
    strftime(rbuf,sizeof(rbuf),"%H:%M",&tm);
    left=lbuf;
    right=rbuf;
  }

  /* blink screen */
  unsigned long int timer=millis()/256;
  if (probes.alert && (timer%2)==0)
    display->invertDisplay();
  else
    display->normalDisplay();

  /* burn-in mitigation */
  overlayswap=((timer/256)%2 == 0) ? false:true;
  ui.setIndicatorPosition(overlayswap?LEFT:RIGHT);

  /* the overlay */

  display->setTextAlignment(TEXT_ALIGN_LEFT);
  display->setFont(overlayswap?ArialMT_Plain_16:ArialMT_Plain_10);
  display->drawString(overlayswap?12:0,overlayswap?screenbottom-16:0,String(overlayswap?right:left));

  display->setTextAlignment(TEXT_ALIGN_RIGHT);
  display->setFont(overlayswap?ArialMT_Plain_10:ArialMT_Plain_16);
  display->drawString(overlayswap?screenright:screenright-10,overlayswap?screenbottom-10:0,String(overlayswap?left:right));
}

void probeframe(OLEDDisplay *display,int16_t x,int16_t y,int i) {
  String header;
  probeval *p;
  if (i<0 || i>probes.probes()) {
    header=String("Screen ")+i;
    p=NULL;
  }
  else {
    p=&probes.val[i];
    header=probename[i];
    if (p->msg) {header+=" "; header+=p->msg;}
  }
  y+=overlayswap?0:9;
  x+=overlayswap?12:0;

  display->setTextAlignment(TEXT_ALIGN_LEFT);
  display->setFont(ArialMT_Plain_24);
  display->drawString(x,y,header); y+=24;

  if (!p) return;
  y-=2;
  display->setFont(URW_Palladio_L_Bold_16);
  if (isnormal(p->degC)) {
    #ifdef NOHIGHLIGHT
    display->drawString(x,y,String(p->degC,3) + DEGREES "C"); y+=16;
    #else
    char dec[8],frac[16];
    int cx=x;

    //float deg=p->degC; const char *ffmt="%.3d" DEGREES "C"; float scale=1000;
    float deg=CtoF(p->degC); const char *ffmt="%.1d" DEGREES "F"; float scale=10;
    sprintf(dec,"%.0f",deg);
    sprintf(frac,ffmt,int(round(scale*(deg-floor(deg)))));

    display->setFont(ArialMT_Plain_24);
    display->drawString(cx,y,dec);
    cx+=display->getStringWidth(dec);
    display->fillCircle(cx+4,y+10,2);
    cx+=8;
    display->setFont(URW_Palladio_L_Bold_16);
    display->drawString(cx,y,frac);
    y+=16;
    x+=screenright-12;
    display->setTextAlignment(TEXT_ALIGN_RIGHT);
    #endif
  }
  if (isnormal(p->relh)) {
    display->drawString(x,y,String(p->relh,1)+" %");
    y+=16;
  }
  if (isnormal(p->hPa)) {
    display->drawString(x,y,String(p->hPa,1)+" hPa");
    y+=16;
  }
  if (isnormal(p->state)) {
    if (p->state==1.0)
      display->drawString(x,y,"Closed");
    else if (p->state==0.0)
      display->drawString(x,y,"Open");
    else
      display->drawString(x,y,String(100*p->state,0)+" closed");
    y+=16;
  }
}

void drawgraph(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  if (!isnormal(outside.min) || !isnormal(outside.max)) {
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(ArialMT_Plain_16);
    display->drawString(x+screenright/2,y+screenbottom/2-8,"No data");
    return;
  }
  int n=outside.entries();

  /* scaling */
  float ymin=floor(outside.min);
  float ymax=ceil(outside.max);
  float yrange=ymax-ymin;
  if (yrange<2) {ymin=(ymin+ymax)/2-1; yrange=2; ymax=ymin+yrange;}
  float ydivision=yrange/4;
  if      (ydivision<=1) ydivision=1;
  else if (ydivision<=2) ydivision=2;
  else if (ydivision<=5) ydivision=5;
  else                   ydivision=10;
  time_t trange=n*outside.interval;
  time_t tdivision=trange/4;
  if      (tdivision<=   60) tdivision=   60;
  else if (tdivision<= 5*60) tdivision= 5*60;
  else if (tdivision<=10*60) tdivision=10*60;
  else if (tdivision<=30*60) tdivision=30*60;
  else                       tdivision=60*60;  
  time_t tmarker=outside.lastlog - outside.lastlog % tdivision;
  time_t tmin=outside.lastlog - trange;
  
  x+=overlayswap?12:0;
  y+=overlayswap?0:12;
  int width=n;
  int height=screenbottom-12;
  #define SCALEY(Y) (height - (height*((Y)-ymin)/yrange))
  #define SCALEX(T) (width  - (outside.lastlog-(T)) / outside.interval)

  /* graticule */
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  display->setFont(ArialMT_Plain_10);
  for(int temp=int(ymin/ydivision-1)*ydivision;temp<=int(1+ymax/ydivision)*ydivision;temp+=ydivision) {
    int yy=SCALEY(temp);
    if (yy<0 || yy>=height) continue;
    display->drawString(x,y+SCALEY(temp)-5,String(temp));
    int step=(temp==0)?tdivision/10:tdivision/5;
    for(time_t tt=tmarker + tdivision;tt>tmin;tt-=step) {
      int xx=SCALEX(tt);
      if (xx<0 || xx>=width) continue;
      display->setPixel(x+xx,y+yy);
    }
  }
  for(time_t tt=tmarker;tt>tmin;tt-=tdivision) {
    int xx=SCALEX(tt);
    if (xx<0 || xx>=width) continue;
    for(int yy=0;yy<height;yy+=4)
      display->setPixel(x+xx,y+yy);
  }

  /* draw thick line */
  float lasty=outside[0];
  float lyy=SCALEY(lasty);
  for(int i=1;i<n;i++) {
    float thisy=outside[i];
    int tyy=SCALEY(thisy);
    if (!isnormal(lasty)) {lasty=thisy; lyy=tyy;}
    if (isnormal(thisy)) {
      int from=tyy,to=lyy;
      if (from > to) {from=lyy; to=tyy;}
      display->drawVerticalLine(x+i,y+from-1,3+to-from);
    }
    lasty=thisy;
    lyy=tyy;
  }
}
void drawprobe0(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {probeframe(display,x,y,0);}
void drawprobe1(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {probeframe(display,x,y,1);}
void drawprobe2(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {probeframe(display,x,y,2);}

FrameCallback frames[]={drawgraph,drawprobe0,drawprobe1,drawprobe2};
int frameCount=lengthof(frames);
OverlayCallback overlays[]={drawoverlay};
int overlaysCount=lengthof(overlays);

void setup() {
  Serial.begin(115200);
  Serial.println("Starting code");

  /* display init */
  ui.setTargetFPS(30);
  ui.setActiveSymbol(activeSymbol);
  ui.setInactiveSymbol(inactiveSymbol);
  ui.setIndicatorPosition(RIGHT);
  ui.setIndicatorDirection(LEFT_RIGHT);
  ui.setFrameAnimation(SLIDE_LEFT);
  ui.setFrames(frames,frameCount);
  ui.setOverlays(overlays,overlaysCount);
  ui.init();
  oleddisplay.flipScreenVertically();

  /* network init */
  #define WIFIMULTIOBJECT wifiMulti
  #include "smithnetac.h"

  /* data init */
  for(int i=0;i<lengthof(probename);i++) {
    switch(i) {
      case 0: probename[i]="Box"; break;
      case 1: probename[i]="Outside"; break;
      case 2: probename[i]="Garage"; break;
      case 3: probename[i]="Door"; break;
      default: probename[i]=String("Probe ")+i;
    }
  }
}

void loop() {
  static unsigned long int next=0,validuntil=0;
  static bool wasconnected=false;
  uint8_t /*wl_status_t*/ wifistatus;

  int remainingTimeBudget=ui.update();
  unsigned long int start=millis();

  wifistatus=wifiMulti.run();
  if (wifistatus==WL_CONNECTED && start>next) {
    HTTPClient http;
    wasconnected=true;

    Serial.println("Gather...");
    http.begin("http://dfsmith.net:8888/measurementlines");
    int httpCode=http.GET();
    Serial.printf("get -> %d\n",httpCode);
    if (httpCode!=HTTP_CODE_OK) {
      next=start+1*1000;
    }
    else {
      validuntil=start+10*60*1000;
      next=start+10*1000;
      probes=probeinfo(http.getString());
    }
  }
  if (start > validuntil) probes.clear();

  /* poor man's watchdog */
  if (wasconnected && wifistatus!=WL_CONNECTED) {
    //esp_wifi_wps_disable();
    ESP.restart();
  }

  unsigned long int stop=millis();
  int delta=stop-start;
  if (delta > remainingTimeBudget)
    Serial.printf("workload %dms of %dms\n",delta,remainingTimeBudget);
  else
    delay(remainingTimeBudget-delta);
}

