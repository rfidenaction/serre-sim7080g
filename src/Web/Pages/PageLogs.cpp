// Web/Pages/PageLogs.cpp
#include "Web/Pages/PageLogs.h"

String PageLogs::getHtml(bool gsmActive, const LogFileStats& stats)
{
    String warningMessage = "";
    String downloadDisabled = "";
    String gsmActiveJs = gsmActive ? "true" : "false";
    
    if (gsmActive) {
        warningMessage = R"HTML(
<div class="card warning">
  <p style="font-size: 1.5em;">⚠️ ATTENTION ⚠️</p>
  <p>Le GSM est actuellement actif.</p>
  <p>Désactivez le GSM avant de <strong>télécharger</strong> les logs pour éviter :</p>
  <ul style="text-align: left; margin: 20px auto; max-width: 400px;">
    <li>Téléchargement de données via réseau cellulaire (coût)</li>
    <li>Saturation de la connexion GSM</li>
  </ul>
  <p><strong>Retournez à la page principale et désactivez le GSM.</strong></p>
</div>
)HTML";
        downloadDisabled = "disabled";
    }
    
    String statsInfo = "";
    
    if (stats.exists) {
        String statsLine = 
            "Taille : " + String(stats.sizeMB, 2) + " MB (" +
            String(stats.percentFull, 3) + "% de " +
            String(stats.totalGB, 2) + " Go)";
        
        statsInfo = 
            "<div class=\"card\">"
            "<p style=\"font-size: 1.3em;\">📊 Informations sur les données</p>"
            "<p class=\"subtext\">" + statsLine + "</p>"
            "<p style=\"font-size: 0.9em;\">Fichier existant : Oui</p>"
            "</div>";
    } else {
        String availableSpace = "Espace disponible : " + String(stats.totalGB, 2) + " Go";
        
        statsInfo = 
            "<div class=\"card\">"
            "<p style=\"font-size: 1.3em;\">📊 Informations sur les données</p>"
            "<p class=\"subtext\">Aucune donnée enregistrée</p>"
            "<p style=\"font-size: 0.9em;\">Fichier existant : Non</p>"
            "<p style=\"font-size: 0.9em;\">" + availableSpace + "</p>"
            "</div>";
    }

    String html = R"HTML(
<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Gestion des Logs - Serre de Marie-Pierre</title>
<style>
body { font-family: Arial; background: #d32f2f; color: white; text-align: center; margin: 0; padding: 20px; }
h1 { background: #b71c1c; padding: 20px; border-radius: 10px; }
.card { background: rgba(255,255,255,0.2); margin: 20px auto; max-width: 600px; padding: 20px; border-radius: 15px; }
.card.warning { background: rgba(255,255,0,0.3); border: 3px solid #ffeb3b; }
.subtext { font-size: 1.2em; margin-top: 15px; }
button { 
  background: #1976d2; 
  color: white; 
  border: none; 
  padding: 15px 30px; 
  font-size: 1.2em; 
  border-radius: 10px; 
  cursor: pointer; 
  margin: 10px;
  min-width: 250px;
}
button:hover:not(:disabled) { background: #0d47a1; }
button:disabled { background: #666; cursor: not-allowed; opacity: 0.5; }
button.danger { background: #c62828; }
button.danger:hover:not(:disabled) { background: #8e0000; }
.back-link { 
  display: inline-block; 
  margin-top: 30px; 
  color: white; 
  text-decoration: underline; 
  font-size: 1.1em;
}
</style>

<script>
const gsmActive = )HTML" + gsmActiveJs + R"HTML(;

async function downloadLogs() {
  if (gsmActive) {
    alert('❌ GSM actif !\n\nDésactivez le GSM avant de télécharger les données.');
    return;
  }

  try {
    const response = await fetch('/logs/download');
    if (!response.ok) {
      const text = await response.text();
      alert('❌ Erreur téléchargement :\n\n' + text);
      return;
    }

    const blob = await response.blob();
    const url = window.URL.createObjectURL(blob);

    const link = document.createElement('a');
    link.href = url;
    link.download = 'datalog.csv';
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);

    window.URL.revokeObjectURL(url);
  } catch (error) {
    alert('❌ Erreur réseau : ' + error);
  }
}

function clearLogs() {
  if (confirm('⚠️ ATTENTION ⚠️\n\nÊtes-vous ABSOLUMENT SÛR de vouloir supprimer TOUTES les données historiques ?\n\nCette action est IRRÉVERSIBLE !')) {
    if (confirm('Dernière confirmation :\n\nToutes les données seront DÉFINITIVEMENT perdues.\n\nContinuer ?')) {
      fetch('/logs/clear', { method: 'POST' })
        .then(response => {
          if (response.ok) {
            alert('✅ Historique supprimé avec succès');
            location.reload();
          } else {
            alert('❌ Erreur lors de la suppression');
          }
        })
        .catch(error => {
          alert('❌ Erreur : ' + error);
        });
    }
  }
}
</script>
</head>
<body>

<h1>🗂️ Gestion des Logs</h1>

)HTML" + warningMessage + statsInfo + R"HTML(

<div class="card">
  <p style="font-size: 1.3em;">Téléchargement des données</p>
  <p class="subtext">Télécharge toutes les données historiques au format CSV</p>
  <button onclick="downloadLogs()" )HTML" + downloadDisabled + R"HTML(>📥 Télécharger les données</button>
</div>

<div class="card">
  <p style="font-size: 1.3em;">Suppression des données</p>
  <p class="subtext">⚠️ DANGER : Supprime définitivement tout l'historique</p>
  <p style="font-size: 0.9em; color: #ffeb3b;">Cette action est IRRÉVERSIBLE</p>
  <button class="danger" onclick="clearLogs()">🗑️ Effacer les données</button>
</div>

<a href="/" class="back-link">← Retour à la page principale</a>

</body>
</html>
)HTML";

    return html;
}
