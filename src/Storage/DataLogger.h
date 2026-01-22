// Storage/DataLogger.h
#pragma once

#include <Arduino.h>
#include <map>
#include <time.h>
#include <variant>  // C++17 pour gérer float et String

// ─────────────────────────────────────────────
// Référentiel temporel
//
// Ce module ne fournit jamais d'heure locale
// Toute conversion UTC → locale est externe
// ─────────────────────────────────────────────

enum class TimeBase : uint8_t {
    Relative,   // millis depuis boot
    UTC         // timestamp absolu
};

// ─────────────────────────────────────────────
// Types de données
// ─────────────────────────────────────────────

enum class DataType : uint8_t {
    Battery,
    Sensor,
    Actuator,
    System
};

enum class DataId : uint8_t {
    BatteryVoltage,
    BatteryPercent,
    Charging,
    ExternalPower,

    AirTemperature,
    AirHumidity,
    SoilMoisture1,
    SoilMoisture2,

    Valve1State,
    Valve2State,

    // Wi-Fi
    WifiStaEnabled,
    WifiStaConnected,
    WifiApEnabled,
    WifiRssi,

    // Cellular (unifié avec le pattern WiFi)
    CellularEnabled,
    CellularConnected,
    CellularOperator,
    CellularIP,
    CellularRssi,

    Boot,
    Error,

    Count
};

// ─────────────────────────────────────────────
// Enregistrement
// ─────────────────────────────────────────────

struct DataRecord {
    uint32_t timestamp;   // millis ou UTC selon timeBase
    TimeBase timeBase;
    DataType type;
    DataId   id;
    std::variant<float, String> value;  // Peut être float OU String
};

// ─────────────────────────────────────────────
// Dernière observation exposée au Web
// ─────────────────────────────────────────────
// NOTE :
// - value peut contenir soit un float, soit un String (std::variant)
// - t_rel_ms est valide uniquement si utc_valid == false
// - si utc_valid == true, seul t_utc doit être utilisé
// ─────────────────────────────────────────────

struct LastDataForWeb {
    std::variant<float, String> value;  // Peut être float OU String
    uint32_t  t_rel_ms  = 0;
    time_t    t_utc     = 0;
    bool      utc_valid = false;
};

// ─────────────────────────────────────────────
// Statistiques fichier de logs
// ─────────────────────────────────────────────

struct LogFileStats {
    bool exists;          // Le fichier existe-t-il ?
    size_t sizeBytes;     // Taille en bytes
    float sizeMB;         // Taille en MB
    float percentFull;    // Pourcentage utilisé
    float totalGB;        // Taille partition (fixe : 1.9 Go)
};

// ─────────────────────────────────────────────
// DataLogger
// ─────────────────────────────────────────────

class DataLogger {
public:
    static void init();

    // Push pour valeurs numériques (float)
    static void push(DataType type, DataId id, float value);
    
    // Push pour valeurs textuelles (String)
    static void push(DataType type, DataId id, const String& textValue);

    static bool getLast(DataId id, DataRecord& out); // live (si implémenté ailleurs)

    static void handle(); // réparation UTC + flush
    
    // Gestion de l'historique
    static void clearHistory(); // Supprime l'historique flash et réinitialise les buffers

    // ───────────── Web ─────────────
    static bool hasLastDataForWeb(DataId id, LastDataForWeb& out);
    static bool getLastUtcRecord(DataId id, DataRecord& out);
    static String getCurrentValueWithTime(DataId id);   // LEGACY
    static String getGraphCsv(DataId id, uint32_t daysBack = 30);
    
    // Statistiques du fichier de logs
    static LogFileStats getLogFileStats();

private:
    // ───────────── Temps ─────────────
    static uint32_t nowRelative();

    // ───────────── Buffers ─────────────
    static constexpr size_t LIVE_SIZE    = 200;
    static constexpr size_t PENDING_SIZE = 2000;
    static constexpr size_t FLUSH_SIZE   = 50;

    static constexpr uint32_t FLUSH_TIMEOUT_MS = 3600000UL; // 1 heure

    // LIVE (ring buffer simple)
    static DataRecord live[LIVE_SIZE];
    static size_t     liveIndex;

    // PENDING — FIFO circulaire avec perte FIFO
    static DataRecord pending[PENDING_SIZE];
    static size_t     pendingHead;   // index du plus ancien élément
    static size_t     pendingCount;  // nombre d'éléments valides

    // ───────────── Web RAM ─────────────
    static std::map<DataId, LastDataForWeb> lastDataForWeb;

    // ───────────── Internes ─────────────
    static void addLive(const DataRecord& r);
    static void addPending(const DataRecord& r);

    static void tryFlush();
    static void flushToFlash(size_t count);
};