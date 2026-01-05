// Connectivity/ManagerUTC.h
#pragma once

#include <Arduino.h>
#include <time.h>

/*
 * ManagerUTC
 *
 * Source unique de vérité du temps UTC système.
 *
 * - Synchronisation via NTP (Wi-Fi)
 * - Politique interne :
 *     * Boot : jusqu'à 10 tentatives rapprochées
 *     * Régime : 1 tentative toutes les 3 heures
 * - UTC valide pendant 25 heures après une synchro réussie
 *
 * Ce module est autonome :
 * - non bloquant
 * - sans TaskManager
 * - sans dépendance DataLogger / Web
 */

class ManagerUTC {
public:
    // Cycle de vie
    static void init();
    static void handle();   // à appeler régulièrement (loop)

    // API publique
    static bool     isUtcValid();
    static bool     hasEverSynced();
    static int32_t  getUtcOffset();
    static time_t   nowUtc();

    // Conversion relative → UTC (pour DataLogger)
    static time_t   convertFromRelative(uint32_t t_rel_ms);

private:
    // Synchronisation interne
    static bool trySync();

    // État interne
    static bool     everSynced;
    static uint8_t  bootAttempts;
    static uint32_t lastAttemptMs;
    static uint32_t lastSyncMs;

    // Référence temporelle
    static uint32_t syncRelMs;   // millis() au moment de la synchro
    static time_t   syncUtc;     // UTC correspondant

    static int32_t  utcOffset;
};
