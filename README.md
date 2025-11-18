ESPHome Topband BMS zu Victron VE.Can Gateway
Dieses Projekt realisiert ein RS485-zu-CAN-Bus Gateway auf Basis eines ESP32 (LILYGO T-CAN485). Es ermöglicht die Kommunikation zwischen Topband BMS basierten Batteriespeichern und Victron GX-Geräten (Cerbo GX, MultiPlus, etc.).

Das Gateway liest die Daten der Batterien über RS485 aus, aggregiert diese (bei Verwendung mehrerer Module) und emuliert das Pylontech-Protokoll auf dem VE.Can-Bus. Dadurch erkennt das Victron-System den Speicher als kompatible Batterie inkl. SOC, Spannung, Strom und dynamischen Lade-/Entladelimits.

🚀 Features
Plug & Play Hardware: Entwickelt für das LILYGO T-CAN485 Board.

Multi-BMS Support: Unterstützt bis zu 16 parallel geschaltete BMS-Module.

Intelligente Aggregation: Berechnet Gesamtkapazität, Durchschnittsspannung und Sicherheitslimits basierend auf allen angeschlossenen Modulen.

Web-Interface: Einfache Konfiguration der Anzahl der Module und Basis-Ströme direkt im Browser.

Status-LED: Visuelles Feedback über den Systemzustand (Verbindung, Fehler, Datentransfer).

Home Assistant Ready: Alle Daten stehen optional auch direkt in Home Assistant via ESPHome-API zur Verfügung.

🛠 Hardware
LILYGO® T-CAN485 (ESP32 Board mit integriertem RS485 und CAN Transceiver)

Verbindungskabel für RS485 (zum Akku) und CAN (zum Victron GX)

⚡ Kurzanleitung: Von 0 zum fertigen Gateway
Voraussetzungen
Sie benötigen ESPHome.

Option A (Empfohlen): Als Add-on in Home Assistant.

Option B: Installation via Kommandozeile auf Ihrem PC.

Schritt 1: Dateien erstellen
Erstellen Sie einen neuen Ordner auf Ihrem Computer oder in Ihrem ESPHome-Verzeichnis, z. B. victron-gateway. Kopieren Sie die beiden Hauptdateien aus diesem Repository in diesen Ordner:

topband-gateway.yaml (Die ESPHome Konfiguration)

victron_topband_gateway.h (Der C++ Code für die Logik)

Schritt 2: OTA-Passwort festlegen (Optional, aber empfohlen)
Um Updates sicher über WLAN durchführen zu können, nutzen wir secrets. Erstellen Sie (falls nicht vorhanden) eine Datei namens secrets.yaml im Hauptverzeichnis Ihres ESPHome-Setups und fügen Sie hinzu:

YAML

ota_password: "IhrSicheresPasswort"
Hinweis: Wenn Sie kein Passwort wünschen, entfernen Sie die Zeile password: !secret ota_password aus der topband-gateway.yaml.

Schritt 3: Flashen
Verbinden Sie das LILYGO T-CAN485 Board per USB mit dem Computer.

Öffnen Sie das ESPHome Dashboard.

Erstellen Sie ein "Neues Gerät" (New Device).

Überspringen Sie den Assistenten und klicken Sie auf "Install".

Wählen Sie "Manually" und wählen Sie Ihre topband-gateway.yaml Datei aus.

ESPHome kompiliert nun den Code und flasht ihn auf das Board.

Schritt 4: WLAN einrichten (Captive Portal)
Nach dem ersten Flashen findet das Board noch kein WLAN. Es startet daher einen eigenen Access Point.

Suchen Sie auf Ihrem Handy oder Laptop nach dem WLAN "BMS-Gateway-Setup".

Verbinden Sie sich (Passwort: setup-bms-123).

Es öffnet sich automatisch eine Webseite (Captive Portal). Falls nicht, rufen Sie 192.168.4.1 auf.

Wählen Sie Ihr Heim-WLAN aus der Liste und geben Sie Ihr WLAN-Passwort ein.

Das Board speichert die Daten, startet neu und verbindet sich ab jetzt automatisch mit Ihrem Netzwerk.

Schritt 5: Physische Installation & Verkabelung
⚠️ ACHTUNG: Achten Sie unbedingt auf die korrekte Polarität!

RS485: Verbinden Sie A/B (bzw. +/-) des Boards mit den entsprechenden Anschlüssen Ihres ersten Topband BMS.

Tipp: Stellen Sie die DIP-Schalter am BMS binär ein (Akku 0 = alle aus, Akku 1 = Schalter 1 an, usw.).

CAN-Bus: Verbinden Sie CAN-H und CAN-L des Boards mit dem BMS-Can Port Ihres Victron GX-Geräts.

Terminierung (WICHTIG): Ein CAN-Bus muss an beiden Enden terminiert sein.

Victron-Seite: Nutzen Sie den blauen 120-Ohm-Terminator-Stecker (im Lieferumfang des GX).

ESP32-Seite: Aktivieren Sie den 120-Ohm-Widerstand auf dem LILYGO-Board (meist ein kleiner DIP-Schalter oder Jumper markiert mit "120R" oder "TERM").

Schritt 6: Victron GX konfigurieren
Öffnen Sie die Remote Console Ihres GX-Geräts.

Navigieren Sie zu: Menü -> Einstellungen -> Dienste.

Wählen Sie den genutzten CAN-Port (z.B. VE.Can 1).

Stellen Sie das Profil auf: "CAN-Bus BMS (500 kbit/s)".

Starten Sie das GX-Gerät neu: Menü -> Einstellungen -> Allgemein -> Neustart.

Schritt 7: Gateway konfigurieren
Suchen Sie die IP-Adresse des ESP32 (im Router oder ESPHome-Log).

Öffnen Sie die IP im Browser. Sie sehen nun das Web-Interface.

Stellen Sie den Regler "Anzahl der BMS-Module" auf Ihre tatsächliche Anzahl ein.

Passen Sie bei Bedarf die Basis-Ströme (Lade-/Entladelimit) an.

✅ Fertig! Die RGB-LED sollte nun grün pulsieren, und das Victron-System sollte die Batterie anzeigen.

🧩 Node-RED Alternative
Dieses Projekt entstand ursprünglich aus einem Node-RED Flow. Für Nutzer, die lieber mit Node-RED arbeiten oder die Logik schnell prototypen möchten, liegt der ursprüngliche Flow ebenfalls in diesem Repository (siehe Ordner /nodered oder Datei flow.json).

Der Node-RED Flow bietet die gleiche Logik, benötigt aber externe Hardware (USB-RS485 Adapter) und eine laufende Node-RED Instanz (z.B. auf dem GX-Gerät via Venus OS Large). Die ESP32-Lösung wird jedoch für den produktiven Dauereinsatz empfohlen, da sie stabiler und unabhängiger läuft.

🤝 Unterstützung & Contributing
Dies ist ein Open-Source-Projekt und lebt von der Community!

Fehler gefunden? Erstelle gerne ein "Issue" hier auf GitHub.

Verbesserungsvorschläge? Pull Requests sind herzlich willkommen! Egal ob Code-Optimierungen, Unterstützung für weitere BMS-Varianten oder Dokumentations-Updates.

Lass uns zusammen daran arbeiten, proprietäre Speicher-Systeme offener und kompatibler zu machen!
