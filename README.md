# Topband / EET BMS zu Victron VE.Can Gateway

Dieses Projekt realisiert ein **RS485-zu-CAN-Bus Gateway** auf Basis eines **ESP32**. Es ermöglicht die Kommunikation zwischen **Topband BMS** basierten Batteriespeichern und **Victron GX-Geräten** (Cerbo GX, MultiPlus, etc.).

Das Projekt wurde speziell für den **EET Batteriespeicher ET0144** (1,44 kWh, 30 Ah, 48V, 19" Rack) entwickelt, funktioniert aber mit den meisten Batterien, die das Topband-Protokoll nutzen.

> **💡 Hinweis zu Node-RED:**
> Dieses Projekt entstand ursprünglich aus einem **Node-RED Flow**. Wer eine reine Software-Lösung bevorzugt (z.B. auf Venus OS Large), findet den Flow weiterhin im Ordner `/nodered` oder als `flows-topband.json`.

## 🚀 Features der Firmware (V2)

* **Keine Programmierung nötig:** Einfaches Flashen per Web-Browser.
* **Multi-Board Support:** Unterstützt **LILYGO T-CAN485** und **Waveshare ESP32-S3-RS485-CAN**.
* **Plug & Play:** Integrierter **WiFi-Manager** (Hotspot) zur einfachen WLAN-Einrichtung.
* **Web-Interface:** Konfiguration der **Anzahl der Akkus** und **Ampere-Limits** bequem im Browser.
* **Dynamische Logik:** * Automatische Berechnung des Gesamt-SOC (basierend auf Kapazität).
  * Automatische Skalierung des Stroms (z.B. 2 Akkus à 20A = 40A System-Limit).
  * Ausfallsicherheit: Fällt ein Akku aus, wird das Limit sofort reduziert.
* **Pylontech-Emulation:** Meldet sich als Pylontech-Batterie am Victron System an.

---

## 🛠 Unterstützte Hardware

Sie benötigen **eines** der folgenden Boards:

### Option A: LILYGO® T-CAN485
* Der Klassiker (ESP32 Standard).
* **Firmware-Datei:** `VictronGateway_LILYGO_v2.ino.merged.bin`

### Option B: Waveshare ESP32-S3-RS485-CAN
* Neuere Version (ESP32-S3), industrielles Gehäuse möglich.
* **Firmware-Datei:** `VictronGateway_v2.ino.merged.bin`

---

## ⚡ Installationsanleitung (Der einfache Weg)

Sie müssen keine Software installieren. Das Flashen erfolgt direkt über den Browser.

### Schritt 1: Firmware herunterladen
Laden Sie oben aus der Dateiliste die passende `.bin` Datei für Ihr Board herunter:
* Für **LILYGO**: `VictronGateway_LILYGO_v2.ino.merged.bin`
* Für **Waveshare**: `VictronGateway_v2.ino.merged.bin`

*(Achten Sie darauf, die Datei mit "merged" im Namen zu nehmen, diese enthält alles Notwendige).*

### Schritt 2: Board flashen
1.  Verbinden Sie das Board per USB mit Ihrem PC.
    * *Hinweis Waveshare:* Halten Sie beim Einstecken ggf. die "BOOT"-Taste gedrückt.
2.  Öffnen Sie das **[Espressif Web Flashing Tool](https://espressif.github.io/esptool-js/)** (Chrome oder Edge Browser).
3.  Klicken Sie auf **Connect** und wählen Sie den COM-Port Ihres Boards.
4.  Tragen Sie folgende Werte ein:
    * **Address:** `0x0`
    * **File:** Wählen Sie Ihre heruntergeladene `.bin` Datei aus.
5.  Klicken Sie auf **Program**.

### Schritt 3: WLAN Einrichtung (Hotspot)
Nach dem Flashen startet das Board neu. Da es Ihre WLAN-Daten noch nicht kennt, eröffnet es einen Hotspot.

1.  Suchen Sie am Handy/Laptop nach dem WLAN: **`LILYGO-Gateway-Setup`** (bzw. `Victron-Gateway-Setup`).
2.  Verbinden Sie sich. Ein Anmelde-Fenster sollte sich öffnen (Captive Portal).
    * *Falls nicht:* Rufen Sie im Browser `192.168.4.1` auf.
3.  Klicken Sie auf **Configure WiFi**.
4.  Wählen Sie Ihr Heim-WLAN aus und geben Sie das Passwort ein.
5.  Das Board speichert und startet neu.

### Schritt 4: Gateway Konfiguration
Suchen Sie im Router die neue IP-Adresse des Gateways und öffnen Sie diese im Browser.

1.  **Anzahl BMS Module:** Stellen Sie ein, wie viele Akkus Sie verbunden haben (1-16).
    * *Wichtig:* Die Akkus müssen per DIP-Schalter adressiert sein (0, 1, 2...).
2.  **Strom pro BMS:** Geben Sie den Nennstrom pro Akku an (z.B. `30` für 30A).
3.  Klicken Sie auf **Speichern**.

---

## 🔌 Verkabelung & Victron Setup

> **⚠️ ACHTUNG:** Achten Sie unbedingt auf die korrekte Polarität (+/- bzw. A/B)!

### 1. RS485 (Verbindung zum Akku)
Verbinden Sie den RS485-Port des Boards mit dem ersten Akku.
* **A** an **A** (manchmal auch D+ oder 485+)
* **B** an **B** (manchmal auch D- oder 485-)
* *GND ist optional, aber empfohlen.*

### 2. CAN-Bus (Verbindung zum Victron GX)
Verbinden Sie den CAN-Port des Boards mit dem **BMS-Can** Anschluss des Victron GX (z.B. Cerbo GX).
* **H** an **CAN-H**
* **L** an **CAN-L**

### 3. Terminierung (WICHTIG!)
Der CAN-Bus muss an beiden Enden terminiert (abgeschlossen) sein:
1.  **Victron-Seite:** Stecken Sie den blauen V-Terminator-Stecker in den zweiten BMS-Can Port.
2.  **ESP32-Seite:** Aktivieren Sie den 120Ω Widerstand auf dem Board (meist ein kleiner Schalter oder Jumper "120R" / "TERM" auf der Platine).

### 4. Victron GX Einstellungen
1.  Öffnen Sie die **Remote Console**.
2.  Gehen Sie zu: `Menü` -> `Einstellungen` -> `Dienste`.
3.  Wählen Sie den genutzten CAN-Port (z.B. *VE.Can 1*).
4.  Stellen Sie das Profil auf: **"CAN-Bus BMS (500 kbit/s)"**.
5.  Starten Sie das GX-Gerät neu.

---

## 💻 Für Entwickler (Source Code)

Wenn Sie den Code anpassen möchten:
Der Quellcode liegt als `.ino` Datei vor (`VictronGateway.ino` / `_v2.ino`).
* **IDE:** Arduino IDE oder PlatformIO.
* **Benötigte Bibliotheken:**
  * `WiFiManager` (von tzapu)
  * `Adafruit NeoPixel`
* **Board-Einstellungen (Arduino IDE):**
  * Waveshare: `ESP32S3 Dev Module`, USB CDC On Boot: Enabled.
  * LILYGO: `ESP32 Dev Module`.

---

## 🤝 Unterstützung

Dies ist ein Open-Source-Projekt.
* **Fehler gefunden?** Erstelle gerne ein "Issue" hier auf GitHub.
* **Erfolg gehabt?** Wir freuen uns über Feedback!
