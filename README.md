# **🔋 Topband / EET BMS zu Victron VE.Can Gateway**

**Ein ESP32-basiertes Gateway, das Topband-BMS-Batterien (z.B. EET, Power Queen, etc.) über RS485 ausliest und als intelligentes BMS über CAN-Bus an Victron GX-Geräte (Cerbo, MultiPlus) sendet.**

![v98](https://github.com/user-attachments/assets/42b3407d-c421-48cc-9c61-e250e72559f5)


## **⚠️ Disclaimer & Warnung**

**BITTE VOR DER NUTZUNG LESEN:**

* **DIY Projekt:** Dies ist ein privates Hobby-Projekt und keine offizielle Software eines Herstellers.  
* **Auf eigene Gefahr:** Die Nutzung erfolgt auf eigenes Risiko. Der Entwickler übernimmt **keine Haftung** für Schäden an Batterien, Wechselrichtern, BMS oder anderer Hardware.  
* **Nur für private Nutzung:** Dieses Projekt ist für den privaten Einsatz gedacht. Keine kommerzielle Nutzung oder Vertrieb ohne Genehmigung.  
* **Experten-Funktionen:** Änderungen an Ladespannungen oder Stromgrenzen können Akkus zerstören, wenn sie falsch eingestellt werden.

## **🚀 Features (Firmware V59)**

### **🔌 Für Victron (CAN-Bus)**

* **Vollständige Integration:** Meldet sich als kompatible Batterie am Victron System an.  
* **DVCC Support:** Übermittelt Ladespannungslimit (CVL), Ladestromlimit (CCL) und Entladestromlimit (DCL).  
* **Smart Aggregation:** Fasst **mehrere BMS-Module** (bis zu 16\) zu einer großen Batteriebank zusammen (Summiert Strom & Kapazität, mittelt Spannung & SOC).  
* **Sicherheit:** Reduziert den Ladestrom automatisch, wenn der Akku voll wird (Balancing-Phase).

### **📊 Web-Interface (Dashboard)**

* **Live-Daten:** Zeigt Spannungen, Ströme, SOC, SOH und Temperaturen in Echtzeit.  
* **Zell-Überwachung:** Detaillierte Ansicht aller Zellspannungen mit **Drift-Anzeige** (Min/Max Differenz) und Farbcodierung.  
* **Zwei Ansichten (Umschaltbar):**  
  * **Karten-Ansicht:** Große, gut lesbare Kacheln für Tablets/Wandmontage.  
  * **Listen-Ansicht:** Kompakte Tabelle (aufklappbar), ideal für viele Batterien.  
* **Diagnose:** Integriertes Live-Log für RS485-Daten und CAN-Status direkt im Browser.

### **⚙️ Hardware & Konfiguration**

* **Plug & Play:** Einfache Einrichtung über WLAN-Hotspot (Victron-Gateway-Setup).  
* **mDNS Support:** Erreichbar unter http://victron-gateway.local (keine IP-Suche nötig).  
* **Konfigurierbar:** Anzahl der BMS, Batteriestrom und Zellenzahl (15s/16s) über Web-Oberfläche einstellbar.  
* **Experten-Modus:** Manuelle Anpassung der Ladeschlussspannung (CVL) möglich (auf eigene Gefahr).

## **🛠 Unterstützte Hardware**

Der Code ist optimiert für ESP32-Boards mit isoliertem RS485 und CAN Transceiver.

### **Empfohlene Boards:**

1. **LILYGO® T-CAN485** (ESP32 Classic)  
   * *Plug & Play, automatische Flusskontrolle.*  
2. **Waveshare ESP32-S3-RS485-CAN** (ESP32-S3)  
   * *Robustes Industriegehäuse möglich, benötigt spezielle Firmware (S3).*

### **Verkabelung:**

| Signal | Board | Batterie / Victron |
| :---- | :---- | :---- |
| **RS485 A** | A / D+ | A (Batterie) |
| **RS485 B** | B / D- | B (Batterie) |
| **CAN H** | H | CAN-H (Victron BMS-Can) |
| **CAN L** | L | CAN-L (Victron BMS-Can) |

**WICHTIG:** Der CAN-Bus muss am ESP32 und am Victron terminiert werden (**120 Ohm Widerstand** einschalten/stecken\!). Ohne Widerstand keine Kommunikation\!

## **⚡ Installation (Der einfache Weg)**

Sie müssen keine Programmierumgebung installieren. Nutzen Sie einfach den Web-Browser.

### **1\. Firmware herunterladen**

Laden Sie die aktuelle .bin Datei aus den [Releases](https://www.google.com/search?q=https://github.com/atomi23/Topband-BMS-to-CAN/releases) herunter.

* Für **LILYGO**: VictronGateway\_Lilygo\_V59.bin  
* Für **Waveshare S3**: VictronGateway\_WaveshareS3\_V59.bin

### **2\. Flashen über Web-Tool**

1. Verbinden Sie das ESP32-Board per USB mit dem PC.  
   * *Hinweis Waveshare S3:* Halten Sie beim Einstecken die "BOOT"-Taste gedrückt.  
2. Öffnen Sie [**web.esphome.io**](https://web.esphome.io/) oder den [**Adafruit Web Flasher**](https://adafruit.github.io/Adafruit_WebSerial_ESPTool/) (Chrome oder Edge Browser).  
3. Klicken Sie auf **Connect** und wählen Sie den COM-Port.  
4. Wählen Sie die heruntergeladene .bin Datei aus.  
5. Klicken Sie auf **Install/Program**.

### **3\. Einrichtung**

1. Nach dem Neustart öffnet der ESP32 einen WLAN-Hotspot: **Victron-Gateway-Setup**.  
2. Verbinden Sie sich mit dem Handy/Laptop (Passwort falls gefragt: 12345678 oder leer lassen).  
3. Ein Fenster sollte sich öffnen ("Anmelden"). Falls nicht, rufen Sie 192.168.4.1 auf.  
4. Klicken Sie auf "Configure WiFi" und geben Sie Ihre WLAN-Daten ein.  
5. Das Gateway startet neu und ist nun unter **http://victron-gateway.local** erreichbar.

## **💻 Installation (Für Entwickler / Source Code)**

Wer den Code selbst kompilieren oder anpassen möchte, benötigt die **Arduino IDE** oder **PlatformIO**.

### **Benötigte Bibliotheken**

Installieren Sie diese über den Bibliotheksverwalter:

* **WiFiManager** (by tzapu) \- für das Captive Portal.  
* **Adafruit NeoPixel** (by Adafruit) \- für die Status-LED.  
* **ESP32 Board Support Package** (by Espressif) \- Version 2.0.x oder neuer.
*   PubSubClient (by Nick O'Leary) - Für MQTT
  
### **Board-Einstellungen (Arduino IDE)**

* **LILYGO T-CAN485:**  
  * Board: ESP32 Dev Module  
  * Partition Scheme: Huge APP (3MB No OTA/1MB SPIFFS)  
* **Waveshare ESP32-S3:**  
  * Board: ESP32S3 Dev Module  
  * USB CDC On Boot: Enabled (Wichtig für Serial Monitor\!)  
  * Partition Scheme: Huge APP

### **Code-Anpassung**

Im Kopfbereich der .ino Datei können bei Bedarf Pins angepasst werden, falls andere Hardware genutzt wird:

// Beispiel für Waveshare S3  
\#define RS485\_TX\_PIN    17  
\#define RS485\_RX\_PIN    18  
\#define PIN\_RS485\_DIR   21 

## **❓ FAQ & Troubleshooting**

**Ich sehe "CAN FEHLER" im Dashboard?**

* Ist das Kabel zum Victron korrekt (H an H, L an L)?  
* Ist der **120 Ohm Abschlusswiderstand** am ESP32-Board aktiviert?  
* Ist am Victron BMS-Can Port der blaue Terminator gesteckt?

**Mein BMS wird nicht gefunden (Timeout)?**

* Stimmt die ID am BMS (DIP-Schalter)? Das Gateway sucht bei ID 0 (alle DIP aus) startend.  
* Sind A und B bei RS485 vertauscht?  
* Haben Sie die richtige Firmware für Ihr Board (Lilygo vs. Waveshare) gewählt?

**Die LED leuchtet rot?**

* Rot dauerhaft: CAN-Fehler (Senden fehlgeschlagen).  
* Grün blinkend: Alles OK, Daten werden an Victron gesendet.

## **🤝 Mitwirken**

Fehler gefunden oder Ideen für Verbesserungen? Erstellt gerne ein Issue oder einen Pull Request\!
