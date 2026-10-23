# Fröling P4 / Lambdatronic S3200 — Protokoll-Referenz

Aus dem Quellcode von [horchi/linux-p4d](https://github.com/horchi/linux-p4d) (Jörg Wendel, GPL-2.0) extrahiert,
2026-08-14. Quellen jeweils als `datei:zeile` angegeben. Diese Datei ist die Grundlage fürs
ESP-Protokoll-Modul (Weg B).

> **Lizenz:** Alle Konstanten/Algorithmen stammen aus GPL-Code → das ESP-Modul, das sie
> nachbildet, steht ebenfalls unter der GPL-2.0 (siehe [machbarkeitsanalyse.md](machbarkeitsanalyse.md) §8).

---

## A) COM1 — natives Service-Protokoll (das „P4d"-Protokoll)

Das ist der Weg, den p4d nutzt: binäres, synchrones Request/Response mit CRC und Byte-Stuffing.

### Serielle Parameter
- **57600 Baud, 8N1** (8 Datenbits, keine Parität, 1 Stoppbit).
  Quelle: `p4cmd.c:96` → `Serial serial(B57600);`, `lib/serial.c:88` → `CS8 | CLOCAL | CREAD`.

### Frame-Aufbau (`service.h`, `p4io.c:prepareRequest`)
```
[ id: 2 B ][ size: 2 B ][ command: 1 B ][ payload: 0..254 B ][ crc: 1 B ]
\__ roh ___/\___________ ab hier maskiert (Byte-Stuffing) __________________/
```
- **id** = `commId = 0x02FD` (Bytes **0x02 0xFD**, big-endian). Rahmen-Startkennung, wird
  **roh** gesendet (nicht maskiert) und dient als Sync. Quelle: `service.h:58,84`.
- **size** = `htons(payloadLen + 1)` — Größe von Payload **inkl. CRC**, **ohne** Header,
  big-endian. Quelle: `p4io.c:33-39`, `service.h:60-65`.
- **command** = 1 Byte (siehe Kommandotabelle). 
- **payload** = entweder Adressen (je 2 B, big-endian, `htons`), oder Bytes, oder Text —
  nie gemischt. Quelle: `p4io.c:48-68`.
- **crc** = 1 Byte (siehe unten), über **Header+Payload unmaskiert** berechnet, dann mit
  maskiert. `sizeDataMax = 254`, `sizeMaxRequest = 260`. Quelle: `service.h:56-74`.
- **Words sind big-endian** (`v = (b1<<8)|b2`). Quelle: `p4io.c:178,195`.

### Byte-Stuffing (Maskierung ab Byte 2, `p4io.c:82-95`)
| Klartext | gesendet |
|---|---|
| `0x02` | `02 00` |
| `0x2B` | `2B 00` |
| `0xFE` | `FE 00` |
| `0x11` (XON) | `FE 12` |
| `0x13` (XOFF) | `FE 14` |
| sonst | unverändert |

**Decode beim Lesen (`p4io.c:104-160`):** `0x02`/`0x2B` → nächstes Byte muss `0x00` sein
(verwerfen); `0xFE` → nächstes Byte `0x12`→`0x11`, `0x14`→`0x13`, `0x00`→`0xFE`. Die
`id` (erste 2 B) wird **ungedecodet** gelesen und gegen `commId` geprüft (`p4io.c:235-246`).

### CRC (1 Byte, `lib/common.c:1804`)
```c
byte crc(const byte* data, int size) {
   byte dummy, crc = 0;
   for (int i = 0; i < size; i++) {
      dummy = data[i] * 2 & 0xFF;          // (b << 1) & 0xFF
      crc = crc ^ (byte)(data[i] ^ dummy); // crc ^= b ^ ((b<<1)&0xFF)
   }
   return crc;
}
```
Berechnet über `header + payload` (unmaskiert), das Ergebnis wird als letztes Byte angehängt
und dann mit-maskiert.

### Kommandos (`service.h:90-132`)
| cmd | Hex | Bedeutung |
|---|---|---|
| cmdCheck | `0x22` | Verbindung prüfen |
| cmdGetValue | `0x30` | aktuellen Wert (per Adresse) lesen |
| cmdGetValueListFirst/Next | `0x31`/`0x32` | Liste verfügbarer Werte (ValueSpec) |
| cmdGetMenuListFirst/Next | `0x37`/`0x38` | Menüstruktur lesen |
| cmdSetParameter | `0x39` | Einstellung ändern |
| cmdGetBaseSetup | `0x40` | Kessel-Konfiguration |
| cmdGetVersion | `0x41` | SW-Version + Zeit/Datum |
| cmdGetTimesFirst/Next | `0x42`/`0x43` | Zeiten lesen |
| cmdGetDigOut / AnlOut / DigIn | `0x44`/`0x45`/`0x46` | IO lesen |
| cmdGetErrorFirst/Next | `0x47`/`0x48` | Fehlerpuffer |
| cmdSetTimes | `0x50` | Zeiten ändern |
| cmdGetState | `0x51` | Kesselzustand (Status) |
| cmdSetDateTime | `0x54` | Zeit stellen |
| cmdGetParameter | `0x55` | Einstellung lesen |
| cmdSetDigOut / AnlOut / DigIn | `0x58`/`0x59`/`0x5A` | IO setzen |
| cmdGetForce / SetForce | `0x5E`/`0x7E` | Force-Modus |

Methoden→cmd (`p4io.c`): `getStatus`→`cmdGetState`; `getValue`→`cmdGetValue`;
`getParameter`→`cmdGetParameter`; `check`→`cmdCheck`; `syncTime`→`cmdSetDateTime`.

### Wichtige Struct-Layouts (`service.h`)
- **Value** (Antwort auf getValue): `word address; sword value;` — Wert ist **signed 16-bit**,
  Skalierung über `ValueSpec.factor` (i. d. R. `real = value / factor`). Antwortframe:
  `header + address(2) + value(2) + crc(1)` (`p4io.c:722`).
- **ValueSpec** (aus getValueList — die Metadaten je Wert): `address; unit; factor(word);
  name; description; type(word)`. Damit weiß man Name/Einheit/Faktor/Typ eines Werts.
- **Status** (getState): `time(6-B-Zeit/Datum); byte mode; byte state; version[12]` +
  Textauflösung `mode/state` über `stateInfos`/`toTitle`.
- **MenuItem** (getMenuList): `byte type; byte unknown1; word parent; word child; word address;
  word unknown2; text description; text unit` — Baum via parent/child, `type` = MenuStructType.
- **ConfigParameter** (get/setParameter): `address; unit; digits; value/min/max/def (sword);
  factor(byte); ub1..3; uw1` — `rValue = value` skaliert mit `factor`/`digits`.
- **IoValue** (dig/anl IO): `word address; byte mode; byte state;`
- **ErrorInfo**: `byte info; word number; byte state; time_t time; text;` (state: 1=gekommen,
  4=gegangen, 2=quittiert).

### Werte-Typ-Codes (MenuStructType, Auswahl, `service.h:143-214`)
`mstMesswert=0x03` (Messwert), `mstPar=0x07` (Zahl-Parameter), `mstParDig=0x08` (bool),
`mstParZeit=0x0a` (hh:mm), `mstParSet=0x32`, `mstDigOut=0x11`, `mstAnlOut=0x12`, `mstDigIn=0x13`.

### Typischer Ablauf (read-only Monitoring)
1. `cmdCheck` (0x22) → Verbindung ok?  ·  `cmdGetVersion` (0x41) → Proof-of-Life.
2. Einmalig `cmdGetValueListFirst/Next` (0x31/0x32) → alle **ValueSpecs** (Adresse, Name,
   Einheit, Faktor, Typ) einsammeln = der „Katalog" der verfügbaren Werte.
3. Zyklisch je interessante Adresse `cmdGetValue` (0x30) → `sword value`, `real = value/factor`.
4. `cmdGetState` (0x51) für Betriebsart/Zustand; `cmdGetErrorFirst/Next` für Störungen.

---

## B) COM2 — ASCII-Datenstrom („InfoWin"-Display-Protokoll) — EINFACHE Alternative

`service.h:600-652` (`InterfaceDef2`). Das ist **kein** Modbus (der kam erst ~2014) und **kein**
Binärprotokoll, sondern ein **ASCII-Stream**, den die Steuerung auf COM2 herausschiebt — genau
das, was das InfoWin-Raumdisplay liest. **Nur lesen, aber trivial zu parsen** (kein CRC, kein
Stuffing, kein Request):
- Paketstart `seqStart = 0x24` (`$`), Ende `seqEnd = 0x1A` (`^Z`), Feldtrenner `;`.
- Felder je Parameter: `name;value;index;div;unit` (`ParamPart`), `sizeMaxPacket=2048`.
- Struct **Parameter**: `int index; char name[29]; double value; char text[19]; char unit[4];`
- Bekannte Indizes (`ParameterIndex`, `service.h:626-643`): `1`=State, `2`=Kesseltemp,
  `3`=Abgastemp, `20`=Puffer oben, `21`=Puffer unten, `22`=Pufferpumpe, `24`=HK1, `25`=HK2,
  `26`=HK1-Pumpe, `27`=HK2-Pumpe, `28`=Außentemp, `99`=Fehler.

**Relevanz:** Für reines Monitoring (unser Ziel: Werte → Web/CSV/MQTT) ist COM2 **drastisch
einfacher** als das COM1-Binärprotokoll — man liest nur Zeilen zwischen `$` und `^Z` und splittet
an `;`. **Aber:** COM2 wird evtl. vom echten InfoWin-Display belegt, und man bekommt nur die
Anzeige-Werte (kein Parameter-Schreiben, keine freie Adressauswahl wie bei COM1). An der Anlage
zu prüfen: welcher COM-Port ist frei, und gibt COM2 diesen Stream aus?

---

## Port-Entscheidung (2026-08-14)

**Der genutzte Port ist COM1** (extern an der Gehäuserückseite erreichbar) → **Weg A dieses
Dokuments = das native Service-Protokoll**. COM2/ASCII (Abschnitt B) ist damit **vom Tisch**.
Konsequenzen: Request/Response (Kessel sendet nichts unaufgefordert → **aktiv pollen**, kein
passives Lauschen); „read-only" = nur Lese-Kommandos; Verkabelung braucht **TX und RX** über
den MAX3232 (ESP-TX→Kessel-RX, Kessel-TX→ESP-RX, GND), TX/RX-Kreuzung beim Bring-up empirisch;
nur **ein** Master auf COM1 (altes p4d muss aus sein).

## Offene Detailpunkte (beim Umsetzen aus dem Code holen)
- Exaktes Byte-Layout/Reihenfolge beim Parsen von `ValueSpec`/`MenuItem`/`Status` (Textfelder
  mit Längenpräfix?) — im jeweiligen `p4io.c`-Getter nachlesen (`getValueSpec` ab `:1060`,
  `getMenuItem` ab `:1157`, `getStatus` ab `:474`, `getValue` ab `:866`).
- `factor`-Anwendung pro Werttyp (Division vs. Digits) — `ConfigParameter::toNice/setValue`.
