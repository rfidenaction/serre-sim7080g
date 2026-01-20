// src/Config/NetworkConfig.h
#pragma once
#include <IPAddress.h>

// -----------------------------------------------------------------------------
// WiFi — STA
// -----------------------------------------------------------------------------
static constexpr const char* WIFI_STA_SSID     = "MyFree2";
static constexpr const char* WIFI_STA_PASSWORD = "1234567890";
static const IPAddress WIFI_STA_IP      (192,168,1,150);
static const IPAddress WIFI_STA_GATEWAY (192,168,1,1);
static const IPAddress WIFI_STA_SUBNET  (255,255,255,0);
static const IPAddress WIFI_STA_DNS     (192,168,1,1);

// -----------------------------------------------------------------------------
// WiFi — AP
// -----------------------------------------------------------------------------
static constexpr const char* WIFI_AP_SSID     = "Serre_de_Marie-Pierre";
static constexpr const char* WIFI_AP_PASSWORD = "1234567890";
static const IPAddress WIFI_AP_IP      (192,168,4,1);
static const IPAddress WIFI_AP_GATEWAY (192,168,4,1);
static const IPAddress WIFI_AP_SUBNET  (255,255,255,0);

// -----------------------------------------------------------------------------
// Timings WiFi
// -----------------------------------------------------------------------------
static constexpr unsigned long WIFI_STA_RETRY_INTERVAL = 5000;

// =============================================================================
// Cellular - Configuration modem SIM7080G
// =============================================================================

// APN (Access Point Name)
// Orange M2M via Domotec Services
static constexpr const char* CELLULAR_APN = "domotec82.fr";

// Credentials (vides pour Domotec)
static constexpr const char* CELLULAR_USER = "";
static constexpr const char* CELLULAR_PASS = "";

// Centre serveur SMS (non utilisé actuellement, SMSC fourni par la SIM)
static constexpr const char* CELLULAR_SMSC = "+32495005580";

// =============================================================================
// Cellular - SMS - Numéros de destination
// Format international avec "+" (validé avec code fonctionnel)
// =============================================================================
static constexpr const char* SMS_NUMBERS[] = {
    "+33672967933"
    // Ajouter d'autres numéros ici si besoin :
    // "+33698765432",
    // "+33611223344"
};
static constexpr size_t SMS_NUMBERS_COUNT = sizeof(SMS_NUMBERS) / sizeof(SMS_NUMBERS[0]);