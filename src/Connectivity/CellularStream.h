// src/Connectivity/CellularStream.h
// Proxy Stream pour TinyGSM avec ring buffer RX et pompage automatique
// Rôle : Permettre à TinyGSM de fonctionner sans bloquer TaskManager
//        Le pompage Serial1 se fait à chaque appel available()/read()/peek()

#ifndef CELLULARSTREAM_H
#define CELLULARSTREAM_H

#include <Arduino.h>

class CellularStream : public Stream {
public:
    // -------------------------------------------------------------------------
    // Singleton
    // -------------------------------------------------------------------------
    static CellularStream& instance();
    
    // -------------------------------------------------------------------------
    // Pompage manuel (appelé par CellularEvent)
    // -------------------------------------------------------------------------
    void pump();
    
    // -------------------------------------------------------------------------
    // Callback octet (pour CellularEvent Phase 2)
    // -------------------------------------------------------------------------
    void setByteCallback(void (*cb)(uint8_t));
    
    // -------------------------------------------------------------------------
    // Gating RX : désactiver la bufferisation vers TinyGSM pendant pending
    // Le tap callback continue de fonctionner
    // -------------------------------------------------------------------------
    void setRxBufferingEnabled(bool enabled);
    
    // -------------------------------------------------------------------------
    // Statistiques
    // -------------------------------------------------------------------------
    uint32_t getOverflows() const;
    uint32_t getBytesReceived() const;
    uint16_t getBufferUsed() const;
    uint32_t getTapBytesCount() const;
    
    // -------------------------------------------------------------------------
    // Stream : lecture (utilisé par TinyGSM)
    // Chaque appel pompe d'abord Serial1
    // -------------------------------------------------------------------------
    int available() override;
    int read() override;
    int peek() override;
    
    // -------------------------------------------------------------------------
    // Stream : écriture (forward vers Serial1)
    // -------------------------------------------------------------------------
    size_t write(uint8_t c) override;
    size_t write(const uint8_t* buf, size_t len) override;
    void flush() override;
    
private:
    // -------------------------------------------------------------------------
    // Constructeur privé (singleton)
    // -------------------------------------------------------------------------
    CellularStream() = default;
    CellularStream(const CellularStream&) = delete;
    CellularStream& operator=(const CellularStream&) = delete;
    
    // -------------------------------------------------------------------------
    // Pompage interne Serial1 → ring buffer
    // -------------------------------------------------------------------------
    void pumpSerial1();
    void pushByte(uint8_t c);
    
    // -------------------------------------------------------------------------
    // Ring buffer RX
    // -------------------------------------------------------------------------
    static constexpr size_t RX_BUFFER_SIZE = 2048;
    uint8_t rxBuffer[RX_BUFFER_SIZE];
    uint16_t rxHead = 0;  // Position écriture
    uint16_t rxTail = 0;  // Position lecture
    
    // -------------------------------------------------------------------------
    // Callback octet (optionnel)
    // -------------------------------------------------------------------------
    void (*byteCallback)(uint8_t) = nullptr;
    
    // -------------------------------------------------------------------------
    // Gating RX
    // -------------------------------------------------------------------------
    bool rxBufferingEnabled = true;
    
    // -------------------------------------------------------------------------
    // Statistiques
    // -------------------------------------------------------------------------
    uint32_t rxOverflows = 0;
    uint32_t rxBytesReceived = 0;
    uint32_t statsTapBytes = 0;
};

#endif // CELLULARSTREAM_H