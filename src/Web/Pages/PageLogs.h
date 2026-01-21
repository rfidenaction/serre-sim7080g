// Web/Pages/PageLogs.h
#pragma once

#include <Arduino.h>
#include "Storage/DataLogger.h"  // Pour LogFileStats

class PageLogs {
public:
    /**
     * Retourne le code HTML complet de la page de gestion des logs
     * @param gsmActive Si true, affiche un message d'avertissement (GSM actif)
     * @param stats Statistiques du fichier de logs
     */
    static String getHtml(bool gsmActive, const LogFileStats& stats);
};