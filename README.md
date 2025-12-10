# 🔋 Topband / EET BMS zu Victron VE.Can Gateway (V117)

Ein ESP32-basiertes Gateway, das Topband-BMS-Batterien (z.B. EET, Power Queen, AmpereTime, etc.) über RS485 ausliest und als intelligentes BMS über CAN-Bus an Victron GX-Geräte (Cerbo, MultiPlus) sendet.

> **Aktuelle Version:** V117 (Stable)

## ⚠️ Disclaimer & Warnung / Haftungsausschluss

**PRIVATE USE ONLY. NO COMMERCIAL USE.**
* **DIY Projekt:** Dies ist ein privates Open-Source-Projekt und steht in keiner geschäftlichen Verbindung zu Topband Battery Co., Ltd. oder Victron Energy.
* **Auf eigene Gefahr:** Die Nutzung erfolgt auf eigenes Risiko. Der Entwickler übernimmt **keine Haftung** für Schäden an Batterien, Wechselrichtern, BMS oder anderer Hardware, die durch die Nutzung dieser Software entstehen könnten.
* **Sicherheit:** Stellen Sie sicher, dass entsprechende DC-Sicherungen verbaut sind. Änderungen an Ladespannungen oder Stromgrenzen können Akkus zerstören, wenn sie falsch eingestellt werden.

---

## 📦 Unterstützte Hardware & Downloads

Da verschiedene Boards unterschiedliche Speicherarchitekturen haben, bieten wir ab V117 angepasste Firmware-Dateien an. **Bitte wählen Sie die richtige Datei für Ihr Board!**

### 1. 🟦 Waveshare ESP32-S3-RS485-CAN (Empfohlen)
Robustes Board mit Gehäuse-Option.
* **Standard Version:** `v117_waveshare_4mb_NoPram.bin`
    * *Für wen:* Für **ALLE** Waveshare S3 Boards. Die sichere Wahl, wenn Sie unsicher sind. Läuft auf 4MB, 8MB und 16MB Versionen stabil.
* **Ultra Version:** `v117_waveshare_16mb_8Pram.bin`
    * *Für wen:* Nur für Boards mit **16MB Flash & 8MB PSRAM**.
    * *Achtung:* Führt auf Standard-Boards zum Bootloop!

### 2. ⬛ LILYGO® T-CAN485 (Classic)
Das ursprüngliche Board (ESP32-WROOM).
* **Datei:** `v117_lilygo_t_can485.bin`

---

## 🚀 Features (V117 Highlights)

### 🔌 Für Victron (CAN-Bus)
* **Vollständige Integration:** Meldet sich als kompatible Batterie am Victron System an.
* **Smart Aggregation:** Fasst bis zu 16 Batterien zu einer großen Bank zusammen.
* **Monitoring Mode:** Der CAN-Versand kann in den Einstellungen deaktiviert werden, um das Gateway als reinen Monitor (ohne Eingriff ins System) zu nutzen.

### 🎨 Web-Interface & Theme Engine
Die "Full" Version bietet nun eine Design-Engine mit **7 verschiedenen Skins**:
* 💎 **Modern Glass:** Transparenter Look mit Status-Glow (Grün=Laden, Orange=Entladen).
* 🔋 **Battery Live:** Hintergrundfarbe ändert sich dynamisch mit dem SOC.
* 👾 **Cyberpunk HUD:** Neon-Look für Technik-Fans.
* 🏗️ **Custom Dashboard:** Karten können per **Drag & Drop** verschoben und in der Größe geändert werden.
* **Plus:** Retro Dark, Simple, Soft UI.

### 🛡️ Sicherheit & Stabilität
* **Hard-Coded Safety:** Ladestrom-Cutoff (0A) bei V > 56.5V oder Temp < 0°C / > 50°C.
* **Watchdog Protection:** Verhindert Abstürze, wenn Batterien nicht antworten oder das WLAN instabil ist.
* **Flash-Schutz:** Diagrammdaten liegen im RAM, Energiewerte werden nur 1x täglich gespeichert.

---

## ⚡ Installation & Flashen

Wir empfehlen das **Espressif Web Tool** (keine Software-Installation nötig).

1.  **Tool öffnen:** Gehen Sie mit **Chrome** oder **Edge** auf [espressif.github.io/esptool-js/](https://espressif.github.io/esptool-js/).
2.  **Verbinden:** Board per USB anschließen, oben auf `Connect` klicken und Port wählen.
    * *Tipp Waveshare:* Ggf. die "BOOT"-Taste beim Einstecken gedrückt halten.
3.  **Vorbereitung (WICHTIG):**
    * Klicken Sie einmal auf **Erase Flash**, um alte, inkompatible Einstellungen zu löschen. Dies verhindert Bootloops bei Versionssprüngen!
4.  **Flashen:**
    * Wählen Sie unten die passende `.bin` Datei aus (Adresse `0x0`).
    * Klicken Sie auf **Program**.
5.  **Starten:**
    * Nach Abschluss auf `Disconnect` klicken.
    * Im Bereich "Console" erneut verbinden (115200 Baud).
    * Reset-Taste am Board (oder Button im Web-Tool) drücken.

---

## 📖 Erste Schritte

1.  **WLAN Einrichten:**
    * Suchen Sie nach dem WLAN **"Victron-Gateway-Setup"**.
    * Verbinden Sie sich. Falls sich die Seite nicht öffnet, rufen Sie `192.168.4.1` auf.
    * Geben Sie Ihre WLAN-Daten ein.
2.  **Zugriff:**
    * Das Dashboard ist nun unter `http://victron-gateway.local` (oder der IP-Adresse) erreichbar.
3.  **Verkabelung:**

| Signal | Board | Batterie (Topband) | Victron (BMS-Can) |
| :--- | :--- | :--- | :--- |
| **RS485 A** | A / D+ | Pin A (oft 1/2 oder 7/8) | - |
| **RS485 B** | B / D- | Pin B (oft 1/2 oder 7/8) | - |
| **CAN H** | H | - | CAN-H |
| **CAN L** | L | - | CAN-L |
| **GND** | GND | GND (Schirmung) | GND (Optional) |

* **WICHTIG:** Den **120 Ohm Widerstand** (DIP Schalter oder Jumper) am Board aktivieren!

---

## 🚦 Diagnose (LED Status)

| Farbe | Verhalten | Bedeutung |
| :--- | :--- | :--- |
| 🔵 **BLAU** | Dauerleuchten | Bootet / Startet WiFi |
| 🟢 **GRÜN** | Blinkt | **System OK** (Herzschlag) |
| 🔴 **ROT** | Dauerleuchten | **Fehler:** Keine Batterie gefunden oder CAN-Kabel ab. |
| 🔴 **ROT** | Schnell blinkend | **ALARM:** Überspannung (>56.5V)! Not-Aus. |
| 🟣 **LILA** | Blinkt | **Schutz:** Temperatur zu hoch/niedrig. |

---

## ❓ FAQ & Troubleshooting

**Mein Waveshare Board startet ständig neu (Bootloop)?**
Sie haben vermutlich eine Version geflasht, die für den Speicherchip zu groß ist, oder alte Einstellungen stören.
1. Nutzen Sie die **"Standard / Safe" (4MB)** Version der Firmware.
2. Führen Sie vor dem Flashen unbedingt ein **"Erase Flash"** durch.

**Ich habe keinen Victron, kann ich das Gateway trotzdem nutzen?**
Ja! Gehen Sie in die Einstellungen und deaktivieren Sie den Haken bei **"Enable Victron CAN"**. Die Fehlermeldung im Dashboard verschwindet dann, und das Gerät arbeitet als reiner Monitor.

**Werte im Diagramm sind nach Neustart weg?**
Das ist Absicht. Um den Speicherchip zu schonen, liegen die hochauflösenden 48h-Kurven nur im RAM. Die kWh-Zähler (Balkendiagramm) werden jedoch dauerhaft gespeichert.

**Selbst Kompilieren (Arduino IDE)?**
Falls Sie den Code selbst anpassen wollen, nutzen Sie bitte folgende Einstellungen, sonst stürzt der ESP32-S3 ab:
* **Board:** `ESP32S3 Dev Module`
* **Partition Scheme:** `Huge APP (3MB No OTA/1MB SPIFFS)`
* **PSRAM:** `OPI PSRAM` (nur bei 16MB/8MB Boards) oder `Disabled`.

![Graph Preview](https://github.com/user-attachments/assets/42b3407d-c421-48cc-9c61-e250e72559f5)

---

### 👨‍💻 Development Team
* **Lead Developer & Testing:** atomi23
* **Co-Pilot & Code-Architect:** Gemini (AI)
