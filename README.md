# 🔋 Topband / EET BMS zu Victron VE.Can Gateway (V117)

Ein ESP32-basiertes Gateway, das Topband-BMS-Batterien (z.B. EET, Power Queen, AmpereTime, etc.) über RS485 ausliest und als intelligentes BMS über CAN-Bus an Victron GX-Geräte (Cerbo, MultiPlus) sendet.

> **Aktuelle Version:** V117 (Stable)

## 📸 Dashboard Preview

Hier ein Einblick in die Web-Oberfläche (Power-Graph & Live-Werte):

<img width="100%" alt="Topband Gateway Dashboard V117" src="https://github.com/user-attachments/assets/3336937b-aefc-4ec4-8f42-b38c23b86068" />

> *Hinweis: Das aktuelle Design bietet Dark-Mode, Glas-Effekte, 7 Themes und Drag & Drop.*

---

## ⚠️ Disclaimer & Warnung

**PRIVATE USE ONLY. NO COMMERCIAL USE.**
* **DIY Projekt:** Dies ist ein privates Open-Source-Projekt und steht in keiner geschäftlichen Verbindung zu Topband Battery Co., Ltd. oder Victron Energy.
* **Auf eigene Gefahr:** Die Nutzung erfolgt auf eigenes Risiko. Der Entwickler übernimmt **keine Haftung** für Schäden an Batterien, Wechselrichtern, BMS oder anderer Hardware.
* **Sicherheit:** Stellen Sie sicher, dass DC-Sicherungen verbaut sind. Änderungen an Parametern können Akkus zerstören.

---

## 📦 Downloads (Wähle deine Version!)

Da Waveshare verschiedene Hardware-Versionen verkauft, bieten wir ab V117 angepasste Firmware-Dateien an.

### 🌐 FULL VERSION (Mit Webinterface & WLAN)
*Die komfortable Lösung mit Dashboard, Diagrammen und MQTT.*

* **`v117_waveshare_4mb_NoPram.bin` (Standard / Safe)**
    * ✅ **Empfohlen!** Läuft auf **ALLEN** Waveshare S3 Boards (4MB/8MB/16MB) stabil. Sicherste Wahl gegen Bootloops.
* **`v117_waveshare_16mb_8Pram.bin` (Ultra / High-End)**
    * *Nur für Experten:* Benötigt zwingend Board mit **16MB Flash & 8MB OPI PSRAM**.
* **`v117_lilygo_t_can485.bin` (Classic)**
    * Für das LILYGO® T-CAN485 Board.

### 🥷 STEALTH VERSION (Ohne WLAN / Nur Kabel)
*Sehr robust, startet in <1 Sekunde, keine Konfiguration nötig.*

* **`STEALTH_V117.waveshare.bin`** (Universal S3)
    * Läuft auf allen Waveshare S3 Boards.
* **`STEALTH_V117.lilygo_t_can485.bin`** (Classic)
    * Für das schwarze T-CAN485 Board.

---

<details>
<summary><strong>🚀 Features & Highlights (Klick zum Ausklappen)</strong></summary>

### 🔌 Für Victron (CAN-Bus)
* **Vollständige Integration:** Meldet sich als kompatible Batterie am Victron System an.
* **Smart Aggregation:** Fasst bis zu 16 Batterien zu einer großen Bank zusammen (Summiert Strom & Kapazität).
* **Monitoring Mode (Full):** Der CAN-Versand kann deaktiviert werden, um das Gateway als reinen Monitor (ohne Eingriff ins System) zu nutzen.

### 🎨 Web-Interface (Nur "Full")
Die "Full" Version bietet eine Design-Engine mit **7 verschiedenen Skins**:
* 💎 **Modern Glass:** Transparenter Look mit Status-Glow.
* 🔋 **Battery Live:** Hintergrundfarbe ändert sich dynamisch mit dem SOC.
* 👾 **Cyberpunk HUD:** Neon-Look für Technik-Fans.
* 🏗️ **Custom Dashboard:** Karten können per **Drag & Drop** verschoben werden.
* **Plus:** Retro Dark, Simple, Soft UI.

### 🛡️ Sicherheit & Stabilität
* **Hard-Coded Safety:** Ladestrom-Cutoff (0A) bei V > 56.5V oder Temp < 0°C / > 50°C.
* **Watchdog Protection:** Verhindert Abstürze, wenn Batterien nicht antworten.
* **Flash-Schutz:** Diagrammdaten liegen im RAM, Energiewerte werden nur 1x täglich gespeichert.

</details>

<details>
<summary><strong>🔌 Verkabelung (Pinout & Anleitung) - WICHTIG!</strong></summary>

### Übersichtsschema

```mermaid
graph LR
    subgraph BATTERIE ["🔋 EET / Topband Batterie"]
        direction TB
        B_Port["Port: RS485 / Link Port<br>(RJ45 Buchse)"]
    end

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
### Schritt 1: Kabel zur Batterie (RS485)
Nimm ein normales LAN-Kabel und schneide einen Stecker ab. Die offenen Adern kommen an das **grüne Schraub-Terminal** am ESP32.

| Batterie Pin (RJ45) | Kabelfarbe (Standard T568B) | ESP32 Klemme |
| :--- | :--- | :--- |
| **Pin 1** | 🟠⚪ **Orange / Weiß** | an **A** (oder A+) |
| **Pin 2** | 🟠 **Orange** | an **B** (oder B-) |

> **Tipp:** Falls die LED am Gateway **ROT** bleibt, tausche einfach A und B am Gateway (Orange und Orange/Weiß vertauschen). Da geht nichts kaputt!

### Schritt 2: Kabel zum Victron (CAN-Bus)
Verbinde das Gateway mit dem **BMS-Can** Port des Victron (Nicht VE.Can!).

| Victron Pin (RJ45) | Kabelfarbe (Standard T568B) | ESP32 Klemme |
| :--- | :--- | :--- |
| **Pin 7** | 🟤⚪ **Braun / Weiß** | an **H** (High) |
| **Pin 8** | 🟤 **Braun** | an **L** (Low) |

### Schritt 3: Einstellungen (DIP Switches)
1.  **Am ESP32 Board:** Aktiviere den **120 Ohm Widerstand** (Schalter auf ON oder Jumper setzen).
2.  **An der Batterie:** Stelle die DIP-Schalter auf **Adresse 1** (Meistens: Schalter 1=ON, Rest=OFF).
3.  **Am Victron:** Stecke den blauen Terminator-Stecker in den zweiten BMS-Can Port.

</details>

<details>
<summary><strong>⚡ Installation & Flashen (Web-Tool)</strong></summary>

Wir empfehlen das **Espressif Web Tool** (keine Software-Installation nötig).

🔗 **[espressif.github.io/esptool-js/](https://espressif.github.io/esptool-js/)**

1.  **Browser:** Bitte **Google Chrome** oder **Edge** nutzen.
2.  **Verbinden:** Board per USB anschließen, oben auf `Connect` klicken und Port wählen.
    * *Tipp Waveshare:* Ggf. die "BOOT"-Taste beim Einstecken gedrückt halten.
3.  **Vorbereitung (WICHTIG):**
    * Klicken Sie einmal auf **Erase Flash**, um alte Einstellungen zu löschen. Dies verhindert Bootloops!
4.  **Flashen:**
    * Wählen Sie unten die passende `.bin` Datei aus (Adresse `0x0`).
    * Klicken Sie auf **Program**.
5.  **Starten:**
    * Nach Abschluss auf `Disconnect` klicken.
    * Im Bereich "Console" erneut verbinden (115200 Baud).
    * **Reset-Taste** am Board (oder auf der Webseite) drücken -> Startlog prüfen.

</details>

<details>
<summary><strong>🚦 Diagnose & LED Status</strong></summary>

Jedes Board (Full & Stealth) verfügt über eine RGB-LED zur Statusanzeige.

| Farbe | Verhalten | Bedeutung | Maßnahme |
| :--- | :--- | :--- | :--- |
| 🔵 **BLAU** | Dauerleuchten | **Booting** | System startet / WLAN Verbindung läuft. |
| 🟢 **GRÜN** | Blinkt langsam | **Betrieb OK** | Kommunikation mit Batterie OK, Daten werden gesendet. |
| 🔴 **ROT** | Dauerleuchten | **Kommunikations-Fehler** | Keine Antwort vom BMS (Kabel A/B tauschen!) oder CAN-Kabel ab. |
| 🔴 **ROT** | Blinkt schnell | **ALARM (Safety)** | Überspannung (>56.5V)! Ladestrom wird auf 0A gesetzt. |
| 🟣 **LILA** | Blinkt | **Temperatur-Schutz** | Zu kalt (<0°C) oder zu heiß (>50°C). |

</details>

<details>
<summary><strong>📖 Bedienung (Webinterface)</strong></summary>

*(Gilt nur für die Full Version)*

1.  Suchen Sie nach dem WLAN **"Victron-Gateway-Setup"**.
2.  Verbinden Sie sich (Passwort leer lassen oder `12345678`).
3.  Geben Sie Ihre WLAN-Daten ein.
4.  Nach Neustart ist das Dashboard unter `http://victron-gateway.local` (oder der IP-Adresse) erreichbar.

</details>

<details>
<summary><strong>❓ FAQ & Troubleshooting</strong></summary>

**Mein Waveshare Board startet ständig neu (Bootloop)?**
Sie haben vermutlich eine Version geflasht, die für den Speicherchip zu groß ist.
* Nutzen Sie die **"Standard / Safe" (4MB)** Version.
* Führen Sie vor dem Flashen unbedingt ein **"Erase Flash"** durch.

**Ich habe keinen Victron, kann ich das Gateway trotzdem nutzen?**
Ja! (Nur Full Version). Gehen Sie in die Einstellungen und deaktivieren Sie den Haken bei **"Enable Victron CAN"**. Die Fehlermeldung im Dashboard verschwindet dann.

**Werte im Diagramm sind nach Neustart weg?**
Das ist Absicht. Um den Speicherchip zu schonen, liegen die hochauflösenden 48h-Kurven nur im RAM. Die kWh-Zähler (Balkendiagramm) werden jedoch dauerhaft gespeichert.

**Selbst Kompilieren (Arduino IDE)?**
Falls Sie den Code selbst anpassen wollen, nutzen Sie bitte folgende Einstellungen für Waveshare S3:
* **Board:** `ESP32S3 Dev Module`
* **Partition Scheme:** `Huge APP (3MB No OTA/1MB SPIFFS)`
* **PSRAM:** `OPI PSRAM` (nur bei 16MB Board) oder `Disabled`.

</details>

---

### 👨‍💻 Development Team & Support

Dieses Projekt wurde mit viel ❤️ und ☕ entwickelt.

* **Lead Developer & Testing:** [atomi23](https://github.com/atomi23)
  <br>
  <a href="https://www.paypal.me/atomi23">
    <img src="https://img.shields.io/badge/Donate-PayPal-blue.svg?logo=paypal&style=for-the-badge" alt="Donate via PayPal" />
  </a>

* **Co-Pilot & Code-Architect:** Gemini (AI)
