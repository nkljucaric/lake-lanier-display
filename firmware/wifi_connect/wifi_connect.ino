// Milestone 2: WiFi connect only — join the network and confirm internet
// reachability. No API parsing yet.
//
// Supports multiple candidate networks (home now, in-laws' network later
// once this is gifted) via WiFiMulti — it tries every network listed in
// secrets.h and connects to whichever is in range.
//
// secrets.h is gitignored. Copy secrets.h.example to secrets.h and fill
// in real SSID/password before flashing.

#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include "secrets.h"

WiFiMulti wifiMulti;

void setup()
{
  Serial.begin(115200);
  delay(1000);

  for (const WifiNetwork &network : WIFI_NETWORKS)
  {
    wifiMulti.addAP(network.ssid, network.password);
  }

  Serial.println("wifi_connect: connecting...");
  while (wifiMulti.run() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("Connected to: ");
  Serial.println(WiFi.SSID());
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Confirm actual internet reachability, not just AP association.
  HTTPClient http;
  http.begin("http://example.com");
  int httpCode = http.GET();
  Serial.printf("GET http://example.com -> HTTP %d\n", httpCode);
  http.end();
}

void loop()
{
  // nothing to do - one-shot connectivity check
}
