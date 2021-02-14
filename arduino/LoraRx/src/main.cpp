#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "SPI.h"
#include "LoRa.h"
#include "SSD1306.h"

#define NOMULTI
#include "../../../../smithnetac.h"

SSD1306 display(0x3C, OLED_SDA, OLED_SCL);

WiFiClient espClient;
PubSubClient mqttc("brick.dfsmith.net", 1883, espClient);

void setup()
{
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW);
  delay(50);
  digitalWrite(OLED_RST, HIGH);
  pinMode(BUILTIN_LED, OUTPUT);
  digitalWrite(BUILTIN_LED, LOW);

  Serial.begin(115200);
  while (!Serial)
    ;
  Serial.println("LoraRx");

  display.init();
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

  LoRa.receive();

  WiFi.begin(SMITHNET);
  while(WiFi.status() != WL_CONNECTED)
  {
    Serial.println("Connecting to WiFi");
    delay(500);
  }
  display.drawString(0, 45, WiFi.localIP().toString());
  display.display();
  delay(1000);
}

static void mqttsend(String msg) {
  if (!mqttc.connected())
    mqttc.connect("sensor","sensor","temperature");
  
  mqttc.publish("auto/sensor",msg.c_str());
}

String LoRaData;

void loop()
{
  int packetSize = LoRa.parsePacket();
  if (packetSize)
  {
    Serial.print("Received packet ");
    while (LoRa.available())
    {
      LoRaData = LoRa.readString();
      Serial.print(LoRaData);
    }

    //print RSSI of packet
    int rssi = LoRa.packetRssi();
    Serial.print(" with RSSI ");
    Serial.println(rssi);

    // Dsiplay information
    display.clear();
    display.drawString(0, 0, "RSSI" + String(rssi));
    display.drawString(0, 15, LoRaData);
    display.display();

    String msg;
    msg+="{\n";
    msg+="  \"lora\": {\n";
    msg+="    \"RSSI\": "+String(rssi)+",\n";
    msg+="    \"data\": "+LoRaData+"\n";
    msg+="  }\n";
    msg+="}\n";
    mqttsend(msg);
  }
}
