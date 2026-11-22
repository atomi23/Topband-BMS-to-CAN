# MQTT and Home Assistant

[Deutsch](mqtt.md) · **English**

[← Documentation overview](../README.en.md)

On this page: [MQTT](#mqtt) · [Home Assistant](#home-assistant)

## MQTT

Enable MQTT in `SMART_WIFI` under **Settings & Tools → Network** and enter the broker details. On current `main`, after a failed attempt, the gateway waits at least 10 seconds before trying the connection again.

## Home Assistant

MQTT Discovery does not require YAML configuration for discovered devices. Once MQTT is connected, click **Send HA Discovery** to publish the discovery data. On current `main`, reconnecting to MQTT does not send discovery automatically.

See also: [Web interface and settings](webinterface.en.md) · [CAN protocols](hardware.en.md#wiring).
