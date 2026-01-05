// Web/WebServer.h
#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>

class WebServer {
public:
    /**
     * Initialise le serveur web :
     * - Configure toutes les routes
     * - Démarre le serveur
     */
    static void init();

private:
    // Instance du serveur asynchrone (port 80)
    static AsyncWebServer server;

    // Handlers pour chaque route
    static void handleRoot(AsyncWebServerRequest *request);
    static void handleWifiToggle(AsyncWebServerRequest *request);
    static void handleApToggle(AsyncWebServerRequest *request);
    static void handleGraphData(AsyncWebServerRequest *request);
    static void handleReset(AsyncWebServerRequest *request);
};
