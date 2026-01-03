// Storage/DataLogger.cpp
#include "Storage/DataLogger.h"
#include <SPIFFS.h>
#include <time.h>  // Pour localtime et strftime

// ... (tout le code précédent inchangé : variables, init, nowRelative, nowUTC, onNtpSync, push, addLive, addPending, tryFlush, flushToFlash, getLast, handle)

// ─────────────────────────────────────────
// 1. Dernière valeur UTC dans la flash
// ─────────────────────────────────────────
bool DataLogger::getLastUtcRecord(DataId id, DataRecord& out) {
    File file = SPIFFS.open("/datalog.csv", FILE_READ);
    if (!file) return false;

    String line;
    bool found = false;
    DataRecord candidate;

    while (file.available()) {
        line = file.readStringUntil('\n');
        if (line.length() == 0) continue;

        unsigned long ts;
        uint8_t typeByte, idByte;
        float val;

        int parsed = sscanf(line.c_str(), "%lu,%hhu,%hhu,%f", &ts, &typeByte, &idByte, &val);
        if (parsed == 4 && idByte == static_cast<uint8_t>(id)) {
            candidate.timestamp = ts;
            candidate.timeBase  = TimeBase::UTC;
            candidate.id        = id;
            candidate.value     = val;
            found = true;
        }
    }

    if (found) out = candidate;

    file.close();
    return found;
}

// ─────────────────────────────────────────
// 2. Valeur courante avec date formatée JJ/MM/AA HH:MM
// ─────────────────────────────────────────
String DataLogger::getCurrentValueWithTime(DataId id) {
    DataRecord r;
    if (getLastUtcRecord(id, r)) {
        struct tm *tm = localtime((time_t*)&r.timestamp);
        char dateStr[20];
        strftime(dateStr, sizeof(dateStr), "%d/%m/%y %H:%M", tm);  // <-- AA au lieu de AAAA

        if (id == DataId::BatteryVoltage) {
            return String(r.value, 2) + " V (" + String(dateStr) + ")";
        } else if (id == DataId::BatteryPercent) {
            return String((int)r.value) + " % (" + String(dateStr) + ")";
        } else {
            return String(r.value, 2) + " (" + String(dateStr) + ")";
        }
    }

    return "NC";
}

// ─────────────────────────────────────────
// 3. Graphique sur période (daysBack jours)
// ─────────────────────────────────────────
String DataLogger::getGraphCsv(DataId id, uint32_t daysBack) {
    File file = SPIFFS.open("/datalog.csv", FILE_READ);
    if (!file) return "";

    uint32_t cutoffTime = nowUTC() - (daysBack * 86400UL);

    String csv = "";
    bool first = true;

    while (file.available()) {
        String line = file.readStringUntil('\n');
        if (line.length() == 0) continue;

        unsigned long ts;
        uint8_t typeByte, idByte;
        float val;

        int parsed = sscanf(line.c_str(), "%lu,%hhu,%hhu,%f", &ts, &typeByte, &idByte, &val);
        if (parsed == 4 && idByte == static_cast<uint8_t>(id) && ts >= cutoffTime) {
            if (!first) csv += ",";
            csv += String(val, 2);
            first = false;
        }
    }

    file.close();
    return csv;
}