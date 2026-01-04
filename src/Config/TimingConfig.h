// Config/TimingConfig.h
#pragma once

/*
 * TimingConfig
 *
 * Centralisation de TOUS les paramètres temporels SYSTÈME.
 *
 * Règle fondamentale :
 *  - ici : uniquement des timings liés au fonctionnement du moteur
 *  - jamais de timings métier (arrosage, seuils agronomiques, etc.)
 *
 * Objectifs :
 *  - lisibilité long terme
 *  - cohérence globale
 *  - éviter les timings implicites dispersés dans le code
 */

// =============================================================================
// Démarrage système
// =============================================================================

/*
 * Durée de la phase d’initialisation système (INIT).
 *
 * Pendant cette phase :
 *  - les modules matériels sont initialisés
 *  - aucune tâche périodique n’est exécutée
 *  - TaskManager / EventManager / Monitor sont inactifs
 *
 * Objectif :
 *  - laisser le matériel et les bus se stabiliser
 *  - éviter toute mesure ou alerte non significative au boot
 */
#define SYSTEM_INIT_DELAY_MS   2500

// =============================================================================
// EventManager / TaskManager supervision
// =============================================================================

/*
 * Période nominale d’appel d’EventManager par TaskManager.
 * Définit le rythme attendu du cœur du système en régime permanent.
 */
#define EVENT_MANAGER_PERIOD_MS        2000

/*
 * Fenêtre temporelle acceptable autour de la période nominale.
 *
 * Si l’intervalle réel entre deux appels sort de cette plage,
 * TaskManagerMonitor bascule en état WARNING.
 */
#define EVENT_MANAGER_MIN_PERIOD_MS    1500
#define EVENT_MANAGER_MAX_PERIOD_MS    2500

// =============================================================================
// Alimentation / PowerManager (PMU)
// =============================================================================

/*
 * Période de mise à jour de l’état global d’alimentation :
 *  - tension batterie
 *  - pourcentage estimé
 *  - état de charge
 *  - présence d’alimentation externe
 *
 * Fréquence volontairement basse :
 *  - données physiquement lentes
 *  - pas critiques à la seconde
 */
#define POWER_MANAGER_UPDATE_INTERVAL_MS  30000

// =============================================================================
// WiFi (diagnostic et logging)
// =============================================================================

/*
 * Période d’enregistrement des informations WiFi (RSSI, état).
 * Utilisé pour le suivi long terme, pas pour la réactivité immédiate.
 */
#define WIFI_STATUS_LOG_INTERVAL_MS    60000

// =============================================================================
// Réservé – extensions futures
// =============================================================================

// Capteurs environnementaux
// #define AIR_SENSOR_UPDATE_INTERVAL_MS      ...
// #define SOIL_SENSOR_UPDATE_INTERVAL_MS     ...

// Stockage / maintenance
// #define FILESYSTEM_MAINTENANCE_INTERVAL_MS ...
