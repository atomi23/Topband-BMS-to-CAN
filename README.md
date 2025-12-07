# **🔋 Topband / EET BMS zu Victron VE.Can Gateway**

**Ein ESP32-basiertes Gateway, das Topband-BMS-Batterien (z.B. EET, Power Queen, AmpereTime, etc.) über RS485 ausliest und als intelligentes BMS über CAN-Bus an Victron GX-Geräte (Cerbo, MultiPlus) sendet.**

![Graph Preview](https://github.com/user-attachments/assets/42b3407d-c421-48cc-9c61-e250e72559f5)

## **⚠️ Disclaimer & Warnung / Haftungsausschluss**

**PRIVATE USE ONLY. NO COMMERCIAL USE.**

* **DIY Projekt:** Dies ist ein privates Open-Source-Projekt und steht in keiner geschäftlichen Verbindung zu Topband Battery Co., Ltd. oder Victron Energy.
* **Auf eigene Gefahr:** Die Nutzung erfolgt auf eigenes Risiko. Der Entwickler übernimmt **keine Haftung** für Schäden an Batterien, Wechselrichtern, BMS oder anderer Hardware, die durch die Nutzung dieser Software entstehen könnten.
* **Sicherheit:** Stellen Sie sicher, dass entsprechende DC-Sicherungen verbaut sind. Änderungen an Ladespannungen oder Stromgrenzen können Akkus zerstören, wenn sie falsch eingestellt werden.

---

## **📦 Verfügbare Versionen**

Dieses Projekt bietet in den Releases nun zwei verschiedene Firmware-Varianten an, je nach Einsatzzweck:

### **1. 🌐 FULL / LATEST (Empfohlen)**
Die komfortable Version mit Web-Oberfläche, WLAN und detaillierter Analyse.
* **Web-Dashboard:** Live-Daten, Zellspannungen, Logs.
* **High-Res Diagramme:** 48-Stunden Leistungsdiagramm ("Welle") & 7-Tage Energie-Historie (Balken).
* **Flash-Schutz:** Speichert Diagrammdaten im RAM und Energiewerte nur 1x täglich, um den ESP32-Chip zu schonen.
* **Konnektivität:** MQTT Unterstützung & SD-Karten Logging.
* **Konfigurierbar:** Über Browser einstellbar (NTP, BMS-Anzahl, etc.).

### **2. 🥷 STEALTH / PURE**
Eine "Headless" Version für maximale Stabilität und Sicherheit. **Kein WLAN, kein Webserver.**
* **Plug & Play:** Anschließen und läuft. Startzeit < 1 Sekunde.
* **Auto-Detect:** Scannt automatisch alle 16 Adressen nach Batterien.
* **Hard-Coded Safety (15S LiFePO4):** * Strikte Sicherheitsgrenzen basierend auf dem Datenblatt.
    * **Not-Aus:** Ladestrom 0A bei V > 56.5V oder Temp < 0°C / > 50°C.
* **Diagnose:** Status-Anzeige ausschließlich über die Onboard-LED.

---

## **🚀 Features (Detail)**

### **🔌 Für Victron (CAN-Bus)**
* **Vollständige Integration:** Meldet sich als kompatible Batterie am Victron System an.
* **DVCC Support:** Übermittelt dynamisch Ladespannungslimit (CVL), Ladestromlimit (CCL) und Entladestromlimit (DCL).
* **Smart Aggregation:** Fasst **mehrere BMS-Module** (bis zu 16) zu einer großen Batteriebank zusammen (Summiert Strom & Kapazität, mittelt Spannung & SOC).
* **Balancing:** Reduziert den Ladestrom automatisch, wenn der Akku voll wird oder eine Zelle driftet.

### **📊 Web-Interface (Nur "Full" Version)**
* **48h Power-Graph:** Zeigt Lade- (Grün) und Entladeleistung (Orange) der letzten 48 Stunden in hoher Auflösung (3-Minuten Intervalle).
* **7-Tage Historie:** Balkendiagramm für geladene und entladene Energie (kWh) der letzten Woche.
* **SD-Karte:** Manager zum Herunterladen und Löschen von `log.csv` Dateien direkt im Browser.
* **Live-Status:** Klare Anzeige von Systemzustand, Fehlern (CAN/SD) und Einzelzellenspannungen.

---

## **🛠 Unterstützte Hardware**

Der Code ist optimiert für ESP32-Boards mit isoliertem RS485 und CAN Transceiver.

### **Empfohlene Boards:**
1.  **LILYGO® T-CAN485** (ESP32 Classic)
    * *Plug & Play, kompakte Bauform.*
2.  **Waveshare ESP32-S3-RS485-CAN** (ESP32-S3)
    * *Robustes Industriegehäuse möglich.*

### **Verkabelung:**

| Signal | Board | Batterie (Topband) | Victron (BMS-Can) |
| :--- | :--- | :--- | :--- |
| **RS485 A** | A / D+ | Pin A (oft Pin 1/2 oder 7/8) | - |
| **RS485 B** | B / D- | Pin B (oft Pin 1/2 oder 7/8) | - |
| **CAN H** | H | - | CAN-H |
| **CAN L** | L | - | CAN-L |
| **GND** | GND | GND (Optional/Shield) | GND (Optional) |

**WICHTIG:** Der CAN-Bus muss am ESP32 und am Victron terminiert werden (**120 Ohm Widerstand** einschalten/stecken!). Ohne Widerstand keine Kommunikation!

---

## **⚡ Installation**

### **1. Firmware herunterladen**
Laden Sie die passende `.bin` Datei aus den [Releases](https://github.com/atomi23/Topband-BMS-to-CAN/releases) herunter.
* `VictronGateway_Full_V98.bin` (Mit Webinterface)
* `VictronGateway_Stealth_V101.bin` (Ohne WLAN, reine Bridge)

### **2. Flashen über Web-Tool**
1.  Verbinden Sie das ESP32-Board per USB mit dem PC.
    * *Hinweis Waveshare S3:* Halten Sie beim Einstecken die "BOOT"-Taste gedrückt.
2.  Öffnen Sie [**web.esphome.io**](https://web.esphome.io/) oder den [**Adafruit Web Flasher**](https://adafruit.github.io/Adafruit_WebSerial_ESPTool/) (Chrome oder Edge Browser).
3.  Klicken Sie auf **Connect** und wählen Sie den COM-Port.
4.  Wählen Sie die heruntergeladene `.bin` Datei aus.
5.  Klicken Sie auf **Install/Program**.

---

## **📖 Bedienung & Diagnose**

### **A. "Full" Version (Webinterface)**
1.  Nach dem ersten Start öffnet der ESP32 einen Hotspot: **Victron-Gateway-Setup**.
2.  Verbinden (Passwort: leer lassen oder `12345678`).
3.  WLAN konfigurieren (192.168.4.1 aufrufen, falls kein Popup erscheint).
4.  Nach Neustart ist das Dashboard unter `http://victron-gateway.local` erreichbar.

### **B. "Stealth" Version (LED Codes)**
Da diese Version kein Display hat, nutzen Sie die LED zur Diagnose:

| Farbe | Verhalten | Bedeutung | Maßnahme |
| :--- | :--- | :--- | :--- |
| 🔵 **BLAU** | Dauerleuchten | **Startet** | System bootet. |
| 🟠 **ORANGE** | Leuchten | **Scannt** | Sucht nach BMS (Adressen 0-15). |
| 🟢 **GRÜN** | Blinkt | **Betrieb OK** | Daten werden an Victron gesendet. |
| 🔴 **ROT** | Dauerleuchten | **Fehler** | Kein BMS gefunden oder CAN-Fehler. Kabel prüfen! |
| 🔴 **ROT** | **Schnell blinkend** | **ALARM** | **Überspannung (>56.5V)!** Not-Abschaltung aktiv. |
| 🟣 **LILA** | Blinkt | **Temp-Schutz** | Zu kalt (< 0°C) oder zu heiß (> 50°C). |

---

## **❓ FAQ**

**Die Werte im Diagramm sind nach einem Neustart weg?**
Ja, das ist Absicht (bei der Full Version). Die hochauflösenden Diagramm-Daten liegen nur im RAM, um den Flash-Speicher des ESP32 nicht durch ständiges Schreiben zu zerstören. Die kWh-Zähler (7-Tage Historie) bleiben jedoch erhalten (Speicherung 1x täglich um 00:00 Uhr).

**Ich sehe "CAN FEHLER" im Dashboard / Rote LED?**
* Ist das Kabel zum Victron korrekt (H an H, L an L)?
* Ist der **120 Ohm Abschlusswiderstand** am ESP32-Board aktiviert?
* Ist am Victron BMS-Can Port der blaue Terminator gesteckt?

**Mein BMS wird nicht gefunden?**
* Stimmt die ID am BMS (DIP-Schalter)? Das Gateway scannt alle IDs, aber bei schlechter Verkabelung (RS485 A/B vertauscht) wird nichts gefunden.

## **🤝 Mitwirken**
Fehler gefunden oder Ideen für Verbesserungen? Erstellt gerne ein Issue oder einen Pull Request!
