// src/Connectivity/CellularEvent.h
// Gestionnaire d'événements modem - Phase 2 : parsing lignes
// Rôle : Recevoir octets via callback, reconstruire lignes, classifier, dispatcher

#ifndef CELLULAREVENT_H
#define CELLULAREVENT_H

#include <Arduino.h>

// Forward declaration
class CellularStream;

// -----------------------------------------------------------------------------
// Types de lignes détectées
// -----------------------------------------------------------------------------
enum class CellularLineType {
    OK,        // "OK"
    ERROR,     // "ERROR"
    PROMPT,    // ">" (prompt SMS)
    LINE       // Ligne normale (réponse AT, URC, etc.)
};

// -----------------------------------------------------------------------------
// Callback de dispatch (vers CellularManager ou debug)
// -----------------------------------------------------------------------------
typedef void (*CellularLineCallback)(CellularLineType type, const char* line);

// -----------------------------------------------------------------------------
// CellularEvent - Gestionnaire événements modem
// -----------------------------------------------------------------------------
class CellularEvent {
public:
    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------
    static void init();
    static void poll();  // Appelé toutes les 20ms par TaskManager
    
    // -------------------------------------------------------------------------
    // Réception octet (appelé par CellularStream)
    // -------------------------------------------------------------------------
    static void onByte(uint8_t c);
    
    // -------------------------------------------------------------------------
    // Configuration callback ligne
    // -------------------------------------------------------------------------
    static void setLineCallback(CellularLineCallback cb);
    
    // -------------------------------------------------------------------------
    // Contrôle du parsing
    // -------------------------------------------------------------------------
    static void enableLineParsing(bool enable);
    static bool isLineParsingEnabled();
    
    // -------------------------------------------------------------------------
    // Statistiques
    // -------------------------------------------------------------------------
    static uint32_t getPollCount();
    static uint32_t getLinesReceived();
    static uint32_t getBufferOverflows();
    
private:
    // -------------------------------------------------------------------------
    // Buffer ligne
    // -------------------------------------------------------------------------
    static constexpr uint16_t LINE_BUFFER_SIZE = 256;
    static char lineBuffer[LINE_BUFFER_SIZE];
    static uint16_t lineLen;
    
    // -------------------------------------------------------------------------
    // Callback
    // -------------------------------------------------------------------------
    static CellularLineCallback lineCallback;
    
    // -------------------------------------------------------------------------
    // Contrôle parsing
    // -------------------------------------------------------------------------
    static bool lineParsingEnabled;
    
    // -------------------------------------------------------------------------
    // Statistiques
    // -------------------------------------------------------------------------
    static uint32_t statsPollCount;
    static uint32_t statsLinesReceived;
    static uint32_t statsBufferOverflows;
    
    // -------------------------------------------------------------------------
    // Méthodes internes
    // -------------------------------------------------------------------------
    static void processChar(uint8_t c);
    static void dispatchLine();
    static CellularLineType classifyLine(const char* line);
};

#endif // CELLULAREVENT_H