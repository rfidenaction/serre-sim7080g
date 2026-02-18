// src/Connectivity/CellularStream.cpp
// Implémentation du proxy Stream avec pompage automatique
// Chaque appel à available()/read()/peek() pompe Serial1 d'abord

#include "Connectivity/CellularStream.h"

// -----------------------------------------------------------------------------
// Singleton
// -----------------------------------------------------------------------------
CellularStream& CellularStream::instance()
{
    static CellularStream inst;
    return inst;
}

// -----------------------------------------------------------------------------
// Configuration callback octet
// -----------------------------------------------------------------------------
void CellularStream::setByteCallback(void (*cb)(uint8_t))
{
    byteCallback = cb;
}

// -----------------------------------------------------------------------------
// Gating RX : désactiver la bufferisation vers TinyGSM pendant pending
// -----------------------------------------------------------------------------
void CellularStream::setRxBufferingEnabled(bool enabled)
{
    rxBufferingEnabled = enabled;
}

// -----------------------------------------------------------------------------
// Pompage Serial1 → ring buffer (appelé automatiquement)
// -----------------------------------------------------------------------------
void CellularStream::pumpSerial1()
{
    while (Serial1.available()) {
        uint8_t c = Serial1.read();
        
        // Compteur systématique (indépendant du callback et du gating)
        statsTapBytes++;
        
        // Stockage dans ring buffer (pour TinyGSM) - seulement si gating actif
        if (rxBufferingEnabled) {
            pushByte(c);
        }
        
        // Notification callback (toujours, même si gating désactivé)
        if (byteCallback) {
            byteCallback(c);
        }
    }
}

// -----------------------------------------------------------------------------
// Pompage manuel (exposé pour CellularEvent)
// -----------------------------------------------------------------------------
void CellularStream::pump()
{
    pumpSerial1();
}

// -----------------------------------------------------------------------------
// Ajout d'un octet dans le ring buffer
// -----------------------------------------------------------------------------
void CellularStream::pushByte(uint8_t c)
{
    uint16_t nextHead = (rxHead + 1) % RX_BUFFER_SIZE;
    
    if (nextHead == rxTail) {
        // Buffer plein → overflow
        rxOverflows++;
        return;
    }
    
    rxBuffer[rxHead] = c;
    rxHead = nextHead;
    rxBytesReceived++;
}

// -----------------------------------------------------------------------------
// Statistiques
// -----------------------------------------------------------------------------
uint32_t CellularStream::getOverflows() const
{
    return rxOverflows;
}

uint32_t CellularStream::getBytesReceived() const
{
    return rxBytesReceived;
}

uint16_t CellularStream::getBufferUsed() const
{
    uint16_t head = rxHead;
    uint16_t tail = rxTail;
    
    if (head >= tail) {
        return head - tail;
    } else {
        return RX_BUFFER_SIZE - tail + head;
    }
}

uint32_t CellularStream::getTapBytesCount() const
{
    return statsTapBytes;
}

// -----------------------------------------------------------------------------
// Stream : available()
// -----------------------------------------------------------------------------
int CellularStream::available()
{
    // Pomper Serial1 d'abord
    pumpSerial1();
    
    // Calculer taille disponible
    uint16_t head = rxHead;
    uint16_t tail = rxTail;
    
    if (head >= tail) {
        return head - tail;
    } else {
        return RX_BUFFER_SIZE - tail + head;
    }
}

// -----------------------------------------------------------------------------
// Stream : read()
// -----------------------------------------------------------------------------
int CellularStream::read()
{
    // Pomper Serial1 d'abord
    pumpSerial1();
    
    // Buffer vide ?
    if (rxTail == rxHead) {
        return -1;
    }
    
    uint8_t c = rxBuffer[rxTail];
    rxTail = (rxTail + 1) % RX_BUFFER_SIZE;
    return c;
}

// -----------------------------------------------------------------------------
// Stream : peek()
// -----------------------------------------------------------------------------
int CellularStream::peek()
{
    // Pomper Serial1 d'abord
    pumpSerial1();
    
    // Buffer vide ?
    if (rxTail == rxHead) {
        return -1;
    }
    
    return rxBuffer[rxTail];
}

// -----------------------------------------------------------------------------
// Stream : write (forward vers Serial1)
// -----------------------------------------------------------------------------
size_t CellularStream::write(uint8_t c)
{
    return Serial1.write(c);
}

size_t CellularStream::write(const uint8_t* buf, size_t len)
{
    return Serial1.write(buf, len);
}

void CellularStream::flush()
{
    Serial1.flush();
}