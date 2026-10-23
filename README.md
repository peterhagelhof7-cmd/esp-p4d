# ESP-P4d

Eine **Fröling-Heizung (Lambdatronic S3200)** mit einem **ESP32-S2 (Wemos S2 Mini)**
auslesen und als eigenständiges Gerät (Weboberfläche + später MQTT) darstellen —
**ohne** Raspberry Pi, Datenbank oder Home Assistant.

Portierung des seriellen COM1-Protokolls aus **[horchi/linux-p4d](https://github.com/horchi/linux-p4d)**.

> **Status v0.2.2:** COM1 (Zustand, Werte-Katalog, Werte, Fehlerpuffer), WLAN mit
> Einrichtungs-Access-Point, Weboberfläche mit COM1-Konsole, Einstellungs-Backup, OTA und
> API für home-central gebaut. **Seit 2026-10-23 am Kessel (S3200, SW 50.04.05.07) im Betrieb.** Historie (CSV) und MQTT folgen.
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

**Verdrahtungs-Anleitung als PDF:** [`verdrahtung.pdf`](verdrahtung.pdf) (Anschluss-Tabelle
+ Blockbild; Quelle: [`docs/verdrahtung.html`](docs/verdrahtung.html), mit Chrome
`--print-to-pdf` regenerierbar).

## Einrichten (nur WLAN)

1. Ohne eingerichtetes WLAN öffnet das Gerät das Einrichtungs-WLAN **`esp-p4d-setup`**
   (Passwort **`esp-p4d-setup`**). Mit dem Handy verbinden – die Seite öffnet sich meist von
   selbst, sonst **http://192.168.4.1**.
2. **Einstellungen** (Benutzer `admin`, Passwort `admin`) → *Netze suchen* → WLAN wählen,
   Passwort eingeben → *Verbinden*. Danach erreichbar unter **http://esp-p4d.local** (oder der
   Adresse im Router). Das Einrichtungs-WLAN geht 30 s nach erfolgreicher Verbindung aus.
3. Einstellungs-Passwort ändern (die Startseite erinnert daran).

**Nie ausgesperrt:** Ist das WLAN 5 Minuten weg (oder falsch eingetragen), kommt
`esp-p4d-setup` von selbst wieder. Das Gerät verbindet sich neu, sobald der Router wieder da ist
(Versuch jede Minute, solange niemand am Einrichtungs-WLAN hängt).

**Feste IP-Adresse** (ab 0.2.1, sonst DHCP; Mechanik wie beim esp-meshcore-gateway): *Einstellungen →
IP-Adresse*, Haken „feste IP-Adresse“. Die Felder sind mit den aktuellen DHCP-Werten vorbelegt; DNS 1 leer =
Router. Vor dem Speichern prüft das Gerät per ARP und Ping, ob die Adresse belegt ist (belegt → wird nicht
gespeichert). Nach dem Neustart meldet es sich **erst per DHCP** an, prüft die Adresse noch einmal, schaltet um
und testet den Router. Belegt oder Router stumm → es **bleibt bei DHCP**, die feste IP wird verworfen (Grund im
Ereignis-Log und unter *IP-Adresse*). Eine Adresse aus einem eingespielten Backup wird beim Start immer neu
geprüft. Adresse am besten außerhalb des DHCP-Bereichs des Routers wählen.

Hinweis Hardware: Der Wemos S2 Mini sendet mit 8,5 dBm statt voller Leistung – bei 19,5 dBm war
sein Access-Point für andere Geräte unsichtbar. Reichweite am Einbauort prüfen (Signal steht
auf der Startseite).

## Weboberfläche

| Seite | Inhalt |
|---|---|
| **Status** `/` | Verbindung zur Heizung, Betriebsart, Zustand, Störung, ausgewählte Werte, WLAN, Gerät |
| **Werte** `/values` | alle Werte, die die Heizung meldet (Werte-Katalog); Haken = geht an home-central |
| **Konsole** `/console` | COM1-Kommunikation live: jeder Rahmen gesendet/empfangen als Hex, Fehler rot; Ereignis-Log |
| **Einstellungen** `/settings` | WLAN, Gerätename, Zeitserver, Passwort, API-Schlüssel, Werte-Katalog neu lesen, **Einstellungs-Backup** (Download/Einspielen), **Firmware-Update (OTA)**, Log, Neustart |

Alles, was etwas ändert oder Geheimes zeigt, braucht den Benutzer `admin`.

- **Einstellungs-Backup:** eine JSON-Datei mit allen Einstellungen **inkl.** WLAN-Passwort,
  Einstellungs-Passwort und API-Schlüssel. Einspielen prüft die Datei und startet neu.
- **OTA:** nimmt nur ESP-P4d-Images mit gleicher oder neuerer Version an (Kennung
  `SM-FW-ID:ESP-P4D:<Version>` in der `.bin`); Einstellungen bleiben erhalten.
- **USB-Konsole** (115200): `l` Ereignis-Log, `s` Kurzstatus, `w` WLAN-Suche, `j` API-JSON,
  `c` COM1-Mitschnitt.

## API für home-central

Header **`X-API-Key: <Schlüssel>`** (steht unter *Einstellungen → API für home-central*,
wird beim ersten Start zufällig erzeugt). Ohne/mit falschem Schlüssel: `401`.

`GET /api/v1/summary` – Zustand und die ausgewählten Werte (ohne Auswahl: alle):

```json
{"ts":1760900000,"name":"ESP-P4d","online":true,"mode":"Automatik","state":"Heizen",
 "error":null,"values":[{"k":"0x0000","n":"Kesseltemperatur","v":72.5,"u":"°C"}]}
```

- `ts` = Unix-Zeit (NTP), `null` solange die Uhr nicht gestellt ist
- `online:false` = Heizung antwortet nicht auf COM1 → alle `v` sind `null`
- `error` = Text der anstehenden Störung: neuester Eintrag „gekommen“ (Zustand 1), zu dem
  danach kein „gegangen“ (4) derselben Nummer kam; „quittiert“ (2) allein beendet sie nicht
- `k` = Adresse im Kessel (stabil), `n`/`u` = Name/Einheit wie vom Kessel geliefert (nur „°“
  wird zu „°C“); jede Adresse kommt nur einmal vor (der Kessel-Katalog enthält Dubletten)
- `v` = `null` auch bei nicht angeschlossenem Fühler (Kessel meldet dann 127,0 °C)

`GET /api/v1/values` – alle Werte mit Faktor `f`, Typ `t`, Rohwert `raw`, Alter `age` (s),
Auswahl `sel` und `nc:true` bei fehlendem Fühler. `GET /api/v1/status` – Gerät (Firmware, WLAN,
Uhrzeit) und Kessel inkl. anstehender Störung `active` (oder `null`) und den 20 neuesten
Einträgen des Fehlerpuffers `errors` (neueste zuerst; der Kessel selbst liefert älteste zuerst,
deshalb wird der ganze Puffer gelesen).

Abfrage: Zustand alle 10 s, Fehlerpuffer alle 5 min, alle Werte reihum (eine Runde dauert je
nach Anzahl einige Sekunden). Ein Abruf durch home-central alle 15–60 s reicht.

## Bauen & Flashen

Am einfachsten mit dem Wrapper-Skript (installiert bei Bedarf Python/PlatformIO,
baut, wartet aufs Board, flasht, öffnet den Monitor):

```powershell
tools\Flash.ps1 -Monitor
```

Oder direkt mit PlatformIO:

```
cd firmware
pio run                 # bauen
pio run -t upload       # flashen (Wemos S2 Mini per USB)
pio device monitor      # Ausgabe (USB-CDC, 115200)
```

Danach geht es per OTA über die Weboberfläche (`firmware/.pio/build/lolin_s2_mini/firmware.bin`).

Erstes Flashen einer anderen Firmware auf dem Board: Download-Modus = GPIO0 beim Einstecken auf
GND (Taster „0“). Nach einem Absturz springt der S2 von selbst in den Download-Modus
(USB-ID `303A:0002`); `esptool --chip esp32s2 --before no_reset --after watchdog_reset read_mac`
startet die Firmware wieder.

## Lizenz

**GPLv3** (siehe [`LICENSE`](LICENSE)). Dieses Projekt leitet das serielle Protokoll aus dem
GPL-lizenzierten [linux-p4d](https://github.com/horchi/linux-p4d) von Jörg Wendel ab und steht
daher ebenfalls unter der GPL.
