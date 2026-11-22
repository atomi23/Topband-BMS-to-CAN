# Hardware und Verkabelung

**Deutsch** · [English](hardware.en.md)

[← Dokumentationsübersicht](../README.md)

Auf dieser Seite: [Bezugsquellen](#bezugsquellen) · [Zubehör](#zubehör) · [Verkabelung](#verkabelung)

## Bezugsquellen

- **Gateway-Boards:** [LilyGo T-CAN485](https://www.amazon.de/s?k=LilyGo+T-CAN485&tag=atomi23-21)\*, [Waveshare ESP32-S3 RS485/CAN](https://www.amazon.de/s?k=Waveshare+ESP32-S3+RS485+CAN&tag=atomi23-21)\* ([Mini](https://www.amazon.de/s?k=Waveshare+ESP32-S3+RS485+CAN+Mini&tag=atomi23-21)\*, [Power/Pro](https://www.amazon.de/s?k=Waveshare+ESP32-S3+RS485+CAN+Power+Pro&tag=atomi23-21)\*), [M5Stack Basic/Core (Classic)](https://www.amazon.de/s?k=M5Stack+Basic+Core&tag=atomi23-21)\* mit [M5Stack Module COMMU](https://www.amazon.de/s?k=M5Stack+Module+COMMU&tag=atomi23-21)\* (enthält den MCP2515).
- **Batterie-Beispiele:** [EET](https://www.amazon.de/s?k=EET+Topband+LiFePO4&tag=atomi23-21)\*, [Power Queen](https://www.amazon.de/s?k=Power+Queen+LiFePO4&tag=atomi23-21)\*, [AmpereTime](https://www.amazon.de/s?k=AmpereTime+LiFePO4&tag=atomi23-21)\*, [Redodo](https://www.amazon.de/s?k=Redodo+LiFePO4&tag=atomi23-21)\*. Prüfe beim konkreten Modell, ob es ein Topband-BMS mit RS485 hat.
- **Wechselrichter und GX-Geräte:** [Victron Cerbo GX](https://www.amazon.de/s?k=Victron+Cerbo+GX&tag=atomi23-21)\*, [Victron MultiPlus](https://www.amazon.de/s?k=Victron+MultiPlus&tag=atomi23-21)\*, [Deye](https://www.amazon.de/s?k=Deye+Hybrid+Wechselrichter&tag=atomi23-21)\*, [GoodWe](https://www.amazon.de/s?k=GoodWe+Hybrid+Wechselrichter&tag=atomi23-21)\*, [Growatt](https://www.amazon.de/s?k=Growatt+Hybrid+Wechselrichter&tag=atomi23-21)\*, [SMA Sunny Island](https://www.amazon.de/s?k=SMA+Sunny+Island&tag=atomi23-21)\*. Prüfe, welches BMS-Protokoll dein Wechselrichter unterstützt.

\*Als Amazon-Partner verdiene ich an qualifizierten Verkäufen. Mit * markierte Links sind Werbe-/Affiliate-Links.

### Zubehör

- [USB-A-auf-USB-C-Datenkabel](https://www.amazon.de/s?k=USB+A+auf+USB+C+Datenkabel&tag=atomi23-21)\*: Zum Flashen der Boards mit USB-C; reine Ladekabel übertragen keine Daten.
- [5-V-USB-A-Netzteil](https://www.amazon.de/s?k=USB+A+Netzteil+5V+2A&tag=atomi23-21)\*: Mit dem USB-Kabel zur Versorgung des [M5Stack Basic](https://docs.m5stack.com/en/core/Basic) oder des [Waveshare ESP32-S3-RS485-CAN](https://docs.waveshare.com/ESP32-S3-RS485-CAN). USB-A ist auch für ältere Basic-Revisionen geeignet, die kein USB-C-auf-USB-C unterstützen.
- [Stabilisiertes 9-V-DC-Netzteil](https://www.amazon.de/s?k=9V+DC+Netzteil+stabilisiert+Schraubklemmen&tag=atomi23-21)\*: Für den DC-Klemmeneingang, sofern die Boardrevision dafür freigegeben ist. 9 V liegen im Bereich des [LilyGo T-CAN485](https://lilygo.cc/products/t-can485) (7–12 V laut Produktseite) und des [Waveshare ESP32-S3-RS485-CAN](https://docs.waveshare.com/ESP32-S3-RS485-CAN) (7–36 V). Bei anderen Varianten Spannung und Polarität am konkreten Board prüfen; nicht an den 5-V-USB-Eingang anschließen.
- [LAN-Kabel](https://www.amazon.de/s?k=LAN+Kabel+RJ45&tag=atomi23-21)\*: Als Leitung für die unten beschriebenen RS485-/CAN-Verbindungen. Die RJ45-Stecker hier nicht mit einem Ethernet- oder PoE-Port verbinden.
- [120-Ω-CAN-Abschlusswiderstand](https://www.amazon.de/s?k=CAN+Bus+Abschlusswiderstand+120+Ohm&tag=atomi23-21)\*: Für ein CAN-Busende, wenn dort kein eingebauter Abschluss aktiv ist. Stecker und Pinbelegung prüfen.
- [Aderendhülsen](https://www.amazon.de/s?k=Aderendhuelsen+feindraehtige+Leitung+Schraubklemmen&tag=atomi23-21)\*: Für feindrähtige Anschlussleitungen an geeigneten RS485-/CAN-Schraubklemmen; Hülse passend zum Leiterquerschnitt und zur Klemme wählen.
- [DC-Sicherung](https://www.amazon.de/s?k=DC+Sicherung+Batterie&tag=atomi23-21)\*: Absicherung der DC-Zuleitung; Nennspannung, Abschaltvermögen und Sicherungsstrom passend zum Aufbau wählen.
- [microSD-Karte](https://www.amazon.de/s?k=microSD+Karte+32GB+FAT32&tag=atomi23-21)\*: Für das SD-Logging des LilyGo-Profils (FAT32, bis 32 GB); im Waveshare- und M5Stack-Profil ist SD-Logging deaktiviert.

**LED-Streifen und PoE:** Die Firmware adressiert nur einen NeoPixel (LilyGo: integrierter WS2812 an GPIO4; Waveshare-Profil: GPIO38, kein dokumentierter Streifenanschluss; M5Stack: kein NeoPixel). Ein externer Streifen ist daher kein direkt unterstütztes Zubehör. Die dokumentierten Gateway-Aufbauten nutzen WLAN oder den kabelgebundenen RS485-/CAN-Betrieb, nicht Ethernet/PoE.

## Verkabelung

- **Sicherheit:** DC-Sicherungen sind Pflicht! Falsche Parameter können Akkus zerstören.

- **Multi-Protokoll:** Victron (Standard), Pylontech (Deye/Goodwe/Growatt), SMA Sunny Island.

### Übersichtsschema

```mermaid
graph LR
    subgraph BATTERIE ["🔋 EET / Topband"]
        B_Port["RS485 Port"]
    end

    subgraph GATEWAY ["📟 ESP32 Gateway"]
        RS485_A["Klemme A (A+)"]
        RS485_B["Klemme B (B-)"]
        CAN_H["CAN H"]
        CAN_L["CAN L"]
    end

    subgraph INVERTER ["🔵 Victron / Deye"]
        V_Port["BMS-Can Port"]
    end

    %% Verkabelung
    B_Port -- "Pin 1 (Orange/Weiß)" --> RS485_A
    B_Port -- "Pin 2 (Orange)" --> RS485_B
    CAN_H -- "Pin 7 (Braun/Weiß)" --> V_Port
    CAN_L -- "Pin 8 (Braun)" --> V_Port
```

### 1. Batterie zu Gateway (RS485)
- Nimm ein LAN-Kabel, schneide einen Stecker ab. Nutze das ORANGE Paar:
- Pin 1 (Orange/Weiß) an ESP32 RS485 A (A+)
- Pin 2 (Orange) an ESP32 RS485 B (B-)
- Die LED bleibt Blau pulsierend? Tausche A und B!

### 2. Gateway zu Wechselrichter (CAN)
- Verbinde den CAN-Port des ESP32 mit dem BMS-Can des Wechselrichters.
- ESP32 CAN H an Victron/Deye Pin 7 (Braun/Weiß)
- ESP32 CAN L an Victron/Deye Pin 8 (Braun)
- GND (optional): Pin 3
- Hinweis: Vergiss nicht den Terminator-Widerstand am zweiten CAN-Port des Victron/Deye und aktiviere den DIP-Schalter (120R) am Gateway!

Zurück zu [Installation und Flashen](installation.md).
