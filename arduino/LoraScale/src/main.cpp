#include <Arduino.h>
#include <cmath>
#include <Wifi.h>
#include "SPI.h"
#include "LoRa.h"
#include "HX711.h"
#include "SSD1306.h"

SSD1306 display(0x3C, OLED_SDA, OLED_SCL);
HX711 scale;

constexpr long maxlong = std::numeric_limits<long>::max();
struct LoadSensorPersistent
{
  long zero;
  long reading;
};

struct LoadSensor
{
  float scale;
  LoadSensorPersistent d;
  LoadSensor(float s) : scale(s) {}
  float val() const { return (d.reading - d.zero) * scale; }
  long raw() const { return d.reading; }
  void rezero() { d.zero = maxlong; }
  long checksum() const { return 1 + d.zero + d.reading; }
};

LoadSensor a(0.5e-2 / 1.15);
LoadSensor b(1.0e-2 / 1.15);
RTC_DATA_ATTR LoadSensorPersistent a_store;
RTC_DATA_ATTR LoadSensorPersistent b_store;
RTC_DATA_ATTR long sum_store;
RTC_DATA_ATTR long counter_store;

void read_ab()
{
  digitalWrite(BUILTIN_LED, HIGH);
  scale.set_gain(32);         /* set ch B */
  a.d.reading = scale.read(); /* read ch A */
  scale.set_gain(64);         /* set ch A */
  b.d.reading = scale.read(); /* read ch B */
  digitalWrite(BUILTIN_LED, LOW);
  if (a.d.reading < a.d.zero)
    a.d.zero = a.d.reading;
  if (b.d.reading < b.d.zero)
    b.d.zero = b.d.reading;
}

String json_tail;

void setup()
{
  pinMode(BUILTIN_LED, OUTPUT);
  scale.begin(34, 4, 64);
  scale.power_up();
  scale.read(); /* discard first reading */
  Serial.begin(115200);
  delay(200);
  float vbat = analogRead(35) / 4095.0 * 2 * 3.3 * 1.1;
  while (!Serial)
    ;
  Serial.println("LoraScale");

  a.d = a_store;
  b.d = b_store;
  int counter = counter_store;
  counter--;
  if (counter < 0 || counter > 10)
    counter = 10;
  long csum = a.checksum() + b.checksum();
  if (csum != sum_store)
  {
    a.rezero();
    b.rezero();
  }
  Serial.println(csum);
  Serial.println(sum_store);
  auto last_a_val = a.val();
  auto last_b_val = b.val();

  read_ab();
  scale.power_down();

  a_store = a.d;
  b_store = b.d;
  sum_store = a.checksum() + b.checksum();
  counter_store = counter;
  auto new_a_val = a.val();
  auto new_b_val = b.val();
  auto d_a = last_a_val - new_a_val;
  auto d_b = last_b_val - new_b_val;

    Serial.println("a: " + String(last_a_val) + "->" + String(new_a_val) + " " + String(a.raw()));
    Serial.println("b: " + String(last_b_val) + "->" + String(new_b_val) + " " + String(b.raw()));
    Serial.println("v: " + String(vbat));

  if (counter == 0 || ::fabs(d_a) > 25 || ::fabs(d_b) > 25)
  {
    pinMode(OLED_RST, OUTPUT);
    digitalWrite(OLED_RST, LOW);
    delay(50);
    digitalWrite(OLED_RST, HIGH);
    pinMode(BUILTIN_LED, OUTPUT);
    digitalWrite(BUILTIN_LED, LOW);

    display.init();
    display.displayOn();
    display.flipScreenVertically();
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.clear();
    display.drawString(0, 15, "On");
    display.display();

    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);
    if (!LoRa.begin(915E6))
    {
      Serial.println("LoRa initialization failed");
      ESP.deepSleep(10 * 1000 * 1000);
      ESP.restart();
    }
    display.drawString(0, 30, "LoRa");
    display.display();

    display.drawString(0, 45, "scale read " + String(a.raw()));
    display.display();
    delay(1000);
    json_tail += "\"location\": \"black box\",\n";
    json_tail += "\"id\": \"net:" + WiFi.macAddress() + "\"\n";

    auto wa = a.val();
    auto wb = b.val();

    display.clear();
    display.drawString(0, 0, "scaleA " + String(wa) + "g");
    display.fillRect(0, 0 + 20, a.val(), 4);
    display.drawString(0, 32, "scaleB " + String(wb) + "g");
    display.fillRect(0, 32 + 20, b.val(), 4);
    display.display();

    String msg;
    msg += "{\n";
    msg += "\"g_a\": " + String(wa) + ",\n";
    msg += "\"g_b\": " + String(wb) + ",\n";
    msg += "\"V\": " + String(vbat) + ",\n";
    msg += json_tail;
    msg += "}";

    Serial.println(msg);

    LoRa.beginPacket();
    LoRa.print(msg);
    LoRa.endPacket();

    delay(500);
    display.displayOff();
  }
}

void loop()
{
  esp_sleep_enable_timer_wakeup(10 * 1000000);
  esp_deep_sleep_start();
}
