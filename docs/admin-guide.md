# ESP-P4d – Admin-Guide

Anleitung für Aufbau, Installation, Einrichtung und Betrieb des ESP-P4d: ein Wemos S2 Mini
(ESP32-S2), der eine **Fröling-Heizung mit Lambdatronic S3200** über deren Service-Schnittstelle
**COM1** ausliest und die Werte im WLAN anzeigt und per API bereitstellt.

Inhalt

1. [Was das Gerät kann – und was nicht](#1-was-das-gerät-kann--und-was-nicht)
2. [Hardware und Verdrahtung](#2-hardware-und-verdrahtung)
3. [Firmware installieren](#3-firmware-installieren)
4. [Ersteinrichtung](#4-ersteinrichtung)
5. [Weboberfläche](#5-weboberfläche)
6. [Werte und Störungen verstehen](#6-werte-und-störungen-verstehen)
7. [Updates und Einstellungs-Backup](#7-updates-und-einstellungs-backup)
8. [API](#8-api)
9. [Anbindung an home-central](#9-anbindung-an-home-central)
10. [Fehlersuche](#10-fehlersuche)
11. [Sicherheit](#11-sicherheit)

---

## 1. Was das Gerät kann – und was nicht

- **Nur lesen.** Das Gerät fragt Zustand, Werte und Fehlerspeicher ab. Es ändert **keine**
  Parameter am Kessel.
- **Lambdatronic S3200 über COM1** (natives Service-Protokoll, 57600 Baud 8N1), wie es
  [linux-p4d](https://github.com/horchi/linux-p4d) verwendet. Erprobt an einem S3200 mit
  Kessel-Software 50.04.05.07. Ältere Regler (z. B. S3100) sprechen ein anderes Protokoll.
- Welche Werte es gibt, liefert der Kessel selbst (**Werte-Katalog**) – typisch 150–250 Stück:
  Kessel-, Abgas-, Puffer-, Boiler-, Vorlauf- und Außentemperatur, Klappen, Saugzug, Solar,
  Pufferladezustand, empfohlene Holzmenge usw.
- Es läuft **eigenständig** (kein Raspberry Pi, keine Datenbank). Über die API kann ein anderer
  Rechner (z. B. [home-central](https://github.com/peterhagelhof7-cmd/home-central)) die Werte
  abholen, aufzeichnen und Meldungen verschicken.
- **Nur ein Gerät an COM1.** Läuft dort schon ein p4d (Raspberry Pi), muss es aus sein.

## 2. Hardware und Verdrahtung

| Teil | Hinweis |
|---|---|
| **Wemos S2 Mini** (ESP32-S2, 4 MB Flash, 2 MB PSRAM) | USB-C, Stromversorgung per USB-Netzteil |
| **MAX3232-Modul** (RS232 ↔ TTL) | mit **3,3 V** vom S2 Mini versorgen, nicht 5 V |
| Kabel zu COM1 | COM1 sitzt außen an der Gehäuserückseite des Reglers |

| von | nach |
|---|---|
| Kessel-TX | MAX3232 R1IN → R1OUT → **GPIO18** (RX des S2) |
| **GPIO17** (TX des S2) | MAX3232 T1IN → T1OUT → Kessel-RX |
| GND Kessel | GND MAX3232 / S2 Mini |
| 3V3 S2 Mini | VCC MAX3232 |

Antwortet der Kessel nicht, zuerst **TX/RX tauschen** (Null-Modem ja/nein ist je nach Kabel
verschieden). Die Pins lassen sich in `firmware/src/main.cpp` (`P4_RX_PIN`/`P4_TX_PIN`) ändern.

Anleitung mit Blockbild: [verdrahtung.pdf](verdrahtung.pdf).

**Funk:** Der S2 Mini sendet mit 8,5 dBm (bei voller Leistung war sein Access Point auf diesen
Boards unsichtbar). Am Einbauort im Heizraum das Signal prüfen – es steht auf der Startseite und
sollte besser als −75 dBm sein.

## 3. Firmware installieren

### Web-Flasher (empfohlen)

<https://peterhagelhof7-cmd.github.io/esp-p4d/> mit Chrome, Edge oder Opera am Desktop öffnen.

1. **Download-Modus:** Taste **0** gedrückt halten und das Board einstecken (oder 0 halten,
   RST kurz drücken), dann loslassen. Es erscheint ein Port „ESP32-S2“ (USB-ID `303A:0002`).
2. **Installieren** → Port wählen → beim ersten Mal „Gerät löschen“ anhaken.
3. Nach dem Flashen das Board **einmal ab- und wieder anstecken** (der S2 startet nicht von selbst).

### Mit PlatformIO

```powershell
tools\Flash.ps1 -Monitor        # Windows: installiert bei Bedarf Python/PlatformIO, baut, flasht
```

```sh
cd firmware
pio run -t upload               # bauen und flashen
pio device monitor              # Ausgabe (USB-CDC, 115200)
```

Die Versionen von Plattform und Bibliotheken sind in `firmware/platformio.ini` fest eingetragen;
ein Build aus dem Repo entspricht dem Release.

## 4. Ersteinrichtung

1. Ohne eingerichtetes WLAN öffnet das Gerät das **Einrichtungs-WLAN `esp-p4d-setup`**
   (Passwort `esp-p4d-setup`). Mit dem Handy verbinden – die Seite öffnet sich meist von selbst,
   sonst `http://192.168.4.1`.
2. **Einstellungen** (Benutzer `admin`, Passwort `admin`) → **Netze suchen** → WLAN wählen,
   Passwort eingeben → **Verbinden**. Danach ist das Gerät unter `http://esp-p4d.local` (oder der
   Adresse aus dem Router) erreichbar. Das Einrichtungs-WLAN geht 30 s nach erfolgreicher
   Verbindung aus.
3. Unter **Gerät** das **Einstellungs-Passwort ändern** (mindestens 6 Zeichen; die Startseite
   erinnert daran), bei Bedarf Gerätename (= Name im Netz, z. B. `esp-p4d.local`) und Zeitserver
   (Standard `pool.ntp.org`; Zeitzone Mitteleuropa). Name und Zeitserver wirken nach einem Neustart.
4. COM1 anschließen. Die Startseite zeigt „verbunden“, sobald der Kessel antwortet; danach liest
   das Gerät den Werte-Katalog (wenige Sekunden).

**Nie ausgesperrt:** Ist das WLAN 5 Minuten nicht erreichbar (oder falsch eingetragen), kommt
`esp-p4d-setup` von selbst dazu. Das Gerät versucht es jede Minute erneut, solange niemand am
Einrichtungs-WLAN hängt, und schaltet es nach erfolgreicher Verbindung wieder ab.

### Feste IP-Adresse

*Einstellungen → IP-Adresse*, Haken „feste IP-Adresse“. Die Felder sind mit den aktuellen
DHCP-Werten vorbelegt; DNS 1 leer = Router.

- Vor dem Speichern prüft das Gerät per ARP und Ping, ob die Adresse belegt ist (belegt → wird nicht
  gespeichert).
- Nach dem Neustart meldet es sich **zuerst per DHCP** an, prüft die Adresse noch einmal, schaltet
  um und testet den Router. Ist die Adresse belegt oder der Router nicht erreichbar, **bleibt es bei
  DHCP**; der Grund steht im Ereignis-Log und unter *IP-Adresse*.
- Adresse außerhalb des DHCP-Bereichs des Routers wählen.

## 5. Weboberfläche

| Seite | Inhalt |
|---|---|
| **Status** `/` | Verbindung zum Kessel, Betriebsart, Zustand, anstehende Störung, ausgewählte Werte; WLAN (Signal, Access Point, Abbrüche); Gerät (Firmware, Uhrzeit, Laufzeit, letzter Neustart, Chip-Temperatur, Speicher, Dauer einer COM1-Runde) |
| **Werte** `/values` | alle Werte des Kessels mit Filter; Haken = Auswahl für Startseite und API; „Leere Werte ausblenden“ blendet Werte mit 0 und fehlende Fühler aus (markierte bleiben sichtbar) |
| **Konsole** `/console` | COM1-Verkehr live: jeder Rahmen gesendet/empfangen als Hex, fehlerhafte rot, filterbar nach Kommando; Ereignis-Log |
| **Einstellungen** `/settings` | WLAN, IP-Adresse, Gerät (Name, Zeitserver, Passwort), API-Schlüssel, Werte-Katalog neu lesen, Einstellungs-Backup, Firmware-Update, Ereignis-Log, Neustart |

Status- und Werte-Seite sind ohne Anmeldung lesbar. Alles, was etwas ändert oder Geheimes zeigt
(Einstellungen, Konsole, Log), verlangt den Benutzer `admin`.

**USB-Konsole** (115200 Baud): `l` Ereignis-Log, `s` Kurzstatus, `w` WLAN-Suche, `j` API-JSON,
`c` COM1-Mitschnitt.

## 6. Werte und Störungen verstehen

**Abfragetakt:** Zustand alle 10 s, Fehlerspeicher alle 5 min, dazwischen alle Werte reihum –
eine Runde über ~180 Werte dauert etwa 10 s.

**Werte-Katalog:** Der Kessel liefert seine Werte nach Menüs gegliedert; manche Adressen stehen
mehrfach darin. Das Gerät behält jede Adresse nur einmal. Nach einem Software-Update am Kessel
*Einstellungen → Werte-Katalog neu lesen*.

**Einheiten:** wie vom Kessel geliefert; abgekürzte Einheiten werden ergänzt („°“ → „°C“,
„k“ → „kg“).

**Fühler fehlt:** Ein nicht angeschlossener Temperaturfühler meldet 127,0 °C. Die Oberfläche zeigt
„Fühler fehlt“, die API liefert `nc:true` bzw. `v:null`.

**Leere Werte:** Viele Einträge gehören zu Heizkreisen, Boilern oder Puffern, die an der Anlage gar
nicht vorhanden sind – sie stehen dauerhaft auf 0.

**Fehlerspeicher:** Der Kessel liefert ihn älteste zuerst; das Gerät liest ihn ganz und behält die
**20 neuesten** Einträge (neueste zuerst). Jeder Eintrag hat einen Zustand:

| Zustand | Bedeutung |
|---|---|
| 1 | gekommen |
| 2 | quittiert |
| 4 | gegangen |

**Anstehende Störung** = neuester „gekommen“-Eintrag, zu dem danach kein „gegangen“ derselben
Fehlernummer kam. „Quittiert“ allein beendet sie nicht.

## 7. Updates und Einstellungs-Backup

**Firmware-Update (OTA):** *Einstellungen → Firmware-Update* mit `esp-p4d-<Version>.bin` aus den
[Releases](https://github.com/peterhagelhof7-cmd/esp-p4d/releases). Das Gerät nimmt nur
ESP-P4d-Images an (Kennung `SM-FW-ID:ESP-P4D:<Version>` in der Datei) und nur gleiche oder neuere
Versionen – für ein Zurück gibt es den Haken „ältere Version erlauben“. Einstellungen bleiben
erhalten. Per Kommandozeile:

```sh
curl -u admin:<passwort> -F "file=@esp-p4d-0.3.0.bin" http://esp-p4d.local/update
```

**Einstellungs-Backup:** *Einstellungen → Einstellungs-Backup* lädt eine JSON-Datei mit **allen**
Einstellungen herunter – **inklusive** WLAN-Passwort, Einstellungs-Passwort und API-Schlüssel, also
sicher aufbewahren. Einspielen prüft die Datei und startet neu. Passt das WLAN im Backup nicht, hilft
das Einrichtungs-WLAN. Eine feste IP aus einem Backup wird beim Start immer neu geprüft.

## 8. API

Die Maschinen-API unter `/api/v1/` verlangt den Header **`X-API-Key: <Schlüssel>`**. Der Schlüssel
steht unter *Einstellungen → API für home-central*, wird beim ersten Start zufällig erzeugt und lässt
sich dort neu erzeugen. Ohne oder mit falschem Schlüssel: `401`.

### `GET /api/v1/summary`

Zustand und die ausgewählten Werte (ohne Auswahl: alle).

```json
{"ts":1760900000,"name":"ESP-P4d","online":true,"mode":"Winterbetrieb","state":"Heizen",
 "error":null,"values":[{"k":"0x0000","n":"Kesseltemperatur","v":72.5,"u":"°C"}]}
```

| Feld | Bedeutung |
|---|---|
| `ts` | Unix-Zeit (NTP); `null`, solange die Uhr nicht gestellt ist |
| `online` | `false` = Kessel antwortet nicht auf COM1 → alle `v` sind `null` |
| `mode`, `state` | Betriebsart und Zustand als Text vom Kessel |
| `error` | Text der anstehenden Störung (siehe Abschnitt 6) oder `null` |
| `k` | Adresse im Kessel – stabil, als Schlüssel verwenden |
| `n`, `u` | Name und Einheit |
| `v` | Wert; `null` bei fehlendem Fühler oder ohne Verbindung |

### `GET /api/v1/values`

Alle Werte mit zusätzlich Faktor `f`, Typ `t`, Rohwert `raw`, Alter `age` (s), Auswahl `sel` und
`nc:true` bei fehlendem Fühler.

### `GET /api/v1/status`

Gerät und Kessel: Firmware, WLAN, Uhrzeit, Verbindung, Kessel-Software, anstehende Störung
`kessel.active` (oder `null`) und die 20 neuesten Einträge des Fehlerspeichers `kessel.errors`.
Dazu die **Vitalwerte** des Boards:

| Feld | Bedeutung |
|---|---|
| `device.wlan.rssi`, `bssid`, `channel` | Signal (dBm), verbundener Access Point, Kanal |
| `device.wlan.reconnects`, `connectedFor` | WLAN-Abbrüche seit Start, Sekunden seit der letzten Verbindung (−1 = getrennt) |
| `device.vital.chipTemp` | interne Chip-Temperatur in °C (±3 °C, liegt über der Raumtemperatur) |
| `device.vital.resetReason` | Grund des letzten Neustarts: `poweron`, `ext`, `sw` (auch Update), `panic`, `int_wdt`, `task_wdt`, `wdt`, `brownout` |
| `device.vital.abnormalReset` | `true` nach Absturz, Watchdog oder Spannungseinbruch |
| `device.vital.boots` | Neustart-Zähler |
| `device.vital.heapFree`, `heapMin`, `heapMaxBlock` | freier interner Speicher jetzt, kleinster seit Start, größter zusammenhängender Block (Bytes) |
| `device.vital.psramSize`, `psramFree` | PSRAM (Bytes) |
| `device.vital.loopMaxMs`, `stackFree` | längster Durchlauf der Hauptschleife seit Start (ms), kleinste Stack-Reserve (Bytes) |
| `device.vital.fsUsed`, `fsTotal` | Dateisystem (Bytes) |
| `device.vital.ntpSyncAgo` | Sekunden seit der letzten Zeit-Synchronisierung (`null` = noch nie) |
| `kessel.polls`, `failures` | COM1-Abfragen und Fehlschläge seit Start |
| `kessel.roundSec`, `roundPolls`, `roundFailures` | Dauer der letzten kompletten Werte-Runde, Abfragen und Fehler darin |

Ein Abruf alle 15–60 s reicht; die Werte ändern sich ohnehin nur im Takt einer Runde.

Ohne Schlüssel lesbar (für die eigene Oberfläche): `GET /api/status`, `GET /api/values`.

## 9. Anbindung an home-central

[home-central](https://github.com/peterhagelhof7-cmd/home-central) holt `summary`, `values` und
`status` ab, zeichnet alle Werte auf, zeigt Diagramme, schickt Telegram-Meldungen (Störung,
„Feuer aus“, Gerät nicht erreichbar, Probleme des Boards) und beantwortet Bot-Befehle. Dort unter
*Einstellungen → Heizung (esp-p4d)* die Adresse (am besten die feste IP) und den API-Schlüssel
eintragen.

## 10. Fehlersuche

| Problem | Ursache / Abhilfe |
|---|---|
| Kessel „keine Antwort“ | TX/RX tauschen; GND verbunden?; MAX3232 an 3,3 V?; kein anderes p4d an COM1; in der **Konsole** sieht man, ob überhaupt Bytes zurückkommen |
| Einrichtungs-WLAN nicht sichtbar | Board neu starten; es erscheint nur ohne eingerichtetes WLAN oder nach 5 min ohne Verbindung |
| Gerät nach Absturz nicht mehr erreichbar, PC zeigt `303A:0002` | Der S2 springt nach einem Absturz in den Download-Modus. Ab- und anstecken, oder `esptool.py --chip esp32s2 --before no_reset --after watchdog_reset read_mac` |
| Signal schwach (unter −80 dBm) | Board/Antenne anders ausrichten, nicht in Metallgehäuse, ggf. USB-Verlängerung |
| Update abgelehnt | falsche Datei (keine ESP-P4d-Kennung) oder ältere Version → Haken „ältere Version erlauben“ |
| Werte fehlen nach Kessel-Update | *Werte-Katalog neu lesen* |
| Uhrzeit fehlt | Zeitserver erreichbar? (`ntpSyncAgo` in `/api/v1/status`) |

## 11. Sicherheit

- Das Gerät gehört ins **lokale Netz**, nicht ins Internet (kein HTTPS).
- **Einstellungs-Passwort** sofort ändern; die Startseite warnt, solange `admin` gesetzt ist.
- Der **API-Schlüssel** schützt `/api/v1/`; bei Verdacht unter *Einstellungen* neu erzeugen (dann
  in home-central nachtragen).
- Status- und Werte-Seite sind im LAN ohne Anmeldung lesbar (nur lesen, keine Geheimnisse).
- Das **Einstellungs-Backup** enthält alle Passwörter im Klartext.
- Das Gerät **schreibt nichts** an den Kessel.
