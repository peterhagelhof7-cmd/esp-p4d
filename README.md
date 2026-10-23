# ESP-P4d

**Fröling-Heizung (Lambdatronic S3200) über COM1 auslesen – mit einem ESP32-S2 statt Raspberry Pi.**

Ein Wemos S2 Mini mit MAX3232 hängt an der Service-Schnittstelle COM1 des Kessels, liest Zustand,
Störungen und alle Messwerte und zeigt sie im WLAN an – eigenständig, ohne Datenbank, ohne
Home Assistant. Über eine kleine REST-API können andere Systeme die Werte abholen.

Das COM1-Protokoll ist aus **[linux-p4d](https://github.com/horchi/linux-p4d)** von Jörg Wendel
portiert – dem bewährten p4d-Daemon für Raspberry Pi & Co. Danke!

**[Web-Flasher](https://peterhagelhof7-cmd.github.io/esp-p4d/)** ·
**[Admin-Guide](docs/admin-guide.md)** ·
**[Releases](https://github.com/peterhagelhof7-cmd/esp-p4d/releases)**

## Funktionen

- **Kessel lesen** (nur lesen, nichts schreiben): Betriebsart, Zustand, anstehende Störung,
  Fehlerspeicher, alle Werte aus dem Werte-Katalog des Kessels (typisch 150–250)
- **Weboberfläche** (auch fürs Handy): Status, alle Werte mit Auswahl, COM1-Konsole mit
  Hex-Mitschnitt, Einstellungen
- **WLAN-Einrichtung** über ein eigenes Einrichtungs-WLAN, feste IP mit Belegt-Prüfung, mDNS
  (`esp-p4d.local`), NTP
- **REST-API** mit Schlüssel: Zusammenfassung, alle Werte, Status inkl. Vitalwerten des Boards
  (Signal, Chip-Temperatur, Neustart-Grund, Speicher, COM1-Fehlerquote)
- **OTA-Update** mit Herkunfts- und Versionsprüfung, **Einstellungs-Backup**
- Passt zu **[home-central](https://github.com/peterhagelhof7-cmd/home-central)**: Verlauf,
  Diagramme, Telegram-Meldungen („Feuer aus“, Störung, Gerät offline)

## Hardware

| Teil | |
|---|---|
| Wemos S2 Mini (ESP32-S2) | 4 MB Flash, 2 MB PSRAM |
| MAX3232-Modul | RS232 ↔ TTL, mit 3,3 V versorgt |
| Fröling Lambdatronic S3200 | COM1 an der Gehäuserückseite |

Verdrahtung: Kessel-TX → MAX3232 → **GPIO18**, **GPIO17** → MAX3232 → Kessel-RX, GND, 3,3 V
([verdrahtung.pdf](docs/verdrahtung.pdf)).

## Schnellstart

1. Firmware mit dem **[Web-Flasher](https://peterhagelhof7-cmd.github.io/esp-p4d/)** aufspielen
   (Chrome/Edge; S2 Mini mit gedrückter Taste **0** einstecken).
2. Mit dem WLAN **`esp-p4d-setup`** (Passwort `esp-p4d-setup`) verbinden → `http://192.168.4.1`
   → Einstellungen (`admin`/`admin`) → Heim-WLAN eintragen → Passwort ändern.
3. COM1 anschließen – `http://esp-p4d.local` zeigt die Werte.

Alles Weitere – Weboberfläche, API, feste IP, Updates, Fehlersuche – im
**[Admin-Guide](docs/admin-guide.md)**.

## API (Kurzform)

Header `X-API-Key: <Schlüssel aus den Einstellungen>`:

| Endpunkt | Inhalt |
|---|---|
| `GET /api/v1/summary` | Zustand, Störung, ausgewählte Werte |
| `GET /api/v1/values` | alle Werte mit Rohwert, Faktor, Alter |
| `GET /api/v1/status` | Gerät, WLAN, Vitalwerte, Kessel-Statistik, Fehlerspeicher |

```json
{"ts":1760900000,"name":"ESP-P4d","online":true,"mode":"Winterbetrieb","state":"Heizen",
 "error":null,"values":[{"k":"0x0000","n":"Kesseltemperatur","v":72.5,"u":"°C"}]}
```

## Selbst bauen

[PlatformIO](https://platformio.org/), Versionen fest in `firmware/platformio.ini`:

```sh
cd firmware
pio run -t upload        # bauen + flashen (S2 Mini im Download-Modus)
pio device monitor       # USB-Konsole, 115200
```

Unter Windows erledigt `tools\Flash.ps1` Installation, Build und Flash in einem Schritt.
`tools\webflasher-update.ps1 -Version <x.y.z>` füllt nach einem Build den Web-Flasher (`docs/`)
und legt das OTA-Image `dist/esp-p4d-<x.y.z>.bin` fürs Release ab.

## Dokumentation

| Datei | Inhalt |
|---|---|
| [docs/admin-guide.md](docs/admin-guide.md) | Installation, Einrichtung, Bedienung, API, Fehlersuche |
| [docs/protokoll.md](docs/protokoll.md) | COM1-Protokoll (Rahmen, Byte-Stuffing, CRC, Kommandos) mit Quellenangaben aus linux-p4d |
| [docs/machbarkeitsanalyse.md](docs/machbarkeitsanalyse.md) | Entwurfsentscheidungen (Port, Hardware, Flash-Verschleiß, Lizenz) |
| [docs/verdrahtung.pdf](docs/verdrahtung.pdf) | Anschluss-Tabelle und Blockbild |

## Status

Version 0.3.0, im Dauerbetrieb an einem S3200 (Kessel-Software 50.04.05.07). Geplant: Historie
als CSV auf dem Gerät, MQTT.

## Lizenz

**GPL-2.0** – siehe [LICENSE](LICENSE). Das Protokoll-Modul (`firmware/src/P4Com.*`) ist aus
[horchi/linux-p4d](https://github.com/horchi/linux-p4d) (© Jörg Wendel, GPL-2.0) abgeleitet;
deshalb steht das ganze Projekt unter derselben Lizenz.

Fröling und Lambdatronic sind Marken der Fröling Heizkessel- und Behälterbau Ges.m.b.H. Dieses Projekt
ist kein Produkt von Fröling und wird von Fröling weder unterstützt noch geprüft. Nutzung auf
eigene Verantwortung.
