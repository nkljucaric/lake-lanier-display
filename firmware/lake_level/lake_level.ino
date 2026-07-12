// Milestone 4: pull live lake elevation from USGS, parse it, and render
// just the number on the panel. Still separate from the full A/B/C
// layout — that comes once inflow/outflow/weather are layered in too.
//
// Display/pin setup mirrors firmware/hello_world/hello_world.ino.
// WiFi join mirrors firmware/wifi_connect/wifi_connect.ino (WiFiMulti +
// gitignored secrets.h — copy secrets.h.example and fill in credentials).
//
// USGS gauge 02334400 (Buford Dam / Lake Lanier), parameter 00062
// ("Elevation of reservoir water surface above datum, ft"). Endpoint
// redirects http -> https (HSTS), so this uses WiFiClientSecure with
// setInsecure() rather than pinning a root CA — acceptable for this
// prototype's scope.

#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold24pt7b.h>
#include "secrets.h"

GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT> display(
    GxEPD2_420_GDEY042T81(/*CS=*/15, /*DC=*/27, /*RST=*/26, /*BUSY=*/25));

SPIClass hspi(HSPI);
WiFiMulti wifiMulti;

const char *USGS_URL =
    "https://waterservices.usgs.gov/nwis/iv/?sites=02334400&parameterCd=00062&format=json";

void connectWiFi()
{
  for (const WifiNetwork &network : WIFI_NETWORKS)
  {
    wifiMulti.addAP(network.ssid, network.password);
  }

  Serial.println("lake_level: connecting to WiFi...");
  while (wifiMulti.run() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected, IP: ");
  Serial.println(WiFi.localIP());
}

bool fetchElevation(String &elevationOut)
{
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, USGS_URL);
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK)
  {
    Serial.printf("GET failed, HTTP %d\n", httpCode);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err)
  {
    Serial.print("JSON parse failed: ");
    Serial.println(err.c_str());
    return false;
  }

  JsonArray values = doc["value"]["timeSeries"][0]["values"][0]["value"];
  if (values.size() == 0)
  {
    Serial.println("No values in response");
    return false;
  }

  elevationOut = values[0]["value"].as<const char *>();
  return true;
}

void renderElevation(const String &elevation)
{
  hspi.begin(13, 12, 14, 15);
  display.epd2.selectSPI(hspi, SPISettings(4000000, MSBFIRST, SPI_MODE0));
  display.init(115200);
  display.setRotation(0);
  display.setTextColor(GxEPD_BLACK);

  display.setFullWindow();
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);

    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(20, 30);
    display.print("LAKE LANIER ELEVATION");

    display.setFont(&FreeMonoBold24pt7b);
    display.setCursor(20, 110);
    display.print(elevation);
    display.setFont(&FreeMonoBold9pt7b);
    display.print(" ft");
  } while (display.nextPage());

  display.hibernate();
}

void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("lake_level: starting");

  connectWiFi();

  String elevation;
  if (fetchElevation(elevation))
  {
    Serial.print("Elevation: ");
    Serial.println(elevation);
    renderElevation(elevation);
  }
  else
  {
    Serial.println("Failed to fetch elevation");
  }
}

void loop()
{
  // nothing to do - one-shot fetch and render
}
