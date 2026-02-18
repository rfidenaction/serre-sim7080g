// src/Connectivity/CellularEvent.cpp
// Phase 2 : Parsing lignes via callback octet
// Reçoit octets de CellularStream, reconstruit lignes, classifie, dispatch

#include "Connectivity/CellularEvent.h"
#include "Connectivity/CellularStream.h"
#include "Utils/Logger.h"

static const char* TAG = "CellEvent";

// -----------------------------------------------------------------------------
// Membres statiques
// -----------------------------------------------------------------------------
char CellularEvent::lineBuffer[LINE_BUFFER_SIZE];
uint16_t CellularEvent::lineLen = 0;

CellularLineCallback CellularEvent::lineCallback = nullptr;
bool CellularEvent::lineParsingEnabled = false;

uint32_t CellularEvent::statsPollCount = 0;
uint32_t CellularEvent::statsLinesReceived = 0;
uint32_t CellularEvent::statsBufferOverflows = 0;

// -----------------------------------------------------------------------------
// Initialisation
// -----------------------------------------------------------------------------
void CellularEvent::init()
{
    lineLen = 0;
    lineBuffer[0] = '\0';
    
    lineParsingEnabled = false;
    lineCallback = nullptr;
    
    statsPollCount = 0;
    statsLinesReceived = 0;
    statsBufferOverflows = 0;
    
    Logger::info(TAG, "CellularEvent initialisé (Phase 2 - parsing lignes)");
}

// -----------------------------------------------------------------------------
// Configuration callback ligne
// -----------------------------------------------------------------------------
void CellularEvent::setLineCallback(CellularLineCallback cb)
{
    lineCallback = cb;
}

// -----------------------------------------------------------------------------
// Contrôle du parsing
// -----------------------------------------------------------------------------
void CellularEvent::enableLineParsing(bool enable)
{
    if (enable && !lineParsingEnabled) {
        // Reset buffer au démarrage du parsing
        lineLen = 0;
        lineBuffer[0] = '\0';
    }
    
    lineParsingEnabled = enable;
    Logger::info(TAG, enable ? "Parsing activé" : "Parsing désactivé");
}

bool CellularEvent::isLineParsingEnabled()
{
    return lineParsingEnabled;
}

// -----------------------------------------------------------------------------
// Statistiques
// -----------------------------------------------------------------------------
uint32_t CellularEvent::getPollCount()
{
    return statsPollCount;
}

uint32_t CellularEvent::getLinesReceived()
{
    return statsLinesReceived;
}

uint32_t CellularEvent::getBufferOverflows()
{
    return statsBufferOverflows;
}

// -----------------------------------------------------------------------------
// Poll - Appelé toutes les 20ms par TaskManager
// -----------------------------------------------------------------------------
void CellularEvent::poll()
{
    statsPollCount++;
    
    // Forcer le pompage de Serial1 vers le ring buffer
    // Les octets arrivent via onByte() appelé par CellularStream
    CellularStream::instance().pump();
}

// -----------------------------------------------------------------------------
// Réception octet (appelé par CellularStream::pumpSerial1)
// -----------------------------------------------------------------------------
void CellularEvent::onByte(uint8_t c)
{
    // Si parsing désactivé, ignorer
    if (!lineParsingEnabled) {
        return;
    }
    
    processChar(c);
}

// -----------------------------------------------------------------------------
// Traitement d'un caractère
// -----------------------------------------------------------------------------
void CellularEvent::processChar(uint8_t c)
{
    // Ignorer \r (on gère uniquement \n comme fin de ligne)
    if (c == '\r') {
        return;
    }
    
    // Fin de ligne : \n
    if (c == '\n') {
        if (lineLen > 0) {
            dispatchLine();
        }
        // Reset buffer
        lineLen = 0;
        lineBuffer[0] = '\0';
        return;
    }
    
    // Détection prompt SMS : '>' arrive souvent SANS \n
    if (c == '>') {
        // Vérifier si buffer vide ou juste espaces
        bool isPrompt = true;
        for (uint16_t i = 0; i < lineLen; i++) {
            if (lineBuffer[i] != ' ' && lineBuffer[i] != '\t') {
                isPrompt = false;
                break;
            }
        }
        
        if (isPrompt) {
            // Dispatch PROMPT immédiatement
            lineBuffer[0] = '>';
            lineBuffer[1] = '\0';
            lineLen = 1;
            dispatchLine();
            
            // Reset buffer
            lineLen = 0;
            lineBuffer[0] = '\0';
            return;
        }
    }
    
    // Caractère normal : ajouter au buffer
    if (lineLen < LINE_BUFFER_SIZE - 1) {
        lineBuffer[lineLen++] = (char)c;
        lineBuffer[lineLen] = '\0';
    } else {
        // Overflow - ligne trop longue, drop et reset
        statsBufferOverflows++;
        lineLen = 0;
        lineBuffer[0] = '\0';
    }
}

// -----------------------------------------------------------------------------
// Dispatch d'une ligne complète
// -----------------------------------------------------------------------------
void CellularEvent::dispatchLine()
{
    // Trim espaces début et fin
    uint16_t start = 0;
    uint16_t end = lineLen;
    
    // Trim début
    while (start < end && (lineBuffer[start] == ' ' || lineBuffer[start] == '\t')) {
        start++;
    }
    
    // Trim fin
    while (end > start && (lineBuffer[end - 1] == ' ' || lineBuffer[end - 1] == '\t')) {
        end--;
    }
    
    // Ligne vide après trim ?
    if (start >= end) {
        return;
    }
    
    // Construire ligne trimée (in-place si nécessaire)
    if (start > 0) {
        memmove(lineBuffer, lineBuffer + start, end - start);
    }
    lineBuffer[end - start] = '\0';
    
    // Classifier
    CellularLineType type = classifyLine(lineBuffer);
    
    // Stats
    statsLinesReceived++;
    
    // Dispatch via callback
    if (lineCallback) {
        lineCallback(type, lineBuffer);
    }
}

// -----------------------------------------------------------------------------
// Classification de ligne
// -----------------------------------------------------------------------------
CellularLineType CellularEvent::classifyLine(const char* line)
{
    // OK
    if (strcmp(line, "OK") == 0) {
        return CellularLineType::OK;
    }
    
    // ERROR (exact)
    if (strcmp(line, "ERROR") == 0) {
        return CellularLineType::ERROR;
    }
    
    // +CME ERROR:xxx ou +CMS ERROR:xxx
    if (strncmp(line, "+CME ERROR", 10) == 0 || strncmp(line, "+CMS ERROR", 10) == 0) {
        return CellularLineType::ERROR;
    }
    
    // Prompt SMS ">"
    if (strcmp(line, ">") == 0) {
        return CellularLineType::PROMPT;
    }
    
    // Ligne normale (réponse AT, URC, etc.)
    return CellularLineType::LINE;
}