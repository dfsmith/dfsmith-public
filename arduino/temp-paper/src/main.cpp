#include <Arduino.h>
#include <time.h>

#include <GxEPD.h>
#include <GxIO/GxIO_SPI/GxIO_SPI.h>
#include <GxIO/GxIO.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include "Fonts/Open_Sans_Bold_92pt.h"

#include <WifiMulti.h>
#include <HTTPClient.h>
#include <PubSubClient.h>

#include <sht-object.h>

#include "board_def.h"

GxIO_Class io(SPI, ELINK_SS, ELINK_DC, ELINK_RESET);
GxEPD_Class epd(io, ELINK_RESET, ELINK_BUSY); /* electronic paper display */

sht7x sensor(26,25,3.3);
String id="";
String location="";
int probenum=-1;
bool tzknown=false;
time_t tzoffset=0;
String current_source="";
time_t current_time=0;
float current_degc=sensor.bad_val;
float current_rh=sensor.bad_val;

WiFiClient espClient;
PubSubClient mqttc("brick.dfsmith.net",1883,espClient);
WiFiMulti wifiMulti;

static String curl(String filepart) {
        HTTPClient http;
        String url="http://dfsmith.net:8888";
        url+=filepart;
        http.begin(url);
        int status=http.GET();
        Serial.printf("GET %s -> %d\n",url.c_str(),status);
        if (status!=HTTP_CODE_OK) return "";
        String val=http.getString();
        val.trim();
        return val;
}

static bool gethttp_time() {
        Serial.println("Finding tz offset");
        String lt=curl("/localtime");
        Serial.println(lt);
        lt.trim();
        /* should be "YYYY-MM-DD HH:MM:SS <time_t> <tzoffset>" */
        lt.remove(0,lt.indexOf(' ')+1); /* remove date */
        lt.remove(0,lt.indexOf(' ')+1); /* remove time */
        time_t t=lt.toInt();
        lt.remove(0,lt.indexOf(' ')+1); /* remove time_t */
        if (lt.length()<=0) return false;
        int offset=lt.toInt();
        Serial.printf("tzoffset set to %d seconds west\n",offset);
        current_time=t;
        tzoffset=offset;
        return true;
}

static bool gethttp_location() {
        if (location.length()>0 || probenum>=0) return true;
        if (id.length()<=0)
                return false;
        if (probenum <= 0) {
                String pnum=curl(String("/idprobe/")+String(id));
                if (pnum.length()>0) probenum=pnum.toInt();
        }
        if (location.length()<=0 && probenum>=0) {
                location=curl(String("/location/")+String(probenum));
        }
        return (location.length()>0 || probenum>=0)?true:false;
}

static bool gethttp_th() {
        String outside=curl("/probe/1");
        Serial.println(outside);
        outside.trim();
        current_time=outside.toDouble();
        outside.remove(0,outside.indexOf(' ')+1);
        current_degc=outside.toDouble();
        outside.remove(0,outside.indexOf(' ')+1);
        current_rh=outside.toDouble();
        outside.remove(0,outside.indexOf(' ')+1);
        if (outside.length()<=0) return false;
        current_source="probe/1";
        return true;
}

static bool get_th() {
        const float bad=sensor.bad_val;
        sensor.read();
        current_degc=sensor.last_degc;
        current_rh=sensor.last_rh;
        if (current_degc==bad || current_rh==bad) return false;
        current_source="onboard";
        return true;
}

static String jsonkeyval(String key,String val,bool quoted=true,bool comma=true) {
        String s=String("\"")+key+"\":";
        if (quoted) s+="\"";
        s+=val;
        if (quoted) s+="\"";
        if (comma) s+=",";
        return s;
}

static String jsonkeyval(String key,const int val,bool comma=true) {
        return jsonkeyval(key,String(val),false,comma);
}

static String jsonkeyval(String key,const float val,int dp,bool comma=true) {
        return jsonkeyval(key,String(val,dp),false,comma);
}

static void publishonboardsensor() {
        const float bad=sensor.bad_val;

        if (!mqttc.connected())
                mqttc.connect("sensor","sensor","temperature");

        String s="{";
        if (probenum>=0)         s+=jsonkeyval("probe",probenum);
        if (current_degc!=bad)   s+=jsonkeyval("degc",current_degc,2);
        if (current_rh!=bad)     s+=jsonkeyval("rh",current_rh,1);
        if (location.length()>0) s+=jsonkeyval("location",location);
        if (id.length()>0)       s+=jsonkeyval("id",id,true,false);
        s+="}";
        Serial.printf("Onboard sensor: %s\n",s.c_str());

        mqttc.publish("auto/sensor",s.c_str());
}

int dimensionsOf(String s,int *height=nullptr) {
        int16_t x1,y1;
        uint16_t w,h;
        epd.getTextBounds(s,0,0,&x1,&y1,&w,&h);
        if (height) *height=h;
        return w;
}

void show_th() {
        int w1;

        Serial.printf("Displaying %.2fdegC %.1f%%rh\n",current_degc,current_rh);
        epd.init();
        epd.setRotation(3);
        epd.eraseDisplay();
        epd.setTextColor(GxEPD_BLACK);
        epd.setTextWrap(false);
        Serial.printf("Screen %d x %d\n",epd.width(),epd.height());

        if (current_degc>-1000) {
                String s_degf=String((current_degc*9)/5+32,0);
                epd.setFont(&Open_Sans_Bold_92);
                w1=dimensionsOf(s_degf);
                epd.setCursor(0,80);
                epd.print(s_degf);
                for(int r=10;r>5;r--)
                        epd.drawCircle(w1+20,20,r,GxEPD_BLACK);
        }

        if (current_rh>-1000) {
                String s_rh=String(current_rh,0);
                epd.setFont(&Open_Sans_Bold_92);
                w1=dimensionsOf(s_rh);
                epd.setCursor(epd.width()-w1-5,epd.height()-10);
                epd.print(s_rh);

                epd.setFont(&FreeSansBold24pt7b);
                epd.setCursor(epd.width()-42,35);
                epd.print('%');
        }

        if (current_time>0) {
                struct tm tm;
                time_t cheat=current_time - tzoffset;
                localtime_r(&cheat,&tm);

                epd.setFont();
                epd.setCursor(0,epd.height()-10);
                char buf[32];
                strftime(buf,sizeof(buf),"%Y-%m-%d %H:%M",&tm);
                epd.print(buf);
        }

        if (current_source.length()>0) {
                epd.setFont();
                epd.setCursor(0,epd.height()-20);
                epd.print(current_source);
        }

        epd.update();
}

void do_work() {
        get_th();
        gethttp_location();
        gethttp_time();
        publishonboardsensor();

        if (current_source.length()<=0)
                gethttp_th(); /* fallback in case no onboard sensor */
        show_th();
}

void setup() {
        Serial.begin(115200);
        Serial.println("starting");

        #include "../../../../smithnetac.h"
        while(wifiMulti.run()!=WL_CONNECTED)
                delay(5000);
        Serial.println(WiFi.localIP());
        id=String("net:")+WiFi.macAddress();

        do_work();
}

void loop() {
        Serial.println("Sleeping");
        esp_sleep_enable_timer_wakeup(10*60*1000000);
        esp_deep_sleep_start();
}
