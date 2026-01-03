// Connectivity/CellularManager.cpp
#include "Connectivity/CellularManager.h"

// Variable statique privée : statut du module GSM
String CellularManager::gsmStatus = "Modem éteint";

void CellularManager::init() {
    // Code futur d'initialisation du modem SIM7080G
    // Exemples possibles plus tard :
    // - Allumage du modem via pin PWRKEY
    // - Serial2.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
    // - Envoi de commandes AT de base (AT, ATE0, etc.)

    // Pour l'instant, on met simplement le statut visible sur la page web
    gsmStatus = "Non initialisé";
}

String CellularManager::getStatus() {
    return gsmStatus;
}