# ESP-P4d — Machbarkeitsanalyse

Portierung von **P4d** ([horchi/linux-p4d](https://github.com/horchi/linux-p4d)) auf einen
**ESP32-S2 (Wemos S2 Mini)** zur Kommunikation mit einer Fröling-Heizung über RS232.

Stand: 2026-08-14 (Erststand).

## 1. Ziel und Randbedingungen

- **Kessel:** Fröling mit **Lambdatronic S3200**, **Baujahr 2011**.
- **Modbus ist KEINE Option** (die Modbus-Schnittstelle der Lambdatronic kam erst ~2014) →
  es bleibt **nur das native P4-Protokoll** = der eigentliche „P4d portieren"-Weg (Weg B).
- **Zielarchitektur (vom Nutzer festgelegt):** eigenständiges Gerät mit **eingebautem
  Webserver + MQTT-Publish**. **Kein** Home Assistant, **kein** ESPHome.
- **Hardware:** Wemos S2 Mini (ESP32-S2) + **MAX3232** (RS232↔TTL, mit 3,3 V betrieben).

### Anforderungen (bestätigt 2026-08-14)

- **Historie: 1 Woche**, als **CSV downloadbar**.
- **Logs downloadbar** (wie `/log.txt` in sensormeter/esp-anwesenheit).
- **Installer-AP** fürs Erstsetup (NetworkManager-Fallback-AP wie in der Familie).
- **Mobiltaugliche Weboberfläche** (responsive — `clamp()`-Schrift + `.tablewrap`, wie gehabt).
- **MQTT:** noch nicht vorhanden, **Zukunfts-Feature** (Struktur vorsehen, niedrige Priorität;
  aktuell **kein** Broker vorhanden). Web-UI läuft davon unabhängig.

Alle Punkte sind im bestehenden Baukasten (sensormeter/esp-anwesenheit) bereits gelöst und
werden wiederverwendet.

## 2. Was portierbar ist — und was nicht

`linux-p4d` ist ein großer **Linux-Daemon (C)**. Ein 1:1-Port ist weder machbar noch sinnvoll:

| Teil von linux-p4d | ESP-P4d |
|---|---|
| **Serielle Kommunikation mit dem Kessel** (`p4io.c/.h`) | **portieren** — das ist der Kern |
| MariaDB-Speicherung | entfällt → **MQTT-Push** + RAM/PSRAM-Ring (keine DB auf dem MCU) |
| libwebsockets-Web-UI (htdocs) | **schlankes eingebautes Web-UI** (Live-Werte, Config, OTA) |
| MQTT | **behalten** (schlanke ESP-MQTT-Anbindung) |
| Lua-Scripting, libcurl, Mail, LMS (`lmccom`), 1-Wire | **out of scope** |

Es entsteht also kein Daemon-Klon, sondern ein **ESP-natives Gerät im bewährten Baukasten**
(NetworkManager/WebServerManager/OtaManager/ConfigManager wie in sensormeter/esp-anwesenheit),
dessen Kern das Fröling-Protokoll spricht.

## 3. Das native P4-Protokoll (Weg B) — Bewertung

**Entscheidend: Das Protokoll muss NICHT reverse-engineert werden — die funktionierende
Referenz liegt als GPL-Quellcode in `linux-p4d` vor** (`p4io.c/.h`). Wir portieren Framing,
Escaping, CRC, Structs und Command-Flow.

Aus `p4io.c/.h` bekannt:
- **Frame:** `Header` = `id` (2 B) + `size` (2 B) + `command` (1 B) → Payload → **1-Byte-CRC**
  (über den *unescapten* Puffer berechnet, letztes Byte des Frames).
- **Byte-Stuffing:** `0xFE` ist Escape-Byte. Regeln u. a.: `0x02→02 00`, `0x2B→2B 00`,
  `0xFE→FE 00`, `0x11→FE 12`, `0x13→FE 14` (XON/XOFF `0x11`/`0x13` werden maskiert →
  Software-Flusskontrolle auf der Leitung).
- **Kommandos:** `cmdCheck`, `cmdGetState`/`cmdGetVersion`, `cmdSetDateTime`,
  `cmdGetParameter`/`cmdSetParameter`, `cmdGetValue`, `cmdGetDigOut`/`GetDigIn`/`GetAnlOut`,
  `cmdGetTimesFirst/Next/Set`, `cmdGetErrorFirst/Next`, `cmdGetValueListFirst/Next`,
  `cmdGetMenuListFirst/Next`. → Werte-/Menübaum ist per **First/Next enumerierbar**.
- **Ablauf:** strikt **synchron** — Request senden → Antwort-Header lesen → Payload-Felder
  parsen → CRC prüfen → `id` gegen die eigene Command-ID prüfen. Adressen **big-endian**.

Das ist **Standard-UART-Arbeit** und liegt voll im Rahmen eines ESP32-S2.

**Noch aus dem Quellcode zu ziehen (kein Blocker, To-do beim Umsetzen):**
- Exakte **Baudrate/Leitungsparameter** (in der `Serial`-Klasse gesetzt, nicht in `p4io.c`;
  beim P4 nativ typ. 57600 8N1 — **aus dem Code + an der Anlage verifizieren**).
- Exakte **Hex-Werte der `cmd*`-Konstanten** und die **Byte-Layouts** der Structs
  (`Value`, `ValueSpec`, `MenuItem`, `Status`, `ConfigParameter` …).
- Die **`crc()`-Funktion** (verbatim übernehmen).

## 4. Lizenz-Hinweis

`linux-p4d` steht unter der **GPL-2.0**. Wenn wir Protokoll-Code daraus übernehmen, wird die
ESP-Firmware ein abgeleitetes Werk → **muss ebenfalls GPL-2.0** sein (abweichend von den
sonst frei gehaltenen Familien-Projekten). Bewusst so einplanen.

## 5. Flash-Verschleiß („p4d hat die SD-Karte verschlissen") — gelöst

Das SD-Sterben auf dem Pi kam **nicht vom Protokoll**, sondern von der **Datenhaltung**:
p4d schrieb laufend Messwerte in MariaDB + Logs auf eine wear-unfreundliche SD-Karte.

Auf dem ESP tritt das mit der Ziel-Architektur **gar nicht erst auf**:
- **Kesselauslesen (Weg B) schreibt NICHTS in den Flash** (nur UART).
- **Webserver** zeigt Live-Werte aus RAM, **MQTT** publisht übers Netz → **fast keine Writes**.
- **PSRAM (2 MB am S2 Mini)** trägt Live-Ring + jüngste Historie **flüchtig** (null Verschleiß).
- **Config-Writes** (NVS/LittleFS) nur bei Einstellungsänderung → vernachlässigbar.
**Konkreter Plan für die 1-Woche-CSV-Historie (gewünscht):**
- Die Woche wird **im PSRAM** als Ring gehalten (flüchtig, **null Verschleiß**) und dient
  dem Web-UI (Live + Kurzhistorie/Graphen).
- Persistenz als downloadbare **CSV auf LittleFS** durch **gebündeltes Anhängen** (z. B. alle
  paar Minuten ein Batch statt pro Messwert) + LittleFS-**Wear-Leveling**; **kein NVS** für
  Zeitreihen. Rolling-7-Tage-Fenster (z. B. Tagesdateien, ältere > 7 Tage prunen — Muster wie
  esp-anwesenheit). Beim Boot Ring aus der CSV nachladen.
- Grobe Rechnung: ein Batch/Minute über ~1 MB verteilt ⇒ **~40+ Jahre** Flash-Lebensdauer.

**Design-Regel:** „lesen → live zeigen/pushen, Flash nur gebündelt+selten" — dann gibt es kein
DB-auf-Speichermedium-Problem trotz Historie.

## 6. Hardware-Bewertung

- **S2 Mini + MAX3232 @ 3,3 V:** passend. UART + Pegelwandlung ist genau das Benötigte; die
  3,3-V-Versorgung des MAX3232 ist korrekt (kein 5 V an ESP-Pins).
- **ESP32-S2-spezifisch:** Single-Core LX7 @240 MHz, WLAN-only (kein BT), natives USB;
  S2 Mini: **4 MB Flash + 2 MB PSRAM**. Reicht locker (kein rechenintensiver Task; das
  Protokoll ist synchron und langsam). 2 UARTs vorhanden → RX/TX zum MAX3232 frei wählbar.
- RS232-Anschluss am Kessel: **COM1** (nativer Service-Port, den p4d nutzt) — an der Anlage
  prüfen, welcher Port frei/erreichbar ist.

## 7. Verdikt

- **Machbar (Weg B).** Der Kern — das Fröling-Protokoll sprechen — ist auf dem ESP32-S2 gut
  umsetzbar, weil die **komplette Referenzimplementierung (GPL) vorliegt** und portiert werden
  kann statt reverse-engineert. Kein Modbus, keine Umkonfiguration der Anlage nötig.
- **Aufwand:** mittel — die Protokoll-Portierung (Framing/Escaping/CRC/Structs/Command-Flow)
  ist Fleißarbeit + Test an der echten Anlage; Web/MQTT/Netz sind Baukasten-Wiederverwendung.
- **Flash-Verschleiß:** mit „live pushen statt speichern" **kein** Thema.

## 8. Lizenz-Entscheidung

Firmware wird **GPL-2.0** (weil Protokoll-Code aus dem GPL-2.0-Projekt `linux-p4d` portiert wird;
dort ist keine „oder spätere Version“-Klausel angegeben, daher bewusst dieselbe Version). Für
ein ohnehin offen veröffentlichtes Projekt praktisch keine Bürde: GPL-Lizenzdatei beilegen,
Herkunft nennen. Eigenes Repo → keine Auswirkung auf die anderen (nicht-GPL) Familien-Projekte.

## 9. Festlegungen (2026-08-14)

- **Betrieb: nur lesen** (Monitoring). Kein Parameter-Schreiben.
- **Port: COM1** (extern an der Gehäuserückseite) → **natives Service-Protokoll**, 57600 8N1
  (siehe `protokoll.md §A`). COM2/ASCII und Modbus sind vom Tisch.
- **Konsequenz:** Request/Response → der ESP **pollt aktiv** (Kessel sendet nichts von selbst);
  read-only = nur Lese-Kommandos; Verkabelung TX+RX+GND über MAX3232, nur ein Master auf COM1.

## 10. Nächste Schritte

1. **Protokoll-Konstanten gezogen** → erledigt (`protokoll.md`).
2. **Gerüst** aus dem Baukasten aufsetzen (Network/Web/Ota/Config + Installer-AP + responsive
   UI + `/log.txt`- und CSV-Download) + **COM1-Protokoll-Modul** mit **Proof-of-Life**
   (`cmdCheck` 0x22, `cmdGetVersion` 0x41), dann Werte-Katalog (`cmdGetValueListFirst/Next`)
   und zyklisches `cmdGetValue`.
3. **Historie** (PSRAM-Ring, 1 Woche) + **CSV-Persistenz** (gebündelt, Wear-Leveling).
4. **MQTT** später als optionales Publish-Modul.
5. **Bring-up an der Anlage:** Verkabelung MAX3232↔COM1, TX/RX-Kreuzung empirisch, altes p4d aus.
