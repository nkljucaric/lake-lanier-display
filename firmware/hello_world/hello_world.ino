// Milestone 1: get any text rendering on the physical panel.
// No WiFi, no APIs — just proves the panel/driver board/wiring works.
//
// Hardware: Waveshare 4.2" e-Paper (400x300, B/W) on Waveshare's
// Universal e-Paper Driver Board (onboard ESP32).
//
// Panel driver: GxEPD2_420 assumes the GDEW042T2 (UC8176/IL0398) panel,
// the common Waveshare 4.2" V1/V2 panel. If the panel that arrives is
// labeled otherwise, swap GxEPD2_420 below for the matching class in
// GxEPD2_WS_ESP32_Driver.ino's panel list.
//
// Pin mapping and HSPI remap are fixed by the driver board's PCB
// (Waveshare: BUSY=25, RST=26, DC=27, CS=15, CLK=13, DIN=14).

#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>

GxEPD2_BW<GxEPD2_420, GxEPD2_420::HEIGHT> display(
    GxEPD2_420(/*CS=*/15, /*DC=*/27, /*RST=*/26, /*BUSY=*/25));

SPIClass hspi(HSPI);

void setup()
{
  Serial.begin(115200);
  Serial.println("hello_world: starting");

  // Waveshare's driver board swaps SCK/MOSI vs standard HSPI pins.
  hspi.begin(13, 12, 14, 15);
  display.epd2.selectSPI(hspi, SPISettings(4000000, MSBFIRST, SPI_MODE0));

  display.init(115200);
  display.setRotation(0);
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(GxEPD_BLACK);

  display.setFullWindow();
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);
    display.setCursor(20, 40);
    display.print("Lake Lanier Display");
    display.setCursor(20, 70);
    display.print("hello world - it works");
  } while (display.nextPage());

  Serial.println("hello_world: done");
  display.hibernate();
}

void loop()
{
  // nothing to do - e-paper holds the image with no power
}
