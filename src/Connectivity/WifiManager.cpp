// WifiManager.cpp

#include <Arduino.h>
#include "Connectivity/WiFiManager.h"
#include <WiFi.h>
#include "Config/NetworkConfig.h"

// États runtime
// -----------------------------------------------------------------------------
bool WiFiManager::staEnabled    = true;
bool WiFiManager::staConnecting = false;
bool WiFiManager::staConnected  = false;
bool WiFiManager::apEnabled     = true;

unsigned long WiFiManager::lastStaAttempt = 0;
Preferences WiFiManager::preferences;

// -----------------------------------------------------------------------------
// Initialisation
// -----------------------------------------------------------------------------
void WiFiManager::init()
{
    loadPreferences();
    startWiFi();
}

// -----------------------------------------------------------------------------
// Chargement preferences
// -----------------------------------------------------------------------------
void WiFiManager::loadPreferences()
{
    preferences.begin("wifi", false);
    staEnabled = preferences.getBool("sta", true);
    preferences.end();
}

// -----------------------------------------------------------------------------
// Démarrage WiFi
// -----------------------------------------------------------------------------
void WiFiManager::startWiFi()
{
    // AP toujours actif au démarrage
    WiFi.mode(WIFI_AP_STA);
    delay(100);

    bool apConfigResult = WiFi.softAPConfig(WIFI_AP_IP, WIFI_AP_GATEWAY, WIFI_AP_SUBNET);
    if (!apConfigResult) {
        Serial.println("Erreur softAPConfig");
    }

    delay(50);

    bool apResult = WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
    Serial.println(apResult ? "AP démarrée" : "Erreur AP");
    Serial.println("IP AP: " + WiFi.softAPIP().toString());

    apEnabled = true;

    // STA optionnel
    if (staEnabled) {
        WiFi.config(WIFI_STA_IP, WIFI_STA_GATEWAY, WIFI_STA_SUBNET, WIFI_STA_DNS);
        WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASSWORD);

        staConnecting  = true;
        staConnected   = false;
        lastStaAttempt = millis();
    }
}

// -----------------------------------------------------------------------------
// Gestion STA (appelée dans loop())
// -----------------------------------------------------------------------------
void WiFiManager::handle()
{
    if (!staEnabled) {
        if (staConnected || staConnecting) {
            WiFi.disconnect();
        }
        staConnected  = false;
        staConnecting = false;
        return;
    }

    wl_status_t status = WiFi.status();

    if (status == WL_CONNECTED) {
        if (!staConnected) {
            staConnected  = true;
            staConnecting = false;
        }
        return;
    }

    // Pas connecté
    staConnected = false;

    // Si STA était en tentative mais timeout dépassé → reset staConnecting
    if (staConnecting && millis() - lastStaAttempt > WIFI_STA_RETRY_INTERVAL) {
        staConnecting = false; // autorise un nouveau retry
    }

    // Retry STA protégé (non agressif)
    if (!staConnecting && millis() - lastStaAttempt > WIFI_STA_RETRY_INTERVAL) {
        WiFi.disconnect();
        WiFi.config(WIFI_STA_IP, WIFI_STA_GATEWAY, WIFI_STA_SUBNET, WIFI_STA_DNS);
        WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASSWORD);

        staConnecting  = true;
        lastStaAttempt = millis();
    }
}

// -----------------------------------------------------------------------------
// AP : coupure à chaud (irréversible)
// -----------------------------------------------------------------------------
void WiFiManager::disableAP()
{
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);

    apEnabled = false;
    Serial.println("AP éteint à chaud (réactivation uniquement par reboot)");
}

// -----------------------------------------------------------------------------
// STA : changement persistant (reboot)
// -----------------------------------------------------------------------------
void WiFiManager::setSTAEnabled(bool enabled)
{
    preferences.begin("wifi", false);
    preferences.putBool("sta", enabled);
    preferences.end();

    delay(500);
    ESP.restart();
}

// -----------------------------------------------------------------------------
// Accesseurs
// -----------------------------------------------------------------------------
bool WiFiManager::isSTAEnabled()    { return staEnabled; }
bool WiFiManager::isSTAConnected() { return staConnected; }
bool WiFiManager::isAPEnabled()    { return apEnabled; }

// -----------------------------------------------------------------------------
// Infos Web
// -----------------------------------------------------------------------------
String WiFiManager::getSTAStatus()
{
    if (!staEnabled) return "Désactivé";

    return staConnected
        ? "Connecté à " + String(WIFI_STA_SSID) + " " + WIFI_STA_IP.toString() + " (" + String(WiFi.RSSI()) + " dBm)"
        : "Recherche " + String(WIFI_STA_SSID) + "...";
}

String WiFiManager::getAPStatus()
{
    return String(WIFI_AP_SSID) + "  " + WIFI_AP_IP.toString();
}
