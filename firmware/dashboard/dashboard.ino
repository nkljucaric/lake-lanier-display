// Milestone 5: full A/B/C dashboard layout on the 400x300 panel.
//
// Pulls together everything from milestones 1-4 plus three new sources:
//   - USGS dv service (02334400, 00062) for the 30-day elevation history
//   - USGS gauge 02334430 (Chattahoochee at Buford Dam) for outflow
//   - USGS gauges 02331000 (Chattahoochee near Leaf) + 02333500
//     (Chestatee near Dahlonega) summed together for inflow - these are
//     the two tributaries feeding the lake, same approach USGS itself
//     describes for estimating inflow
//   - Open-Meteo for current/forecast weather and 24h rain
//
// USACE's CWMS API was tried first for inflow/outflow (it publishes
// pre-computed Buford.Flow-In/Flow-Out series) but its catalog showed
// both series' last-update stuck at 2026-07-09 - several days stale -
// so this uses the USGS gauges above instead, which update every 15 min
// same as the elevation gauge.
//
// Display/pin setup and WiFi join mirror firmware/lake_level/lake_level.ino.
//
// Flash note: this sketch compiles to 94% of the default 1.2MB app
// partition - too little headroom for future work (bigger panel, more
// fonts/icons). Build with PartitionScheme=huge_app (3MB app, no OTA -
// fine for a one-shot fetch-and-render sketch) instead, which brings it
// down to 39%:
//   arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app firmware/dashboard
//   arduino-cli upload --fqbn esp32:esp32:esp32:PartitionScheme=huge_app -p <port> firmware/dashboard

#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold24pt7b.h>
#include "secrets.h"

GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT> display(
    GxEPD2_420_GDEY042T81(/*CS=*/15, /*DC=*/27, /*RST=*/26, /*BUSY=*/25));

SPIClass hspi(HSPI);
WiFiMulti wifiMulti;

const char *USGS_IV_URL =
    "https://waterservices.usgs.gov/nwis/iv/?sites=02334400&parameterCd=00062&format=json";
const char *USGS_DV_URL =
    "https://waterservices.usgs.gov/nwis/dv/?sites=02334400&parameterCd=00062&statCd=00003&period=P30D&format=json";
// USGS's daily-statistics service: mean of daily-mean elevation for each
// calendar day across the full period of record - i.e. "the historical
// average for this time of year." RDB (tab-delimited), not JSON.
const char *USGS_STAT_URL =
    "https://waterservices.usgs.gov/nwis/stat/?sites=02334400&parameterCd=00062&statReportType=daily&statTypeCd=mean&format=rdb";
const char *USGS_OUTFLOW_SITE = "02334430";  // Chattahoochee River at Buford Dam
const char *USGS_INFLOW_SITE_1 = "02331000"; // Chattahoochee River near Leaf
const char *USGS_INFLOW_SITE_2 = "02333500"; // Chestatee River near Dahlonega
const char *USGS_FLOW_PARAM = "00060";       // Streamflow, ft3/s

const double LAKE_LAT = 34.16;
const double LAKE_LON = -84.073;

const int MAX_HISTORY = 31;

struct DashboardData
{
  bool haveElevation = false;
  float elevation = 0;

  bool haveHistory = false;
  float history[MAX_HISTORY];
  int historyCount = 0;

  bool haveInflow = false;
  float inflow = 0;
  bool haveOutflow = false;
  float outflow = 0;

  bool haveWeather = false;
  int todayCode = 0;
  int tomorrowCode = 0;
  float rain24hIn = 0;

  bool haveSeasonalAvg = false;
  float seasonalAvgFt = 0;

  float fullPoolFt = 1071.0;
  char dateStr[16] = "";
  char shortDateStr[8] = "";
  char updatedStr[12] = "";
};

// Lake Lanier's full pool is seasonal: 1071 ft May 1 - Nov 30 (summer),
// 1070 ft Dec 1 - Apr 30 (winter). monthIndex is 0=Jan..11=Dec.
float fullPoolForMonth(int monthIndex)
{
  return (monthIndex >= 4 && monthIndex <= 10) ? 1071.0 : 1070.0;
}

const char *weatherCodeToText(int code)
{
  switch (code)
  {
  case 0:
    return "Clear";
  case 1:
    return "Mostly Clear";
  case 2:
    return "Partly Cloudy";
  case 3:
    return "Overcast";
  case 45:
  case 48:
    return "Fog";
  case 51:
  case 53:
  case 55:
    return "Drizzle";
  case 56:
  case 57:
    return "Fz Drizzle";
  case 61:
  case 63:
  case 65:
    return "Rain";
  case 66:
  case 67:
    return "Fz Rain";
  case 71:
  case 73:
  case 75:
    return "Snow";
  case 77:
    return "Snow Grains";
  case 80:
  case 81:
  case 82:
    return "Showers";
  case 85:
  case 86:
    return "Snow Showers";
  case 95:
    return "T-Storms";
  case 96:
  case 99:
    return "T-Storms/Hail";
  default:
    return "Unknown";
  }
}

void connectWiFi()
{
  for (const WifiNetwork &network : WIFI_NETWORKS)
  {
    wifiMulti.addAP(network.ssid, network.password);
  }

  Serial.println("dashboard: connecting to WiFi...");
  while (wifiMulti.run() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected, IP: ");
  Serial.println(WiFi.localIP());
}

void syncTime()
{
  // Georgia is Eastern time; TZ string keeps DST correct year-round.
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);
  tzset();

  struct tm timeinfo;
  int attempts = 0;
  while (!getLocalTime(&timeinfo) && attempts < 20)
  {
    delay(500);
    attempts++;
  }
  if (attempts >= 20)
  {
    Serial.println("NTP sync failed, date/full-pool season may be wrong");
  }
}

bool httpGetJson(const char *url, JsonDocument &doc, const char *acceptHeader = nullptr)
{
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, url);
  if (acceptHeader)
  {
    http.addHeader("Accept", acceptHeader);
  }

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK)
  {
    Serial.printf("GET failed (%d): %s\n", httpCode, url);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  DeserializationError err = deserializeJson(doc, payload);
  if (err)
  {
    Serial.print("JSON parse failed: ");
    Serial.println(err.c_str());
    return false;
  }
  return true;
}

bool fetchElevationCurrent(DashboardData &d)
{
  JsonDocument doc;
  if (!httpGetJson(USGS_IV_URL, doc))
  {
    return false;
  }

  JsonArray values = doc["value"]["timeSeries"][0]["values"][0]["value"];
  if (values.size() == 0)
  {
    return false;
  }

  d.elevation = values[0]["value"].as<float>();
  d.haveElevation = true;
  return true;
}

bool fetchElevationHistory(DashboardData &d)
{
  JsonDocument doc;
  if (!httpGetJson(USGS_DV_URL, doc))
  {
    return false;
  }

  JsonArray values = doc["value"]["timeSeries"][0]["values"][0]["value"];
  int n = 0;
  for (JsonObject v : values)
  {
    if (n >= MAX_HISTORY)
    {
      break;
    }
    d.history[n++] = v["value"].as<float>();
  }
  d.historyCount = n;
  d.haveHistory = n > 1;
  return d.haveHistory;
}

bool fetchUsgsInstantFlow(const char *siteId, float &out)
{
  String url = "https://waterservices.usgs.gov/nwis/iv/?sites=" + String(siteId) +
               "&parameterCd=" + USGS_FLOW_PARAM + "&format=json";

  JsonDocument doc;
  if (!httpGetJson(url.c_str(), doc))
  {
    return false;
  }

  JsonArray values = doc["value"]["timeSeries"][0]["values"][0]["value"];
  if (values.size() == 0)
  {
    return false;
  }

  out = values[0]["value"].as<float>();
  return true;
}

bool fetchOutflow(float &out)
{
  return fetchUsgsInstantFlow(USGS_OUTFLOW_SITE, out);
}

bool fetchInflow(float &out)
{
  float a, b;
  if (!fetchUsgsInstantFlow(USGS_INFLOW_SITE_1, a) || !fetchUsgsInstantFlow(USGS_INFLOW_SITE_2, b))
  {
    return false;
  }
  out = a + b;
  return true;
}

bool fetchWeather(DashboardData &d)
{
  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(LAKE_LAT, 4) +
               "&longitude=" + String(LAKE_LON, 4) +
               "&daily=weather_code,precipitation_sum" +
               "&precipitation_unit=inch" +
               "&timezone=America%2FNew_York&past_days=1&forecast_days=2";

  JsonDocument doc;
  if (!httpGetJson(url.c_str(), doc))
  {
    return false;
  }

  JsonArray dailyCode = doc["daily"]["weather_code"];
  JsonArray dailyRain = doc["daily"]["precipitation_sum"];
  // past_days=1 + forecast_days=2 -> [yesterday, today, tomorrow]
  if (dailyCode.size() < 3)
  {
    return false;
  }

  d.rain24hIn = dailyRain[0].as<float>();
  d.todayCode = dailyCode[1].as<int>();
  d.tomorrowCode = dailyCode[2].as<int>();
  d.haveWeather = true;
  return true;
}

// Historical average elevation for a given calendar day, parsed out of
// USGS's tab-delimited daily-statistics file. Field layout (0-indexed):
// agency_cd, site_no, parameter_cd, ts_id, loc_web_ds, month_nu, day_nu,
// begin_yr, end_yr, count_nu, mean_va.
String tabField(const String &line, int index)
{
  int start = 0;
  for (int i = 0; i < index; i++)
  {
    int tab = line.indexOf('\t', start);
    if (tab == -1)
    {
      return "";
    }
    start = tab + 1;
  }
  int end = line.indexOf('\t', start);
  return end == -1 ? line.substring(start) : line.substring(start, end);
}

bool fetchSeasonalAverageElevation(int month, int day, float &out)
{
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, USGS_STAT_URL);
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK)
  {
    Serial.printf("GET failed (%d): %s\n", httpCode, USGS_STAT_URL);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  String monthStr = String(month);
  String dayStr = String(day);
  bool found = false;

  int lineStart = 0;
  while (lineStart < (int)payload.length())
  {
    int lineEnd = payload.indexOf('\n', lineStart);
    if (lineEnd == -1)
    {
      lineEnd = payload.length();
    }
    String line = payload.substring(lineStart, lineEnd);
    lineStart = lineEnd + 1;

    if (!line.startsWith("USGS\t02334400\t00062\t"))
    {
      continue;
    }
    if (tabField(line, 5) == monthStr && tabField(line, 6) == dayStr)
    {
      out = tabField(line, 10).toFloat();
      found = true;
      break;
    }
  }
  return found;
}

void printRightAligned(const char *s, int rightX, int y, const GFXfont *font)
{
  display.setFont(font);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(rightX - w, y);
  display.print(s);
}

void drawSparkline(const float *values, int count, int x, int y, int w, int h)
{
  if (count < 2)
  {
    return;
  }

  float minV = values[0], maxV = values[0];
  for (int i = 1; i < count; i++)
  {
    if (values[i] < minV)
      minV = values[i];
    if (values[i] > maxV)
      maxV = values[i];
  }
  if (maxV - minV < 0.1)
  {
    maxV = minV + 0.1; // avoid a divide-by-zero flat line
  }

  int prevX = x;
  int prevY = y + h - (int)((values[0] - minV) / (maxV - minV) * h);
  for (int i = 1; i < count; i++)
  {
    int px = x + (int)((float)i / (count - 1) * w);
    int py = y + h - (int)((values[i] - minV) / (maxV - minV) * h);
    display.drawLine(prevX, prevY, px, py, GxEPD_BLACK);
    prevX = px;
    prevY = py;
  }
}

void renderDashboard(const DashboardData &d)
{
  hspi.begin(13, 12, 14, 15);
  display.epd2.selectSPI(hspi, SPISettings(4000000, MSBFIRST, SPI_MODE0));
  display.init(115200);
  display.setRotation(0);
  display.setTextColor(GxEPD_BLACK);

  char buf[48];

  display.setFullWindow();
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);

    // --- Header ---
    display.setFont(&FreeMonoBold12pt7b);
    display.setCursor(10, 22);
    display.print("LAKE LANIER");
    printRightAligned(d.dateStr, 390, 22, &FreeMonoBold9pt7b);
    display.drawLine(10, 30, 390, 30, GxEPD_BLACK);

    // --- A priority: elevation ---
    display.setFont(&FreeMonoBold24pt7b);
    display.setCursor(10, 80);
    if (d.haveElevation)
    {
      snprintf(buf, sizeof(buf), "%.2f", d.elevation);
    }
    else
    {
      snprintf(buf, sizeof(buf), "--.--");
    }
    display.print(buf);
    display.setFont(&FreeMonoBold12pt7b);
    display.print(" ft");

    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(10, 100);
    if (d.haveElevation)
    {
      float delta = d.elevation - d.fullPoolFt;
      snprintf(buf, sizeof(buf), "%+.2f ft vs full pool", delta);
    }
    else
    {
      snprintf(buf, sizeof(buf), "vs full pool");
    }
    display.print(buf);

    display.setCursor(10, 120);
    if (d.haveElevation && d.haveSeasonalAvg)
    {
      float delta = d.elevation - d.seasonalAvgFt;
      snprintf(buf, sizeof(buf), "%+.2f ft vs %s avg", delta, d.shortDateStr);
    }
    else
    {
      snprintf(buf, sizeof(buf), "vs average for this time of year");
    }
    display.print(buf);

    display.drawLine(10, 128, 390, 128, GxEPD_BLACK);

    // --- 30-day sparkline ---
    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(10, 142);
    display.print("30-DAY TREND");
    if (d.haveHistory)
    {
      drawSparkline(d.history, d.historyCount, 10, 150, 380, 28);
    }

    display.drawLine(10, 186, 390, 186, GxEPD_BLACK);

    // --- B priority: inflow / outflow / rain 24h ---
    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(10, 202);
    display.print("INFLOW");
    display.setCursor(150, 202);
    display.print("OUTFLOW");
    display.setCursor(290, 202);
    display.print("RAIN 24H");

    display.setFont(&FreeMonoBold12pt7b);
    display.setCursor(10, 224);
    display.print(d.haveInflow ? String((int)d.inflow) + " cfs" : String("-- cfs"));
    display.setCursor(150, 224);
    display.print(d.haveOutflow ? String((int)d.outflow) + " cfs" : String("-- cfs"));
    display.setCursor(290, 224);
    display.print(d.haveWeather ? String(d.rain24hIn, 2) + " in" : String("-- in"));

    display.drawLine(10, 234, 390, 234, GxEPD_BLACK);

    // --- C priority: today's + tomorrow's weather (stacked) ---
    display.setFont(&FreeMonoBold12pt7b);
    display.setCursor(10, 250);
    display.print("TODAY ");
    display.setFont(&FreeMonoBold9pt7b);
    display.print(d.haveWeather ? weatherCodeToText(d.todayCode) : "--");

    display.setFont(&FreeMonoBold12pt7b);
    display.setCursor(10, 270);
    display.print("TOMORROW ");
    display.setFont(&FreeMonoBold9pt7b);
    display.print(d.haveWeather ? weatherCodeToText(d.tomorrowCode) : "--");

    // --- Footer ---
    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(10, 290);
    snprintf(buf, sizeof(buf), "Updated %s", d.updatedStr);
    display.print(buf);

  } while (display.nextPage());

  display.hibernate();
}

void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("dashboard: starting");

  connectWiFi();
  syncTime();

  DashboardData data;
  struct tm timeinfo;
  bool haveTime = getLocalTime(&timeinfo);
  if (haveTime)
  {
    strftime(data.dateStr, sizeof(data.dateStr), "%a %b %d", &timeinfo);
    strftime(data.shortDateStr, sizeof(data.shortDateStr), "%b %d", &timeinfo);
    strftime(data.updatedStr, sizeof(data.updatedStr), "%I:%M %p", &timeinfo);
    data.fullPoolFt = fullPoolForMonth(timeinfo.tm_mon);
  }

  if (!fetchElevationCurrent(data))
  {
    Serial.println("Failed to fetch current elevation");
  }
  if (!fetchElevationHistory(data))
  {
    Serial.println("Failed to fetch elevation history");
  }
  if (haveTime)
  {
    data.haveSeasonalAvg = fetchSeasonalAverageElevation(timeinfo.tm_mon + 1, timeinfo.tm_mday, data.seasonalAvgFt);
    if (!data.haveSeasonalAvg)
    {
      Serial.println("Failed to fetch seasonal average elevation");
    }
  }
  data.haveInflow = fetchInflow(data.inflow);
  if (!data.haveInflow)
  {
    Serial.println("Failed to fetch inflow");
  }
  data.haveOutflow = fetchOutflow(data.outflow);
  if (!data.haveOutflow)
  {
    Serial.println("Failed to fetch outflow");
  }
  if (!fetchWeather(data))
  {
    Serial.println("Failed to fetch weather");
  }

  renderDashboard(data);
}

void loop()
{
  // nothing to do - one-shot fetch and render
}
