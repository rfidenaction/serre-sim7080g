# Analyse ManagerUTC - Gestion NTP en contexte LTE Cat-M

**Projet** : Serre connectée - LilyGo T-SIM7080G-S3  
**Date d'analyse** : 2025-01-07  
**Version firmware** : 5.7.1  
**Statut** : ⏸️ EN ATTENTE - Implémentation après CellularManager

---

## 📋 Contexte du projet

### Matériel
- **Carte** : LilyGo T-SIM7080G-S3
- **MCU** : ESP32-S3 (240 MHz, PSRAM)
- **Modem** : SIM7080G (LTE Cat-M1 / NB-IoT)
- **PMU** : AXP2101

### Connectivité
- **WiFi** : Principal actuellement
- **LTE Cat-M** : À implémenter (CellularManager)
- **Contrainte majeure** : Data réseau limité et coûteux

### Architecture logicielle
- TaskManager : Tâches périodiques non-bloquantes
- EventManager : Observation d'état (courant/précédent)
- ManagerUTC : Synchronisation NTP contrôlée

---

## 🔍 Problème identifié

### Description technique

Dans `ManagerUTC.cpp`, la fonction `trySync()` bloque la loop principale :

```cpp
bool ManagerUTC::trySync()
{
    configTzTime(SYSTEM_TIMEZONE, "pool.ntp.org", ...);
    sntp_init();
    
    const uint32_t startMs = millis();
    while (millis() - startMs < 10000) {  // ⏱️ Jusqu'à 10 secondes
        time(&utcNow);
        if (utcNow >= UTC_MIN_VALID_TIMESTAMP) {
            sntp_stop();
            return true;
        }
        delay(100);  // ⚠️ Bloque 100ms × 100 fois max
    }
    
    sntp_stop();
    return false;
}
```

### Impact réel

Pendant la synchronisation NTP (jusqu'à 10 secondes) :
- ❌ `loop()` principale est gelée
- ❌ `TaskManager::handle()` ne s'exécute pas
- ❌ `WiFiManager::handle()` ne répond pas
- ❌ WebServer inaccessible (timeout HTTP pour l'utilisateur)
- ❌ Risque de WARNING dans TaskManagerMonitor

### Justification du timeout de 10s

**Le timeout de 10s est CORRECT** pour du LTE Cat-M :

| Type réseau | Latence typique | Latence max |
|-------------|----------------|-------------|
| WiFi | 10-50 ms | 200 ms |
| **LTE Cat-M** | **500 ms - 3s** | **10s** |
| NB-IoT | 1-10s | 30s |

**Problème** : Ce n'est pas la durée qui pose problème, mais **où** elle se produit (dans une boucle bloquante).

---

## 📊 Données techniques critiques

### Consommation data NTP via LTE

**Protocole NTP** :
- Paquet NTP = 48 bytes (client → serveur)
- Réponse NTP = 48 bytes (serveur → client)
- Total brut = 96 bytes

**Overhead réseau LTE** :
- UDP header : 8 bytes
- IP header : 20-40 bytes
- PPP/LTE framing : ~40-60 bytes
- **Total par sync : ~200-250 bytes**

**Avec politique actuelle** (resync toutes les 3h) :
- 8 syncs/jour × 250 bytes = **2 KB/jour**
- **60 KB/mois** juste pour NTP

### Spécificités SIM7080G

D'après documentation officielle SIMCOM :

1. **AT+CNTP disponible** pour NTP natif via modem
   - Commande : `AT+CNTPCID=1` puis `AT+CNTP="pool.ntp.org",0`
   - Avantage : Le modem gère tout
   - Inconvénient : Consommation data non contrôlée

2. **GPS et Cellular incompatibles simultanément**
   - Citation GitHub : "GPS/GNSS and cellular can not be used together. Causes module to hang"
   - Nécessite séquence : réseau OFF → GPS → réseau ON

3. **Power Save Mode (PSM) + eDRX**
   - Permet autonomie jusqu'à 10 ans sur batterie
   - Réveil modem = 1-3 secondes avant transmission

### Dérive RTC ESP32

- Dérive typique : **~4.5 minutes par 12h** sans resync
- Justification expiration après 25h : **Excellent choix**

---

## 💡 Solutions évaluées

### Solution A : Polling non-bloquant (RECOMMANDÉE)

**Principe** : Ne plus attendre dans `trySync()`, mais vérifier l'état à chaque appel de `handle()`.

**Architecture** :
```cpp
enum class NtpState { IDLE, WAITING, SUCCESS, TIMEOUT };
static NtpState ntpState = IDLE;
static uint32_t ntpStartMs = 0;

void ManagerUTC::startNtpSync() {
    sntp_init();
    ntpState = WAITING;
    ntpStartMs = millis();
}

void ManagerUTC::handle() {
    if (ntpState == WAITING) {
        time_t now;
        time(&now);
        if (now >= UTC_MIN_VALID_TIMESTAMP) {
            ntpState = SUCCESS;
            utcValid = true;
            // ...
        } else if (millis() - ntpStartMs > 10000) {
            ntpState = TIMEOUT;
        }
        return;
    }
    // ... reste de la logique inchangée
}
```

**Avantages** :
- ✅ Zéro blocage de loop()
- ✅ Fonctionne avec WiFi ET LTE
- ✅ Garde la machine d'état intacte
- ✅ Simple à implémenter (30 min)
- ✅ Compatible avec approche de contrôle strict

**Inconvénients** :
- ⚠️ Latence de détection = 2s (période d'appel handle())
  - WiFi : 50ms réel + 2s détection = 2.05s total (acceptable)
  - LTE : 3s réel + 2s détection = 5s total (excellent)

**Effort** : 30 minutes

---

### Solution B : AT+CNTP via SIM7080G (Future)

**Principe** : Déléguer NTP au modem via commandes AT.

**Commandes** :
```cpp
Serial1.println("AT+CNTPCID=1");  // Context ID
Serial1.println("AT+CNTP=\"pool.ntp.org\",0");  // Sync
// Réponse : +CNTP: 1 (succès) ou +CNTP: 61 (échec)
```

**Avantages** :
- ✅ Zéro code ESP32 pour NTP
- ✅ Le modem optimise la data
- ✅ Pas de blocage ESP32

**Inconvénients** :
- ⚠️ Nécessite CellularManager fonctionnel
- ⚠️ Fiabilité variable selon opérateurs
- ⚠️ Moins de contrôle retry

**Quand l'utiliser** : Mode LTE uniquement (sans WiFi)

**Effort** : 2 heures

---

### Solution C : Hybrid WiFi + LTE (OPTIMALE à terme)

**Principe** : NTP via WiFi si disponible, sinon via LTE.

**Architecture** :
```cpp
void ManagerUTC::handle() {
    bool useWifi = WiFiManager::isSTAConnected();
    bool useLte  = CellularManager::isConnected();
    
    if (!utcValid) {
        if (useWifi) {
            // NTP via ESP32 (rapide, gratuit)
            startNtpSync();
        } else if (useLte) {
            // NTP via AT+CNTP (backup)
            requestNtpViaCellular();
        }
    }
}
```

**Avantages** :
- ✅ WiFi = rapide + gratuit
- ✅ LTE = backup fiable
- ✅ Optimise coûts data
- ✅ Redondance réseau

**Inconvénients** :
- ⚠️ Complexité accrue
- ⚠️ Tests nécessaires avec les 2 réseaux

**Effort** : 3 heures

---

## 📊 Tableau comparatif

| Critère | Solution A | Solution B | Solution C |
|---------|-----------|-----------|-----------|
| **Blocage loop** | ✅ Aucun | ✅ Aucun | ✅ Aucun |
| **Compatibilité code actuel** | ✅ Excellente | ⚠️ Nécessite CellularManager | 🟡 Moyenne |
| **Contrôle data** | ✅ Total | 🟡 Partiel | ✅ Optimal |
| **Effort implémentation** | 30 min | 2h | 3h |
| **Production-ready** | ✅ Maintenant | ⚠️ Futur | ⚠️ Futur |
| **Adapté WiFi** | ✅ Oui | ❌ Non | ✅ Oui |
| **Adapté LTE** | ✅ Oui | ✅ Oui | ✅ Optimal |

---

## 🎯 Décision

### Phase 1 : Attente CellularManager

**Status actuel** : ⏸️ **EN PAUSE**

**Raison** :
- CellularManager n'est pas encore implémenté
- Impossible de tester le comportement LTE réel
- ManagerUTC fonctionne actuellement (pas de bug bloquant)

**Action** : Implémenter CellularManager en priorité

### Phase 2 : Implémentation post-CellularManager

**Solution retenue** : **Solution A** (polling non-bloquant) comme base

**Puis** : Migration vers **Solution C** (hybrid) une fois LTE stable

**Justification** :
1. Solution A simple et rapide (30 min)
2. Fonctionne immédiatement avec WiFi
3. Base solide pour ajouter LTE ensuite
4. Solution C optimale mais nécessite CellularManager complet

---

## 📝 Checklist d'implémentation future

### Phase 1 : Non-bloquant (Solution A)

- [ ] Ajouter `enum class NtpState` dans ManagerUTC.h
- [ ] Remplacer `trySync()` par `startNtpSync()` (non-bloquant)
- [ ] Modifier `handle()` pour checker l'état périodiquement
- [ ] Tester avec WiFi uniquement
- [ ] Vérifier TaskManagerMonitor (pas de WARNING)
- [ ] Tester WebServer pendant sync NTP

### Phase 2 : Hybrid WiFi+LTE (Solution C)

- [ ] Implémenter `requestNtpViaCellular()` via AT+CNTP
- [ ] Ajouter sélection intelligente WiFi/LTE
- [ ] Logger la consommation data réelle
- [ ] Tester failover WiFi → LTE
- [ ] Mesurer latences réelles Cat-M
- [ ] Optimiser timeout si nécessaire

---

## 🔗 Références

### Documentation officielle
- [Espressif ESP-IDF - System Time](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/system_time.html)
- [SIMCOM SIM7080G AT Commands](https://simcom.ee/documents/SIM7080G/SIM7000%20Series_AT%20Command%20Manual_V1.07.pdf)

### GitHub LilyGo
- [LilyGo T-SIM7080G Examples](https://github.com/Xinyuan-LilyGO/T-SIM7080G)
- [Issue GPS/Cellular incompatible](https://github.com/Xinyuan-LilyGO/T-SIM7080G/issues)

### Analyses réseau
- NTP over LTE : ~200-250 bytes par sync
- Cat-M latency : 500ms - 10s typique
- Dérive RTC ESP32 : ~4.5 min/12h

---

## 📅 Historique

| Date | Action | Auteur |
|------|--------|--------|
| 2025-01-07 | Analyse complète du problème | Équipe |
| 2025-01-07 | Décision d'attendre CellularManager | Équipe |
| TBD | Implémentation Solution A | TBD |
| TBD | Migration Solution C | TBD |

---

## 💬 Notes additionnelles

### Pourquoi la machine d'état actuelle est bonne

L'approche actuelle (retry adaptatif, expiration 25h, contrôle strict) est **parfaitement adaptée** au LTE Cat-M. Le seul point à corriger est le **blocage de la loop()**.

### Approche recommandée par Espressif

Espressif recommande explicitement d'utiliser :
1. Callbacks asynchrones : `sntp_set_time_sync_notification_cb()`
2. Polling non-bloquant : `sntp_get_sync_status()`

Notre Solution A (polling) suit cette recommandation.

### Considérations futures

Une fois le système en production LTE :
- Monitorer la consommation data réelle NTP
- Ajuster la période de resync (3h actuellement) si nécessaire
- Considérer un RTC externe (DS3231) pour réduire les syncs

---

**Document vivant** - À mettre à jour lors de l'implémentation
