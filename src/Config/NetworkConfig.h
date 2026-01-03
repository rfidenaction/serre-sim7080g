#pragma once
#include <IPAddress.h>

// -----------------------------------------------------------------------------
// WiFi – STA
// -----------------------------------------------------------------------------
static constexpr const char* WIFI_STA_SSID     = "MyFree2";
static constexpr const char* WIFI_STA_PASSWORD = "1234567890";

static const IPAddress WIFI_STA_IP      (192,168,1,150);
static const IPAddress WIFI_STA_GATEWAY (192,168,1,1);
static const IPAddress WIFI_STA_SUBNET  (255,255,255,0);
static const IPAddress WIFI_STA_DNS     (192,168,1,1);

// -----------------------------------------------------------------------------
// WiFi – AP
// -----------------------------------------------------------------------------
static constexpr const char* WIFI_AP_SSID     = "Serre_de_Marie-Pierre";
static constexpr const char* WIFI_AP_PASSWORD = "1234567890";

static const IPAddress WIFI_AP_IP      (192,168,4,1);
static const IPAddress WIFI_AP_GATEWAY (192,168,4,1);
static const IPAddress WIFI_AP_SUBNET  (255,255,255,0);

// -----------------------------------------------------------------------------
// Timings
// -----------------------------------------------------------------------------
static constexpr unsigned long WIFI_STA_RETRY_INTERVAL = 5000;
