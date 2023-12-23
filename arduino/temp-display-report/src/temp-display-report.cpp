#include <Arduino.h>
#include <math.h>
#include <time.h>
#include <vector>

#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <OLEDDisplayUi.h>
#include <PubSubClient.h>
#include <SSD1306.h>
#include <WiFiMulti.h>

#include <sht-object.h>

#include "images.h"
#include "palladio_b.h"

using std::isnormal;
using std::vector;

#define lengthof(X) (sizeof(X) / sizeof(*(X)))
#define DEGREES "\u00B0"
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

WiFiClient espClient;
PubSubClient mqttc("brick.dfsmith.net", 1883, espClient);
sht7x sensor(26, 25, 3.3);
WiFiMulti wifiMulti;
SSD1306 oleddisplay(0x3c, 5, 4);
OLEDDisplayUi ui(&oleddisplay);
static const int screenbottom = oleddisplay.getHeight();
static const int screenright = oleddisplay.getWidth();

/* information about the local probe on this device */
static int probenum = -1; /* cannot be 0: String.toInt() limitation */
static String location = "";
static String id = ""; /* "net:<MAC_addr>" */
#define ONBOARDPROBE 4

static bool tzknown = false;
static time_t tzoffset = 0;
static bool connected = false; /* can issue http requests */

static float CtoF(float degC) { return 32.0 + (9.0 / 5) * degC; }

class minilog {
 public:
  typedef uint16_t ytype;
  const ytype LOGNAN = 65535;
  int interval; /* time_t units */
  ytype *y;
  int ylen;
  int logoffset; /* next empty y slot */
  time_t lastlog;
  time_t lastrescale;

  int minmaxindex;
  ytype tmin, tmax;
  ytype min, max;

  minilog(int seconds) {
    clear();
    interval = seconds;
    ylen = screenright - 12;
    y = new ytype[ylen];
  }

  ~minilog() { delete y; }

  int entries() { return ylen; }
  float getmin() { return decode(min); }
  float getmax() { return decode(max); }

  void clear() {
    logoffset = 0;
    lastlog = 0;
    for (minmaxindex = 0; minmaxindex < entries(); minmaxindex++) minmax(y[minmaxindex] = LOGNAN);
  }

  void reminmax() {
    for (minmaxindex = 0; minmaxindex < entries(); minmaxindex++) minmax(y[minmaxindex]);
  }

  void minmax(ytype t) {
    if (minmaxindex == 0) {
      tmin = tmax = LOGNAN;
    }
    if (t == LOGNAN) return;
    if (tmin == LOGNAN)
      tmin = t;
    else if (t < tmin)
      tmin = t;
    if (tmax == LOGNAN)
      tmax = t;
    else if (t > tmax)
      tmax = t;

    if (minmaxindex + 1 == entries()) {
      min = tmin;
      max = tmax;
    }
  }

  ytype encode(float t) {
    if (!isnormal(t)) return LOGNAN;
    float y = (t + 128.0) * 128.0;
    if (y < 0) return 0;
    if (y > LOGNAN - 1) return LOGNAN - 1;
    return y;
  }

  float decode(ytype y) {
    if (y == LOGNAN) return nan("");
    return (y / 128.0) - 128.0;
  }

  void add(float t, time_t when) {
    if (difftime(when, lastlog) > entries() * interval) {
      clear();
      lastlog = when - 1;
    }
    if (difftime(when, lastrescale) > 5 * entries() * interval) {
      reminmax();
      lastrescale = when;
    }
    ytype yval = encode(t);
    minmax(yval);
    for (; when > lastlog; lastlog += interval) {
      y[logoffset++] = (when > lastlog + interval) ? LOGNAN : yval;
      if (logoffset >= entries()) logoffset = 0;
    }
  }

  float operator[](int index) {
    do {
      if (index >= entries()) break;
      if (index < 0) break;
      if (index == 0) minmaxindex = 0;
      if (index == minmaxindex + 1) minmaxindex = index;
      int i = index + logoffset;
      if (i >= entries()) i -= entries();
      minmax(y[i]);
      return decode(y[i]);
    } while (0);
    return nan("");
  }
};
static minilog outside(2 * 60);

class probeval {
 public:
  void clear() {
    degC = relh = hPa = state = nan("");
    msg = NULL;
    /* name=name */
  };
  probeval() { clear(); }

  float degC;
  float relh;
  float hPa;
  float state;
  const char *name;
  const char *msg;
};

class probeinfo {
 public:
  int alert;
  time_t captime;
  vector<probeval> val;

  probeinfo(size_t n) : val{n} { clear(); }

  int probes() { return val.size(); }

  void setname(int probe, const char *name) {
    size_t i = probe;
    if (i < val.size()) val[i].name = name;
  }

  void setalert(int severity) {
    if (alert == severity) return;
    Serial.printf("alert %d->%d\n", alert, severity);
    alert = severity;
    Serial.println((alert != 0) ? "Alert activated" : "Alert cleared");
    Serial.printf("alert %d->%d\n", alert, severity);
  }

  void clear() {
    captime = -1;
    for (int i = 0; i < probes(); i++) val[i].clear();
  }

  void print() {
    Serial.printf("Captime %ld\n", captime);
    for (int i = 0; i < probes(); i++) {
      Serial.printf("%d: %f %f %f %f\n", i, val[i].degC, val[i].relh, val[i].hPa, val[i].state);
    }
  }

  void checkalert() {
    /* log outside temp to chart */
    if (captime > 0 && isnormal(val[1].degC)) {
      // float deg=val[1].degC;
      float deg = CtoF(val[1].degC);
      outside.add(deg, captime);
    }
    int severity = 0;

    do {
      if (isnormal(val[3].state) && val[3].state < 0.99) {
        /* change garage header */
        val[2].msg = "open";
        severity = MAX(2, severity);
      }
      val[2].msg = "closed";

      if (isnormal(val[0].relh) && val[0].relh > 45.0) severity = MAX(8, severity);
    } while (0);
    setalert(severity);
  }

  String nextword(String &line) {
    /* remove the first floating point number from line */
    line.trim();
    int num = line.indexOf(' ');
    if (num < 1) num = line.length();
    String f = line.substring(0, num);
    line.remove(0, num + 1);
    return f;
  }

  double nextdouble(String &line, int *counter) {
    /* remove the first floating point number from line */
    do {
      String f;
      f = nextword(line);
      if (f.length() < 1) break;
      if (f.equals("?")) break;
      (*counter)++;
      return f.toDouble();
    } while (0);
    return nan("");
  }

  int update(String result) {
    /* create probeinfo from http "measurementlines" string */
    int fields = 0;
    Serial.println(result);
    clear();

    while (result.length() > 0) {
      /* pull the first next line from result (surprisingly complex, might not be robust) */
      int cr = result.indexOf('\n');
      if (cr < 0) cr = result.length();
      String line = result.substring(0, cr + 1);
      result.remove(0, cr + 1);
      line.trim();
      if (line.length() < 1) continue;

      /* parse */
      int colon = line.indexOf(':');
      if (colon < 1) continue;
      String type = line.substring(0, colon);
      line.remove(0, colon + 1);
      line.trim();

      if (type.equals("seconds")) {
        for (int i = 0; i < probes(); i++) {
          auto tmp = nextdouble(line, &fields);
          if (!isnan(tmp)) {
            captime = tmp; /* drops decimal */
            break;
          }
        }
        continue;
      }
      if (type.equals("degC")) {
        for (int i = 0; i < probes(); i++) val[i].degC = nextdouble(line, &fields);
        continue;
      }
      if (type.equals("%rh")) {
        for (int i = 0; i < probes(); i++) val[i].relh = nextdouble(line, &fields);
        continue;
      }
      if (type.equals("hPa")) {
        for (int i = 0; i < probes(); i++) val[i].hPa = nextdouble(line, &fields);
        continue;
      }
      if (type.equals("state")) {
        for (int i = 0; i < probes(); i++) val[i].state = nextdouble(line, &fields);
        continue;
      }
    }
    checkalert();
    return fields;
  }

  void append(int probenum, const probeval *pv) {
    if (probenum < 0 || probenum >= probes()) return;
    val[probenum] = *pv;
    checkalert();
  }
};

class probedisplay {
 public:
  probeinfo &probes;
  bool overlayswap;

  probedisplay(probeinfo &p) : probes{p} {}

  void overlay(OLEDDisplay *display) {
    static char lbuf[16], rbuf[16];
    const char *left, *right;
    if (probes.captime <= 0) {
      sprintf(rbuf, "%d", WiFi.status());
      left = "No connection";
      right = rbuf;
    } else {
      struct tm tm;
      time_t cheat = probes.captime - tzoffset;
      localtime_r(&cheat, &tm);

      /* set brightness from ambient light estimation */
      int minutes = tm.tm_hour * 60 + tm.tm_min;
      if (minutes < 6 * 60)
        display->setContrast(0);
      else if (minutes < 12 * 60)
        display->setContrast(255 * (minutes - 6 * 60) / 6 * 60);
      else if (minutes < 18 * 60)
        display->setContrast(255);
      else
        display->setContrast(255 * (24 * 60 - minutes) / 6 * 60);

      strftime(lbuf, sizeof(lbuf), "%Y-%m-%d", &tm);
      strftime(rbuf, sizeof(rbuf), "%H:%M", &tm);
      left = lbuf;
      right = rbuf;
    }

    /* blink screen */
    unsigned long int timer = millis() / 256;
    if (probes.alert != 0 && (timer % probes.alert) == 0)
      display->invertDisplay();
    else
      display->normalDisplay();

    /* burn-in mitigation */
    overlayswap = ((timer / 256) % 2 == 0) ? false : true;
    ui.setIndicatorPosition(overlayswap ? LEFT : RIGHT);

    /* the overlay */

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(overlayswap ? ArialMT_Plain_16 : ArialMT_Plain_10);
    display->drawString(overlayswap ? 12 : 0, overlayswap ? screenbottom - 16 : 0, String(overlayswap ? right : left));

    display->setTextAlignment(TEXT_ALIGN_RIGHT);
    display->setFont(overlayswap ? ArialMT_Plain_10 : ArialMT_Plain_16);
    display->drawString(overlayswap ? screenright : screenright - 10, overlayswap ? screenbottom - 10 : 0,
                        String(overlayswap ? left : right));
  }

  void probe(OLEDDisplay *display, int16_t x, int16_t y, int i) {
    String header;
    probeval *p;
    if (i < 0 || i > probes.probes()) {
      header = String("Screen ") + i;
      p = NULL;
    } else {
      p = &probes.val[i];
      header = p->name;
      if (p->msg) {
        header += " ";
        header += p->msg;
      }
    }
    y += overlayswap ? 0 : 9;
    x += overlayswap ? 12 : 0;

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(ArialMT_Plain_24);
    display->drawString(x, y, header);
    y += 24;

    if (!p) return;
    y -= 2;
    display->setFont(URW_Palladio_L_Bold_16);
    if (isnormal(p->degC)) {
#ifdef NOHIGHLIGHT
      display->drawString(x, y, String(p->degC, 3) + DEGREES "C");
      y += 16;
#else
      char dec[8], frac[16];
      int cx = x;

      // float deg=p->degC; const char *ffmt="%.3d" DEGREES "C"; float scale=1000;
      float deg = CtoF(p->degC);
      int ddeg = 10 * deg + 0.5;
      sprintf(dec, "%d", ddeg / 10);
      sprintf(frac, "%1d" DEGREES "F", ddeg % 10);

      display->setFont(ArialMT_Plain_24);
      display->drawString(cx, y, dec);
      cx += display->getStringWidth(dec);
      display->fillCircle(cx + 4, y + 10, 2);
      cx += 8;
      display->setFont(URW_Palladio_L_Bold_16);
      display->drawString(cx, y, frac);
      y += 16;
      x += screenright - 12;
      display->setTextAlignment(TEXT_ALIGN_RIGHT);
#endif
    }
    if (isnormal(p->relh)) {
      display->drawString(x, y, String(p->relh, 1) + " %");
      y += 16;
    }
    if (isnormal(p->hPa)) {
      display->drawString(x, y, String(p->hPa, 1) + " hPa");
      y += 16;
    }
    if (isnormal(p->state)) {
      if (p->state == 1.0)
        display->drawString(x, y, "Closed");
      else if (p->state == 0.0)
        display->drawString(x, y, "Open");
      else
        display->drawString(x, y, String(100 * p->state, 0) + " closed");
      y += 16;
    }
  }

  void graph(OLEDDisplay *display, int16_t x, int16_t y) {
    float min = outside.getmin();
    float max = outside.getmax();
    if (!isnormal(min) || !isnormal(max)) {
      display->setTextAlignment(TEXT_ALIGN_CENTER);
      display->setFont(ArialMT_Plain_16);
      display->drawString(x + screenright / 2, y + screenbottom / 2 - 8, "No data");
      return;
    }
    int n = outside.entries();

    /* scaling */
    float ymin = floor(min);
    float ymax = ceil(max);
    float yrange = ymax - ymin;
    if (yrange < 2) {
      ymin = (ymin + ymax) / 2 - 1;
      yrange = 2;
      ymax = ymin + yrange;
    }
    float ydivision = yrange / 4;
    if (ydivision <= 1) {
      ydivision = 1;
    } else if (ydivision <= 2) {
      ydivision = 2;
    } else if (ydivision <= 5) {
      ydivision = 5;
    } else {
      ydivision = 10;
    }

    time_t trange = n * outside.interval;
    time_t tdivision = trange / 4;
    int tspacing;
    if (tdivision <= 60) {
      tdivision = 60;
      tspacing = 16;
    } else if (tdivision <= 5 * 60) {
      tdivision = 5 * 60;
      tspacing = 16;
    } else if (tdivision <= 10 * 60) {
      tdivision = 10 * 60;
      tspacing = 8;
    } else if (tdivision <= 30 * 60) {
      tdivision = 30 * 60;
      tspacing = 8;
    } else {
      tdivision = 60 * 60;
      tspacing = 4;
    }
    time_t tmarker = outside.lastlog - outside.lastlog % tdivision;
    time_t tmin = outside.lastlog - trange;

    x += overlayswap ? 12 : 0;
    y += overlayswap ? 0 : 12;
    int width = n;
    int height = screenbottom - 12;
#define SCALEY(Y) (height - (height * ((Y)-ymin) / yrange))
#define SCALEX(T) (width - (outside.lastlog - (T)) / outside.interval)

    /* graticule */
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(ArialMT_Plain_10);
    for (int temp = int(ymin / ydivision - 1) * ydivision; temp <= int(1 + ymax / ydivision) * ydivision; temp += ydivision) {
      int yy = SCALEY(temp);
      int xspacing;
      if (temp % 10 == 0)
        xspacing = 2;
      else if (temp % 5 == 0)
        xspacing = 4;
      else if (temp % 2 == 0)
        xspacing = 8;
      else
        xspacing = 16;
      if (yy < 0 || yy >= height) continue;
      display->drawString(x, y + SCALEY(temp) - 5, String(temp));
      for (int xx = 0; xx < width; xx += xspacing) display->setPixel(x + xx, y + yy);
    }
    for (time_t tt = tmarker; tt > tmin; tt -= tdivision) {
      int xx = SCALEX(tt);
      if (xx < 0 || xx >= width) continue;
      for (int yy = 0; yy < height; yy += tspacing) display->setPixel(x + xx, y + yy);
    }

    /* draw thick line */
    float lasty = outside[0];
    float lyy = SCALEY(lasty);
    for (int i = 1; i < n; i++) {
      float thisy = outside[i];
      int tyy = SCALEY(thisy);
      if (!isnormal(lasty)) {
        lasty = thisy;
        lyy = tyy;
      }
      if (isnormal(thisy)) {
        int from = tyy, to = lyy;
        if (from > to) {
          from = lyy;
          to = tyy;
        }
        display->drawVerticalLine(x + i, y + from - 1, 3 + to - from);
      }
      lasty = thisy;
      lyy = tyy;
    }
  }
};

probeinfo probes{5};

static String curl(String filepart) {
  HTTPClient http;
  String url = "http://dfsmith.net:8888";
  url += filepart;
  http.begin(url);
  int status = http.GET();
  Serial.printf("GET %s -> %d\n", url.c_str(), status);
  if (status != HTTP_CODE_OK) return "";
  return http.getString();
}

static bool gethttptz() {
  Serial.println("Finding tz offset");
  String lt = curl("/localtime");
  Serial.println(lt);
  lt.trim();
  /* should be "YYYY-MM-DD HH:MM:SS <time_t> <tzoffset>" */
  lt.remove(0, lt.indexOf(' ') + 1); /* remove date */
  lt.remove(0, lt.indexOf(' ') + 1); /* remove time */
  lt.remove(0, lt.indexOf(' ') + 1); /* remove time_t */
  if (lt.length() <= 0) return false;
  int offset = lt.toInt();
  Serial.printf("tzoffset set to %d seconds west\n", offset);
  tzoffset = offset;
  return true;
}

static int getdata() {
  int readings = 0;

  /* read remote sensors */
  readings += probes.update(curl("/measurementlines"));

  /* read onboard sensor */
  probeval pv;
  sensor.read();
  float degc = sensor.last_degc, rh = sensor.last_rh;
  const float bad = sensor.bad_val;
  pv.clear();
  if (degc != bad) {
    pv.degC = degc;
    readings++;
  }
  if (rh != bad) {
    pv.relh = rh;
    readings++;
  }
  probes.append(ONBOARDPROBE, &pv);
  return readings;
}

static bool gethttplocation() {
  if (location.length() > 0) return true;
  if (probenum <= 0) {
    id = String("net:") + WiFi.macAddress();
    String pnum = curl(String("/idprobe/") + String(id));
    if (pnum.length() > 0) probenum = pnum.toInt();
  }
  if (probenum < 0) return false;
  location = curl(String("/location/") + String(probenum));
  location.trim();
  return (location.length() > 0) ? true : false;
}

static String jsonkeyval(String key, String val, bool quoted = true, bool comma = true) {
  String s = String("\"") + key + "\":";
  if (quoted) s += "\"";
  s += val;
  if (quoted) s += "\"";
  if (comma) s += ",";
  return s;
}

static String jsonkeyval(String key, int val, bool comma = true) { return jsonkeyval(key, String(val), false, comma); }

static String jsonkeyval(String key, float val, int dp, bool comma = true) {
  return jsonkeyval(key, String(val, dp), false, comma);
}

static void publishonboardsensor() {
  if (!mqttc.connected()) mqttc.connect("sensor", "sensor", "temperature");

  probeval *p = &probes.val[ONBOARDPROBE];
  String s = "{";
  if (probenum >= 0) s += jsonkeyval("probe", probenum);
  if (isnormal(p->degC)) s += jsonkeyval("degc", p->degC, 2);
  if (isnormal(p->relh)) s += jsonkeyval("rh", p->relh, 1);
  if (location.length() > 0) s += jsonkeyval("location", location);
  if (id.length() > 0) s += jsonkeyval("id", id, true, false);
  s += "}";
  Serial.printf("Onboard sensor: %s\n", s.c_str());

  mqttc.publish("auto/sensor", s.c_str());
}

void checkwork(OLEDDisplayUiState *state) {
  /* see if long-duration work can be run */
  static enum FrameState last = IN_TRANSITION;
  enum FrameState current = state->frameState;
  if (current != FIXED || last != FIXED) {
    last = current;
    return;
  }

  unsigned long int now = millis();

  if (!connected) return;

  ArduinoOTA.handle();

  static unsigned long int updatetz = 0;
  if (now > updatetz) {
    tzknown = false;
    updatetz = now + 10 * 60 * 1000;
  }
  if (!tzknown) tzknown = gethttptz();

  static unsigned long int next = 0, validuntil = 0;
  if (now > next) {
    if (getdata() > 0) {
      validuntil = now + 10 * 60 * 1000;
      next = now + 10 * 1000;
    } else {
      next = now + 1000;
    }
  }
  if (now > validuntil) probes.clear();

  static unsigned long int nextpub = 0;
  if (now > nextpub) {
    gethttplocation();
    publishonboardsensor();
    nextpub = now + 30 * 1000;
  }
}

static void setupOTA() {
  Serial.println("Finding network");
  if (wifiMulti.run() == WL_CONNECTED) {
    Serial.println("WiFi connected");
    connected = true;
  }

  // ArduinoOTA.setPort(3232);
  // ArduinoOTA.setPassword("admin");

  ArduinoOTA
      .onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
          type = "sketch";
        else  // U_SPIFFS
          type = "filesystem";

        // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
        Serial.println("Start updating " + type);
      })
      .onEnd([]() { Serial.println("\nEnd"); })
      .onProgress([](unsigned int progress, unsigned int total) { Serial.printf("Progress: %u%%\r", (progress / (total / 100))); })
      .onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        switch (error) {
          case OTA_AUTH_ERROR:
            Serial.println("Auth Failed");
            break;
          case OTA_BEGIN_ERROR:
            Serial.println("Begin Failed");
            break;
          case OTA_CONNECT_ERROR:
            Serial.println("Connect Failed");
            break;
          case OTA_RECEIVE_ERROR:
            Serial.println("Receive Failed");
            break;
          case OTA_END_ERROR:
            Serial.println("End Failed");
            break;
          default:
            Serial.println("Unknown code");
            break;
        }
      });
  ArduinoOTA.begin();
  Serial.print("OTA IP address: ");
  Serial.println(WiFi.localIP());
}

probedisplay frames{probes};
void drawprobe0(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) {
  frames.probe(display, x, y, 0);
  checkwork(state);
}
void drawprobe1(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) {
  frames.probe(display, x, y, 1);
  checkwork(state);
}
void drawprobe2(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) {
  frames.probe(display, x, y, 2);
  checkwork(state);
}
void drawprobe4(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) {
  frames.probe(display, x, y, 4);
  checkwork(state);
}
void drawgraph(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) {
  frames.graph(display, x, y);
  checkwork(state);
}
void drawoverlay(OLEDDisplay *display, OLEDDisplayUiState *state) { frames.overlay(display); }

vector<FrameCallback> frame = {drawgraph, drawprobe0, drawprobe1, drawprobe2, drawprobe4};
vector<OverlayCallback> overlay = {drawoverlay};

void setup() {
  Serial.begin(115200);
  Serial.println("Starting code");

  oleddisplay.init();
  oleddisplay.setContrast(255);
  oleddisplay.clear();
  oleddisplay.drawRect(screenright / 2 - 10, screenbottom / 2 - 10, 20, 20);
  oleddisplay.display();

  /* display init */
  ui.setTargetFPS(30);
  ui.setActiveSymbol(activeSymbol);
  ui.setInactiveSymbol(inactiveSymbol);
  ui.setIndicatorPosition(RIGHT);
  ui.setIndicatorDirection(LEFT_RIGHT);
  ui.setFrameAnimation(SLIDE_LEFT);
  ui.setFrames(frame.data(), frame.size());
  ui.setOverlays(overlay.data(), overlay.size());
  ui.init();
  oleddisplay.flipScreenVertically();

/* network init */
#include "../../../../smithnetac.h"
  setupOTA();

  /* data init */
  const char *probename[] = {"Box", "Outside", "Garage", "Door", "Here"};
  for (int i = 0; i < lengthof(probename); i++) {
    probes.setname(i, probename[i]);
  }
}

void loop() {
  static unsigned long int wificheck = 30000;
  uint8_t /*wl_status_t*/ wifistatus;

  int remainingTimeBudget = ui.update();
  unsigned long int start = millis();

  wifistatus = wifiMulti.run();
  connected = (wifistatus == WL_CONNECTED) ? true : false;

  /* poor man's watchdog */
  if (!connected && millis() > wificheck) {
    // esp_wifi_wps_disable();
    // ESP.restart();
    WiFi.mode(WIFI_OFF);
    delay(5000);
    WiFi.mode(WIFI_STA);
    wificheck = millis() + 30000;
  }

  unsigned long int stop = millis();
  int delta = stop - start;
  if (delta > remainingTimeBudget)
    Serial.printf("workload %dms of %dms\n", delta, remainingTimeBudget);
  else
    delay(remainingTimeBudget - delta);
}
