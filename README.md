Kurzanleitung: Von 0 bis zum fertigen Gateway
Voraussetzung: Sie benötigen ESPHome. Am einfachsten geht es als Add-on in Home Assistant. Alternativ können Sie ESPHome auch auf Ihrem PC installieren.

Schritt 1: Dateien erstellen
Erstellen Sie einen neuen Ordner auf Ihrem Computer, z. B. victron-gateway.

Kopieren Sie die beiden unten stehenden Code-Blöcke.

Speichern Sie den ersten Block als topband-gateway.yaml in diesem Ordner.

Speichern Sie den zweiten Block als victron_topband_gateway.h im selben Ordner.

Schritt 2: OTA-Passwort festlegen (Optional, aber empfohlen)
Um Updates über WLAN zu sichern, erstellen Sie (falls nicht schon vorhanden) eine Datei namens secrets.yaml im Hauptverzeichnis Ihres ESPHome-Setups und fügen Sie eine Zeile hinzu: ota_password: "IhrWahlpasswort"

Wenn Sie kein OTA-Passwort möchten, löschen Sie einfach die Zeile password: !secret ota_password aus der YAML-Datei.

Schritt 3: Flashen
Verbinden Sie Ihr LILYGO T-CAN485 Board per USB mit dem Computer, auf dem ESPHome läuft.

Öffnen Sie ESPHome.

Erstellen Sie ein "Neues Gerät" (New Device).

Überspringen Sie den Assistenten und klicken Sie auf "Install".

Wählen Sie "Manually" und suchen Sie Ihre topband-gateway.yaml Datei.

ESPHome wird nun den Code kompilieren und auf das Board flashen.

Schritt 4: WLAN einrichten (Captive Portal)
Nach dem Flashen startet das Board neu und findet kein WLAN.

Es erstellt einen eigenen WLAN-Access-Point mit dem Namen "BMS-Gateway-Setup".

Verbinden Sie sich mit Ihrem Handy oder Laptop mit diesem WLAN. (Das Passwort ist: setup-bms-123)

Ihr Gerät sollte automatisch eine Webseite öffnen (das Captive Portal).

Wählen Sie dort Ihr Heim-WLAN aus und geben Sie Ihr WLAN-Passwort ein.

Das Board speichert die Daten und startet neu. Ab jetzt verbindet es sich automatisch mit Ihrem Heim-WLAN.

Schritt 5: Physische Installation
Verbinden Sie RS485 (A/B oder +/-) des Boards mit den entsprechenden Anschlüssen Ihres ersten Topband BMS. Stellen Sie die DIP-Schalter am BMS auf Adresse 0, am nächsten auf 1 usw.

Verbinden Sie CAN (CAN-H und CAN-L) des Boards mit dem CAN-Bus-Anschluss Ihres Victron GX-Geräts (z.B. Cerbo GX). Achten Sie unbedingt auf die Polarität!

WICHTIG (Terminierung): Ein CAN-Bus muss an beiden Enden terminiert sein.

Victron-Ende: Verwenden Sie den blauen 120-Ohm-Terminator-Stecker, der bei Ihrem GX-Gerät dabei war.

ESP32-Ende: Ihr LILYGO-Board hat einen Jumper (oft markiert mit "120R" oder "TERM") oder einen kleinen Schalter. Aktivieren Sie diesen 120-Ohm-Widerstand auf dem LILYGO-Board.

Schritt 6: Victron GX einstellen
Öffnen Sie die Remote Console Ihres GX-Geräts.

Gehen Sie zu: Menü -> Einstellungen -> Dienste.

Wählen Sie den CAN-Bus-Port, den Sie verwenden (z.B. VE.Can 1).

Stellen Sie das Profil für diesen Port auf "CAN-Bus BMS (500 kbit/s)".

Starten Sie das GX-Gerät neu (Menü -> Einstellungen -> Allgemein -> Neustart).

Schritt 7: Gateway konfigurieren
Finden Sie die IP-Adresse Ihres ESP32-Boards (im Router oder im ESPHome-Log).

Öffnen Sie diese IP-Adresse in einem Webbrowser.

Sie sehen nun die Weboberfläche mit allen Statuswerten und den Einstellungen.

Stellen Sie den Regler "Anzahl der BMS-Module" auf die korrekte Anzahl ein (z. B. auf 2, wenn Sie zwei Akkus haben).

Passen Sie bei Bedarf die Basis-Ströme an.

Das Gateway ist nun betriebsbereit. Die RGB-LED sollte grün pulsieren.
