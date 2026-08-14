# ESP-P4d

Eine **Fröling-Heizung (Lambdatronic S3200)** mit einem **ESP32-S2 (Wemos S2 Mini)**
auslesen und als eigenständiges Gerät (Weboberfläche + später MQTT) darstellen —
**ohne** Raspberry Pi, Datenbank oder Home Assistant.

Portierung des seriellen COM1-Protokolls aus **[horchi/linux-p4d](https://github.com/horchi/linux-p4d)**.

> **Status:** Machbarkeitsanalyse abgeschlossen, **Milestone 1** (COM1-Protokoll-Modul +
> Proof-of-Life) gebaut. Netz/Web/OTA/Historie(CSV)/MQTT folgen.
> Siehe [`machbarkeitsanalyse.md`](machbarkeitsanalyse.md) und [`protokoll.md`](protokoll.md).

## Zielbild

- **Nur lesen** (Monitoring) über **COM1** (nativer Service-Port, 57600 8N1).
- Eigenständiges Gerät: responsive **Weboberfläche**, **Installer-AP** fürs Erstsetup,
  **1 Woche Historie als CSV** downloadbar, **Logs** downloadbar, **OTA**.
- **MQTT** als späteres, optionales Publish-Feature.
- Kein Flash-Verschleiß: Werte live in RAM/PSRAM, CSV gebündelt + Wear-Leveling.

## Hardware

- **Wemos S2 Mini** (ESP32-S2, 4 MB Flash, 2 MB PSRAM)
- **MAX3232** (RS232↔TTL, mit **3,3 V** versorgt)
- Verbindung zu **COM1** des Kessels (extern an der Gehäuserückseite):
  - Kessel-TX → MAX3232 R1IN, R1OUT → **ESP GPIO18** (RX)
  - **ESP GPIO17** (TX) → MAX3232 T1IN, T1OUT → Kessel-RX
  - GND ↔ GND
  - Antwortet der Kessel nicht: **TX/RX tauschen** (Null-Modem), GND prüfen,
    und sicherstellen, dass **kein anderes p4d** auf COM1 hängt (nur ein Master).

Die UART-Pins sind in `firmware/src/main.cpp` als `P4_RX_PIN`/`P4_TX_PIN` einstellbar.

## Bauen & Flashen (Milestone 1)

```
cd firmware
pio run                 # bauen
pio run -t upload       # flashen (Wemos S2 Mini per USB)
pio device monitor      # Ausgabe (USB-CDC, 115200)
```

Der Proof-of-Life fragt zyklisch `cmdCheck` (0x22) und `cmdGetVersion` (0x41) ab und meldet
über den seriellen Monitor, ob die Verbindung steht und welche Kessel-Software-Version läuft.

## Lizenz

**GPLv3** (siehe [`LICENSE`](LICENSE)). Dieses Projekt leitet das serielle Protokoll aus dem
GPL-lizenzierten [linux-p4d](https://github.com/horchi/linux-p4d) von Jörg Wendel ab und steht
daher ebenfalls unter der GPL.
