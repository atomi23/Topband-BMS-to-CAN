# 🔋 Topband / EET BMS zu Victron VE.Can Gateway (V117)

Ein ESP32-basiertes Gateway, das Topband-BMS-Batterien (z.B. EET, Power Queen, AmpereTime, etc.) über RS485 ausliest und als intelligentes BMS über CAN-Bus an Victron GX-Geräte (Cerbo, MultiPlus) sendet. [web:22]

> **Aktuelle Version:** V117 (Stable) [web:22]

---

## 📸 Dashboard Preview

Hier ein Einblick in die Web-Oberfläche (Power-Graph & Live-Werte). [web:22]

> Hinweis: Das aktuelle Design (V117) bietet Dark-Mode, Glas-Effekte, 7 Themes und Drag & Drop. [web:22]

---

## ⚠️ Disclaimer & Warnung

**PRIVATE USE ONLY. NO COMMERCIAL USE.** [web:22]

- **DIY Projekt:** Privates Open-Source-Projekt, keine geschäftliche Verbindung zu Topband Battery Co., Ltd. oder Victron Energy. [web:22]  
- **Auf eigene Gefahr:** Nutzung auf eigenes Risiko, keine Haftung für Schäden an Batterien, Wechselrichtern, BMS oder anderer Hardware. [web:22]  
- **Sicherheit:** DC-Sicherungen verbauen, falsche Parameter können Akkus zerstören. [web:22]

---

## 📦 Downloads (Wähle deine Version!)

Da Waveshare verschiedene Hardware-Versionen verkauft, gibt es ab V117 angepasste Firmware-Dateien. [web:22]

### 🌐 FULL VERSION (Mit Webinterface & WLAN)

Die komfortable Lösung mit Dashboard, Diagrammen und MQTT. [web:22]

- `v117_waveshare_4mb_NoPram.bin` (Standard / Safe)  
  - Empfohlen, läuft auf allen Waveshare S3 Boards (4MB/8MB/16MB) stabil. [web:22]
- `v117_waveshare_16mb_8Pram.bin` (Ultra / High-End)  
  - Nur für Boards mit 16MB Flash & 8MB OPI PSRAM. [web:22]
- `v117_lilygo_t_can485.bin`  
  - Für das LILYGO T-CAN485 Board. [web:27][web:39]

### 🥷 STEALTH VERSION (Ohne WLAN / Nur Kabel)

Sehr robust, startet in unter 1 Sekunde, keine Konfiguration nötig. [web:22]

- `STEALTH_V117.waveshare.bin` (Universal S3) – für alle Waveshare S3 Boards. [web:22]  
- `STEALTH_V117.lilygo_t_can485.bin` – für das schwarze T-CAN485 Board. [web:27][web:39]

---

<details>
<summary><strong>🚀 Features & Highlights</strong></summary>

### 🔌 Für Victron (CAN-Bus)

- Vollständige Integration: Meldet sich als kompatible Batterie am Victron-System an. [web:22]  
- Smart Aggregation: Fasst bis zu 16 Batterien zu einer Bank zusammen (Strom & Kapazität werden summiert). [web:22]  
- Monitoring Mode (Full): CAN-Versand deaktivierbar, Gateway arbeitet nur als Monitor. [web:22]

### 🎨 Web-Interface (nur Full)

Design-Engine mit sieben Skins. [web:22]

- Modern Glass, Battery Live, Cyberpunk HUD, Custom Dashboard (Drag & Drop), Retro Dark, Simple, Soft UI. [web:22]

### 🛡️ Sicherheit & Stabilität

- Hard-Coded Safety: Ladestrom-Cutoff bei Spannung über 56,5 V oder Temperatur unter 0 °C bzw. über 50 °C. [web:22]  
- Watchdog Protection bei fehlender Antwort der Batterien. [web:22]  
- Flash-Schutz: Diagrammdaten nur im RAM, Energiewerte werden einmal täglich gespeichert. [web:22]

</details>

---

<details>
<summary><strong>🔌 Verkabelung (Pinout & Anleitung)</strong></summary>

Wenn unsicher, an den Farben eines TIA‑568B-Netzwerkkabels orientieren. [web:22]

### Übersichtsschema (Mermaid)

graph LR
subgraph BATTERIE ["🔋 EET / Topband Batterie"]
direction TB
B_Port["Port: RS485 / Link Port
(RJ45 Buchse)"]
end

text
subgraph GATEWAY ["📟 ESP32 Gateway"]
    direction TB
    RS485_A["Klemme: A (oder D+)"]
    RS485_B["Klemme: B (oder D-)"]
    CAN_H["Klemme: H"]
    CAN_L["Klemme: L"]
end

subgraph VICTRON ["🔵 Victron Cerbo / GX"]
    direction TB
    V_Port["Port: BMS-Can<br>(Nicht VE.Can!)"]
end

%% Verkabelung
B_Port -- "Pin 1 (Orange/Weiß)" --> RS485_A
B_Port -- "Pin 2 (Orange)" --> RS485_B
CAN_H -- "Weiß/Braun" --> V_Port
CAN_L -- "Braun" --> V_Port
text

### Schritt 1: Kabel zur Batterie (RS485)

- Normales LAN-Kabel, einen Stecker abschneiden, offene Adern an grünes RS485-Terminal am ESP32. [web:22]  
- Batterie RJ45 Pin 1 – Orange/Weiß → ESP32 Klemme A (A+). [web:22]  
- Batterie RJ45 Pin 2 – Orange → ESP32 Klemme B (B−). [web:22]  
- Wenn LED am Gateway dauerhaft rot: A und B tauschen (Orange ↔ Orange/Weiß). [web:22]

### Schritt 2: Kabel zum Victron (CAN-Bus)

- Gateway mit **BMS-Can** des Victron verbinden (nicht VE.Can). [web:22][web:22]  
- Victron RJ45 Pin 7 – Braun/Weiß → ESP32 Klemme H (High). [web:22]  
- Victron RJ45 Pin 8 – Braun → ESP32 Klemme L (Low). [web:22]

### Schritt 3: Einstellungen (DIP Switches)

- Am ESP32-Board: 120‑Ohm-Abschlusswiderstand aktivieren (Schalter/Jumper auf ON). [web:22]  
- An der Batterie: DIP-Schalter auf Adresse 1 (meist: Schalter 1 = ON, Rest = OFF). [web:22]  
- Am Victron: Blauen Terminator in den zweiten BMS‑Can‑Port stecken. [web:22]

</details>

---

<details>
<summary><strong>⚡ Installation & Flashen (Web-Tool)</strong></summary>

Empfohlenes Tool: **Espressif Web Tool** (läuft direkt im Browser). [web:13][web:16]  
Link: https://espressif.github.io/esptool-js/ [web:13]

- Browser: Google Chrome oder Edge verwenden. [web:13]  
- Verbinden: Board per USB anschließen, „Connect“ klicken und den richtigen Port wählen. [web:13]  
- Tipp Waveshare: Ggf. BOOT-Taste beim Einstecken gedrückt halten. [web:36]

**Vorbereitung (wichtig)**  
- Einmal „Erase Flash“ ausführen, um alte Einstellungen zu entfernen und Bootloops zu vermeiden. [web:36]

**Flashen**

- Passende `.bin`-Datei wählen (Adresse 0x0). [web:13]  
- „Program“ klicken und Flashvorgang abwarten. [web:13]

**Starten**

- Nach Abschluss „Disconnect“ klicken. [web:13]  
- Im Bereich „Console“ erneut verbinden (115200 Baud) und Reset-Taste am Board drücken. [web:13]  

</details>

---

<details>
<summary><strong>🚦 Diagnose & LED Status</strong></summary>

Jedes Board (Full & Stealth) hat eine RGB-LED zur Statusanzeige. [web:22]

| Farbe  | Verhalten       | Bedeutung                | Maßnahme                                      |
|--------|-----------------|--------------------------|-----------------------------------------------|
| 🔵 Blau | Dauerleuchten   | Booting                  | System startet / WLAN-Verbindung wird aufgebaut. [web:22] |
| 🟢 Grün | Blinkt langsam  | Betrieb OK               | Kommunikation mit Batterie läuft, Daten werden gesendet. [web:22] |
| 🔴 Rot  | Dauerleuchten   | Kommunikationsfehler     | Keine Antwort vom BMS oder CAN-Kabel prüfen. [web:22] |
| 🔴 Rot  | Blinkt schnell  | Alarm (Safety)           | Überspannung über 56,5 V, Ladestrom auf 0 A. [web:22] |
| 🟣 Lila | Blinkt          | Temperaturschutz         | Batterie zu kalt (< 0 °C) oder zu heiß (> 50 °C). [web:22] |

</details>

---

<details>
<summary><strong>📖 Bedienung (Webinterface)</strong></summary>

Gilt nur für die Full-Version mit WLAN/Web-GUI. [web:22]

- Nach dem Start nach WLAN „Victron-Gateway-Setup“ suchen. [web:22]  
- Verbinden (Passwort leer oder „12345678“). [web:22]  
- Eigenes WLAN im Setup eintragen und speichern. [web:22]  
- Danach ist das Dashboard unter `http://victron-gateway.local` oder über die IP-Adresse erreichbar. [web:22]

</details>

---

<details>
<summary><strong>🔌 Verkabelung (Pinout Tabelle)</strong></summary>

| Signal  | Board        | Batterie (Topband)              | Victron (BMS-Can) |
|---------|-------------|----------------------------------|-------------------|
| RS485 A | A / D+      | Pin A (oft 1/2 oder 7/8)        | –                 |
| RS485 B | B / D−      | Pin B (oft 1/2 oder 7/8)        | –                 |
| CAN H   | H           | –                                | CAN-H             |
| CAN L   | L           | –                                | CAN-L             |
| GND     | GND         | GND (Schirmung)                 | GND (optional)    |

Wichtig: Den 120‑Ohm‑Widerstand (DIP-Schalter oder Jumper) am Board aktivieren. [web:22]

</details>

---

<details>
<summary><strong>❓ FAQ & Troubleshooting</strong></summary>

**Waveshare-Board startet ständig neu (Bootloop)?**  
- Wahrscheinlich zu große Firmware für den Flash. [web:36]  
- Standard/Safe‑Version (4 MB) verwenden und vorher „Erase Flash“ ausführen. [web:36]

**Kein Victron vorhanden – trotzdem nutzbar?**  
- Ja, mit der Full-Version. [web:22]  
- Im Webinterface „Enable Victron CAN“ deaktivieren, damit die Fehlermeldung verschwindet. [web:22]

**Werte im Diagramm nach Neustart weg?**  
- Absicht: 48‑Stunden‑Kurven liegen nur im RAM zum Flash-Schonen. [web:22]  
- kWh‑Zähler (Balkendiagramm) werden dauerhaft gespeichert. [web:22]

**Selbst kompilieren (Arduino IDE, Waveshare S3)**  

- Board: ESP32S3 Dev Module. [web:9]  
- Partition Scheme: Huge APP (3MB No OTA / 1MB SPIFFS). [web:7]  
- PSRAM: OPI PSRAM (bei 16MB‑Board) oder Disabled. [web:9]

</details>

---

## 👨‍💻 Development Team & Support

Dieses Projekt wurde mit viel Herzblut und Koffein entwickelt. [web:22]

- Lead Developer & Testing: **atomi23**. [web:22]  
- Co‑Pilot & Code‑Architect: **Gemini (AI)**. [web:22]

Unterstützung per PayPal: https://www.paypal.me/atomi23 [web:22]
