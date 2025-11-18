# ESPHome Topband BMS zu Victron VE.Can Gateway

Dieses Projekt realisiert ein **RS485-zu-CAN-Bus Gateway** auf Basis eines **ESP32 (LILYGO T-CAN485)**. Es ermöglicht die Kommunikation zwischen **Topband BMS** basierten Batteriespeichern und **Victron GX-Geräten** (Cerbo GX, MultiPlus, etc.).

> **💡 Hinweis zu Node-RED:**
> Dieses Projekt entstand ursprünglich aus einem **Node-RED Flow**. Wenn Sie statt eines ESP32 lieber eine reine Software-Lösung (z.B. auf Venus OS Large) bevorzugen, finden Sie den ursprünglichen Node-RED Flow ebenfalls in diesem Repository (siehe Datei `flow.json` oder Ordner `/nodered`).

Das Gateway liest die Daten der Batterien über RS485 aus, aggregiert diese (bei Verwendung mehrerer Module) und emuliert das **Pylontech-Protokoll** auf dem VE.Can-Bus. Dadurch erkennt das Victron-System den Speicher als kompatible Batterie inkl. SOC, Spannung, Strom und dynamischen Lade-/Entladelimits.

## 🚀 Features

* **Plug & Play Hardware:** Entwickelt für das LILYGO T-CAN485 Board.
* **Multi-BMS Support:** Unterstützt bis zu 16 parallel geschaltete BMS-Module.
* **Intelligente Aggregation:** Berechnet Gesamtkapazität, Durchschnittsspannung und Sicherheitslimits basierend auf allen angeschlossenen Modulen.
* **Web-Interface:** Einfache Konfiguration der Anzahl der Module und Basis-Ströme direkt im Browser.
* **Status-LED:** Visuelles Feedback über den Systemzustand (Verbindung, Fehler, Datentransfer).
* **Home Assistant Ready:** Alle Daten stehen optional auch direkt in Home Assistant via ESPHome-API zur Verfügung.

## 🛠 Hardware

* **LILYGO® T-CAN485** (ESP32 Board mit integriertem RS485 und CAN Transceiver)
* Verbindungskabel für RS485 (zum Akku) und CAN (zum Victron GX)

---

## ⚡ Installationsanleitung

### Voraussetzungen
Sie benötigen **ESPHome**.
* **Option A (Empfohlen):** Als Add-on in Home Assistant.
* **Option B:** Installation via Kommandozeile auf Ihrem PC.

### Schritt 1: Dateien vorbereiten

Laden Sie die beiden Dateien aus diesem Repository herunter und speichern Sie sie in einem neuen Ordner (z. B. `victron-gateway`):

1.  `topband-gateway.yaml` (Die ESPHome Konfiguration)
2.  `victron_topband_gateway.h` (Der C++ Code für die Logik)

### Schritt 2: OTA-Passwort (Optional)

Um Updates sicher über WLAN durchführen zu können, verweist die Konfiguration auf ein `secret`.
* **Variante A:** Erstellen Sie eine `secrets.yaml` in Ihrem ESPHome-Ordner mit dem Inhalt: `ota_password: "IhrSicheresPasswort"`.
* **Variante B:** Öffnen Sie die `topband-gateway.yaml` und löschen Sie die Zeile `password: !secret ota_password`, wenn Sie kein Passwort wünschen.

### Schritt 3: Flashen

1.  Verbinden Sie das **LILYGO T-CAN485 Board** per USB mit dem Computer.
2.  Öffnen Sie das ESPHome Dashboard.
3.  Erstellen Sie ein "Neues Gerät" (*New Device*).
4.  Überspringen Sie den Einrichtungs-Assistenten und klicken Sie direkt auf **"Install"**.
5.  Wählen Sie die Option **"Manually"** (oder "Select File").
6.  Wählen Sie Ihre heruntergeladene `topband-gateway.yaml` Datei aus.
7.  ESPHome kompiliert nun den Code und flasht ihn auf das Board.

### Schritt 4: WLAN einrichten (Captive Portal)

Nach dem ersten Flashen findet das Board noch kein WLAN. Es startet daher einen eigenen Access Point zur Einrichtung.

1.  Suchen Sie auf Ihrem Handy oder Laptop nach dem WLAN **"BMS-Gateway-Setup"**.
2.  Verbinden Sie sich (Passwort: `setup-bms-123`).
3.  Es öffnet sich automatisch eine Webseite (Captive Portal). Falls nicht, rufen Sie `192.168.4.1` im Browser auf.
4.  Wählen Sie Ihr Heim-WLAN aus der Liste und geben Sie Ihr WLAN-Passwort ein.
5.  Das Board speichert die Daten, startet neu und verbindet sich ab jetzt automatisch mit Ihrem Netzwerk.

### Schritt 5: Physische Installation & Verkabelung

> **⚠️ WICHTIG:** Achten Sie unbedingt auf die korrekte Polarität der Kabel!

**1. RS485 (Verbindung zum Akku)**
Verbinden Sie die Anschlüsse A/B (bzw. +/-) des Boards mit den entsprechenden Anschlüssen Ihres ersten Topband BMS.
* *Hinweis:* Stellen Sie die DIP-Schalter an den Akkus binär ein (Akku 0 = alle aus, Akku 1 = Schalter 1 an, usw.).

**2. CAN-Bus (Verbindung zum Victron GX)**
Verbinden Sie CAN-H und CAN-L des Boards mit dem **BMS-Can** Port Ihres Victron GX-Geräts (z.B. Cerbo GX).

**3. Terminierung (Abschlusswiderstände)**
Ein CAN-Bus muss an **beiden physikalischen Enden** terminiert sein, sonst kommt es zu Kommunikationsfehlern.
* **Victron-Seite:** Nutzen Sie den blauen 120-Ohm-Terminator-Stecker (im Lieferumfang des GX enthalten).
* **ESP32-Seite:** Aktivieren Sie den 120-Ohm-Widerstand auf dem LILYGO-Board. Dies geschieht meist über einen kleinen DIP-Schalter oder Jumper auf der Platine (oft markiert mit "120R" oder "TERM").

### Schritt 6: Victron GX konfigurieren

Damit der Victron das Gateway erkennt, muss die CAN-Geschwindigkeit angepasst werden.

1.  Öffnen Sie die **Remote Console** Ihres GX-Geräts.
2.  Navigieren Sie zu: `Menü` -> `Einstellungen` -> `Dienste`.
3.  Wählen Sie den genutzten CAN-Port (z.B. *VE.Can 1*).
4.  Stellen Sie das Profil auf: **"CAN-Bus BMS (500 kbit/s)"**.
5.  Starten Sie das GX-Gerät neu: `Menü` -> `Einstellungen` -> `Allgemein` -> `Neustart`.

### Schritt 7: Gateway konfigurieren

1.  Suchen Sie die IP-Adresse des ESP32 (im Router oder im ESPHome-Log).
2.  Öffnen Sie die IP-Adresse in einem Webbrowser.
3.  Sie sehen nun das Web-Interface des Gateways.
4.  Stellen Sie den Regler **"Anzahl der BMS-Module"** auf Ihre tatsächliche Anzahl ein.
5.  Passen Sie bei Bedarf die Basis-Ströme (Lade-/Entladelimit) an.

✅ **Fertig!** Die RGB-LED am Board sollte nun **grün** pulsieren, und das Victron-System sollte die Batterie in der Geräteliste anzeigen.

---

## 🤝 Unterstützung & Contributing

Dies ist ein Open-Source-Projekt und lebt von der Community!

* **Fehler gefunden?** Erstelle gerne ein "Issue" hier auf GitHub.
* **Verbesserungsvorschläge?** Pull Requests sind herzlich willkommen!

Lass uns zusammen daran arbeiten, proprietäre Speicher-Systeme offener und kompatibler zu machen!
