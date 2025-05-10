/* Read SPI data from ST Electronics M95320WQ EEPROM chip. */
/* Platform Wemos LOLIN32 (with OLED 128x64). */
/* See https://randomnerdtutorials.com/esp32-built-in-oled-ssd1306/ for pins. */

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

struct SpiPins {
  // HSPI pins for reference
  static constexpr byte mosi = 13;
  static constexpr byte miso = 12;
  static constexpr byte sclk = 14;
  static constexpr byte cs = 15;
};

SPIClass spi;
Adafruit_SSD1306 display(128, 64, &Wire, -1);

void testdrawchar(void) {
  display.clearDisplay();
  display.setTextSize(1);               // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE);  // Draw white text
  display.setCursor(0, 0);              // Start at top-left corner
  display.cp437(true);  // Use full 256 char 'Code Page 437' font

  // Not all the characters will fit on the display. This is normal.
  // Library will draw what it can and the rest will be clipped.
  for (int16_t i = 0; i < 256; i++) {
    if (i == '\n')
      display.write(' ');
    else
      display.write(i);
  }

  display.display();
  delay(2000);
}

void testdrawstyles(void) {
  display.clearDisplay();

  display.setTextSize(1);               // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE);  // Draw white text
  display.setCursor(0, 0);              // Start at top-left corner
  display.println(F("Hello, world!"));

  display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);  // Draw 'inverse' text
  display.println(3.141592);

  display.setTextSize(2);  // Draw 2X-scale text
  display.setTextColor(SSD1306_WHITE);
  display.print(F("0x"));
  display.println(0xDEADBEEF, HEX);

  display.display();
  delay(2000);
}

void dump(size_t addr, byte *data, size_t size) {
  for (int d = 12; d >= 0; d -= 4) Serial.print((addr >> d) & 0xF, HEX);
  Serial.print(":");
  for (byte i = 0; i < size; i++) {
    Serial.print(" ");
    if (data[0] < 16) Serial.print(0, HEX);
    Serial.print(data[i], HEX);
  }
  Serial.println("");
}

struct Eeprom95320 {
  enum Op {
    WRSR = 0x01,   // write status register
    WRITE = 0x02,  // write
    READ = 0x03,   // read
    WRDI = 0x04,   // write disable
    RDSR = 0x05,   // read status register
    WREN = 0x06,   // write enable
    RDID = 0x83,   // D variant
    WRID = 0x82,   // D variant
    RDLS = 0x83,   // overlap, separated by address
    LID = 0x82,    // overlap, separated by address
  };

  void cs(bool enable) {
    delayMicroseconds(1);
    digitalWrite(spi.pinSS(), enable ? LOW : HIGH);
    delayMicroseconds(1);
  }

  void read(size_t addr, byte *data, size_t size) {
    /* Read 95320 */
    cs(true);
    spi.transfer(Op::READ);
    spi.transfer16(addr);
    spi.transfer(data, size);
    cs(false);
  }

  struct Status {
    bool wip;       // write in progress
    bool wel;       // write enable latch bit
    bool bp1, bp0;  // block protect bits
    bool srwd;      // status register write protect

    enum Mask {
      WIP = 0x01,
      WEL = 0x02,
      BP0 = 0x03,
      BP1 = 0x04,
      SRWD = 0x80,
    };

    struct AddrRange {
      size_t low;
      size_t high;
    };

    AddrRange protected_addr() const {
      static constexpr AddrRange protected_range[] = {
          {0, 0}, {0xC00, 0x10000}, {0x800, 0x10000}, {0, 0x10000}};
      return protected_range[(bp1 ? 2 : 0) | (bp0 ? 1 : 0)];
    }

    Status(byte status) {
      wip = !!(status & Mask::WIP);
      wel = !!(status & Mask::WEL);
      bp0 = !!(status & Mask::BP0);
      bp1 = !!(status & Mask::BP1);
      srwd = !!(status & Mask::SRWD);
    }

    byte value() const {
      return (wip ? Mask::WIP : 0) | (wel ? Mask::WEL : 0) |
             (bp0 ? Mask::BP0 : 0) | (bp1 ? Mask::BP1 : 0) |
             (srwd ? Mask::SRWD : 0);
    }

    void print() const {
      auto out = [](char const *name, bool val) {
        Serial.print(name);
        Serial.print("=");
        Serial.print(val);
      };
      out("WIP", wip);
      out(",WEL", wel);
      out(",BP0", bp0);
      out(",BP1", bp1);
      out(",SRWD", srwd);
    }
  };

  Status read_status() {
    cs(true);
    spi.transfer(Op::RDSR);
    Status s(spi.transfer(0));
    cs(false);
    return s;
  }

  void write_status(Status const &s) {
    /* Need write_on(). */
    cs(true);
    spi.transfer(Op::WRSR);
    spi.transfer(s.value());
    cs(false);
  }

  void write_en() {
    cs(true);
    spi.transfer(Op::WREN);
    cs(false);
  }

  void write_dis() {
    cs(true);
    spi.transfer(Op::WRDI);
    cs(false);
  }

  void write(size_t addr, byte *data, size_t size) {
    /* Need write_en(). */
    cs(true);
    spi.transfer(Op::WRITE);
    spi.transfer16(addr);
    spi.transfer(data, size);
    cs(false);
    while (read_status().wip) delayMicroseconds(10);
  }

  SPIClass &spi;

  Eeprom95320(SPIClass &spi_dev) : spi(spi_dev) {}

  void begin() {
    spi.setFrequency(100000); /* up to 20MHz */
    spi.setDataMode(SPI_MODE0);
    spi.setBitOrder(MSBFIRST);
    spi.setHwCs(false);
    cs(false);
    pinMode(spi.pinSS(), OUTPUT);
  }
};
Eeprom95320 ee(spi);

void setup() {
  Serial.begin(115200);
  Wire.begin(5, 4);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3c)) {
    Serial.println(F("SSD1306 allocation failed: halted"));
    for (;;);
  }
  spi.begin();
  ee.begin();
}

void loop() {
  display.clearDisplay();
  testdrawchar();
  testdrawstyles();
  display.clearDisplay();
  display.display();

  byte buffer[32];
  Serial.println("read 4KB");
  ee.read_status().print();
  for (size_t addr = 0; addr < 4096; addr += 32) {
    ee.read(addr, buffer, sizeof(buffer));
    dump(addr, buffer, sizeof(buffer));
  }
  delay(5000);
}
