// Config/IO-Config.h
#pragma once

/*
 * IO-Config
 * 
 * Configuration des GPIO et pins physiques.
 * 
 * Actuellement : Modem SIM7080G uniquement
 * Extensions futures : Sections réservées (commentées)
 * 
 * ⚠️ GPIO INTERDITS (PSRAM Octal SPI) :
 *  - GPIO 35, 36, 37 : NE JAMAIS UTILISER
 *    (Causera des crashs système)
 */

// =============================================================================
// Modem SIM7080G (Cellular Cat-M / NB-IoT)
// =============================================================================

// Configuration UART
#define MODEM_UART_NUM     1         // Utilise Serial1
#define MODEM_UART_BAUD    115200    // Baudrate (recommandé LilyGO)

// Pins de contrôle du modem
#define MODEM_PWR_PIN      41        // PWRKEY - Power on/off (pulse 128ms min)
#define MODEM_DTR_PIN      42        // DTR - Sleep control
#define MODEM_RI_PIN       3         // RI - Ring Indicator (notification SMS)

// Pins UART (connexion ESP32-S3 ↔ SIM7080G)
#define MODEM_RX_PIN       4         // ESP32 RX ← TX modem
#define MODEM_TX_PIN       5         // ESP32 TX → RX modem

// =============================================================================
// Sections réservées pour extensions futures
// =============================================================================

// Capteurs environnementaux
// (à définir selon vos besoins)

// Actionneurs (électrovannes, pompes)
// (à définir selon vos besoins)

// =============================================================================
// GPIO à NE JAMAIS UTILISER
// =============================================================================

/*
 * ⚠️ PSRAM Octal SPI
 * 
 * Le T-SIM7080G-S3 utilise PSRAM Octal SPI qui réserve :
 *  - GPIO 35
 *  - GPIO 36
 *  - GPIO 37
 * 
 * Ces GPIO sont INTERDITS. Leur utilisation causera des crashs.
 * 
 * Référence : Documentation LilyGO officielle
 */

// =============================================================================
// Notes techniques
// =============================================================================

/*
 * Carte : LilyGo T-SIM7080G-S3 V1.0 (avec PMU AXP2101)
 * MCU   : ESP32-S3 (dual-core Xtensa LX7 @ 240MHz)
 * Modem : SIMCom SIM7080G (Cat-M / NB-IoT)
 * 
 * Limites connues :
 *  - GPS et Cellular ne peuvent PAS fonctionner simultanément
 *  - GPIO 35-37 réservés PSRAM (interdits)
 *  - SIM card doit être insérée AVANT power-on du modem
 */