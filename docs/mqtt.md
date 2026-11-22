# MQTT und Home Assistant

**Deutsch** · [English](mqtt.en.md)

[← Dokumentationsübersicht](../README.md)

Auf dieser Seite: [MQTT](#mqtt) · [Home Assistant](#home-assistant)

## MQTT

Aktiviere MQTT in `SMART_WIFI` unter **Einstellungen & Tools → Netzwerk** und trage dort die Broker-Daten ein. Nach einem fehlgeschlagenen Verbindungsversuch wartet das Gateway im aktuellen `main` mindestens 10 Sekunden, bevor es die Verbindung erneut versucht.

## Home Assistant

MQTT Discovery benötigt keine YAML-Konfiguration für die erkannten Geräte. Klicke nach der MQTT-Verbindung auf **HA Discovery senden**, um die Discovery-Daten zu veröffentlichen. Beim MQTT-Reconnect wird Discovery im aktuellen `main` nicht automatisch gesendet.

Siehe auch: [Webinterface und Einstellungen](webinterface.md) · [CAN-Protokolle](hardware.md#verkabelung).
