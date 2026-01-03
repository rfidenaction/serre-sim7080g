// Connectivity/CellularManager.h
#pragma once

#include <Arduino.h>

class CellularManager {
public:
    /**
     * Initialise le module cellulaire (modem SIM7080G)
     * À compléter plus tard avec les commandes AT, alimentation, etc.
     */
    static void init();

    /**
     * Retourne le statut actuel du module GSM
     * Utilisé par la page web pour l'affichage
     */
    static String getStatus();

    // Tu pourras ajouter plus tard :
    // static void setStatus(const String& status);
    // static bool isConnected();
    // static void sendData(const String& payload);
    // etc.

private:
    static String gsmStatus;
};