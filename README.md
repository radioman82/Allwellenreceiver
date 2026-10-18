# Allwellenreceiver (SI4735 + Arduino Micro + LCD 20x4)

Ein kompakter, leistungsfähiger DIY-Allwellenempfänger für **UKW (FM), Mittelwelle (MW), Kurzwelle (SW) und Langwelle (LW)** inklusive **SSB (USB/LSB)** und **CW**, aufgebaut mit einem **Arduino Micro (ATmega32U4)**, dem **Silicon Labs SI4735 / SI4732** Radio-IC und einem klassischen **20x4 LCD-Display**, untergebracht in einem maßgeschneiderten **3D-Druckgehäuse**.

Das Projekt basiert auf Konzepten der SI4735-Amateurfunk-Community (u. a. [IU4ALH](https://github.com/IU4ALH/IU4ALH) und der SI4735-Bibliothek von [PU2CLR](https://github.com/pu2clr/SI4735)), wurde jedoch speziell für den kompakten **Arduino Micro**, ein 4-Bit paralleles **20x4 LCD** und optimierte Firmware mit EEPROM-Speicherung, schnellem Menü und BFO-Feinabstimmung angepasst.

---

## Impressionen & Gehäuse (3D-Druck)

<p align="center">
  <img src="enclosure/vorderansicht.jpg" alt="Frontansicht Allwellenempfänger" width="48%">
  <img src="enclosure/Betrieb.jpg" alt="Allwellenempfänger im Betrieb" width="48%">
</p>

<p align="center">
  <img src="enclosure/Innenleben.jpg" alt="Innenleben und Verkabelung" width="48%">
  <img src="enclosure/antennenanschluss.jpg" alt="Rückseite mit Antennenanschluss" width="48%">
</p>

Die druckfertige 3D-Modelldatei (STL) für das Gehäuse befindet sich im Ordner [`enclosure/`](enclosure/):
* **3D-Druckdatei:** [`enclosure/radio_enclosure.stl`](enclosure/radio_enclosure.stl)

---

## Features

- **Empfangsbereiche:**
  - **UKW (FM):** 87.0 MHz – 108.0 MHz (mit 50 µs De-Emphasis, Stereo-Blend & optimierten Soft-Mute-Schwellen)
  - **Langwelle (LW):** ab 150 kHz
  - **Mittelwelle (MW):** MW1, MW2, MW3 (bis 3500 kHz)
  - **Kurzwelle (SW):** Durchgehende Bandsegmente SW1 bis SW9, sowie durchgehender Bereich (ALL 150 kHz – 30 MHz)
  - **Amateurfunkbänder:** 80m (LSB), 40m (LSB), 20m (USB), 15m (USB), 10m (USB)
  - **CB-Funk:** 11m (AM)
- **Modulationsarten:** AM, LSB, USB, CW
- **SSB Patch-Support:** Lädt das SI4735 SSB-Patch für echten Einseitenband- und Morsesignal-Empfang.
- **BFO-Feinabstimmung:** Schnelle und präzise BFO-Einstellung (25-Hz-Schritte) zur perfekten Demodulation von SSB/CW.
- **Bandbreitenfilter:** Umschaltbare Filterbandbreiten für AM und SSB zur Unterdrückung von Nachbarkanalstörungen.
- **Automatische / Manuelle Verstärkungsregelung (AGC & Attenuation):** Startet mit maximaler Empfindlichkeit, manuell regelbar.
- **EEPROM-Speicher:**
  - Letzte Frequenz, Betriebsart, Schrittweite und Bandbreite werden pro Band automatisch im EEPROM gespeichert und beim Einschalten wiederhergestellt.
- **Benutzeroberfläche:**
  - 20x4 Zeichen LCD mit Signalstärkeanzeige (S-Meter / RSSI).
  - Drehimpulsgeber (Rotary Encoder) mit integriertem Taster für Frequenzabstimmung und Menünavigation.
  - Individueller Startbildschirm (Fuchs-Grafik).

---

## Hardware & Pinbelegung

### Verwendete Komponenten
1. **Mikrocontroller:** Arduino Micro (ATmega32U4, 5V / 16 MHz)
2. **Empfänger-Modul:** Silicon Labs SI4735 (oder SI4732 mit I2C-Ansteuerung)
3. **Display:** HD44780-kompatibles LCD-Modul 20x4 (4-Bit-Modus)
4. **Bedienung:** Rotary Encoder mit Push-Button
5. **Gehäuse:** 3D-Druckgehäuse ([`enclosure/radio_enclosure.stl`](enclosure/radio_enclosure.stl))

### Pinout (Arduino Micro)

| Funktion / Komponente | Arduino Micro Pin | Bemerkung |
| :--- | :--- | :--- |
| **Rotary Encoder Pin A** | Pin 0 (RX) | Interrupt-fähig |
| **Rotary Encoder Pin B** | Pin 1 (TX) | Interrupt-fähig |
| **Rotary Push Button** | Pin 15 | Menüauswahl / Bestätigung |
| **SI4735 RESET** | Pin 10 | Hardware-Reset für Radio-IC |
| **SI4735 SDA** | Pin 2 | I2C Datenleitung |
| **SI4735 SCL** | Pin 3 | I2C Taktleitung |
| **LCD RS** | Pin 14 (MISO) | Register Select |
| **LCD Enable (E)** | Pin 16 (MOSI) | Enable |
| **LCD D4** | Pin 4 | Datenleitung Bit 4 |
| **LCD D5** | Pin 5 | Datenleitung Bit 5 |
| **LCD D6** | Pin 6 | Datenleitung Bit 6 |
| **LCD D7** | Pin 7 | Datenleitung Bit 7 |

---

## Installation & Kompilierung

1. **Arduino IDE vorbereiten:**
   - Installiere die aktuelle Arduino IDE.
   - Wähle als Board: **Arduino Micro** (bzw. Arduino Leonardo).
2. **Benötigte Bibliotheken:**
   - [PU2CLR SI4735](https://github.com/pu2clr/SI4735) (über den Arduino Bibliotheksverwalter installierbar)
   - `LiquidCrystal` (in Arduino integriert)
   - `EEPROM` (in Arduino AVR Core integriert)
3. **Sketch öffnen & hochladen:**
   - Öffne die Datei [`radio/radio.ino`](radio/radio.ino) in der Arduino IDE.
   - Verbinde den Arduino Micro per USB und lade den Sketch hoch.

---

## Danksagung & Credits

- Besonderer Dank an **Ricardo Caratti (PU2CLR)** für die herausragende [SI4735 Arduino Library](https://github.com/pu2clr/SI4735).
- Dank an **IU4ALH** für seine Schaltungsbeispiele und Inspirationen rund um SI4735-Projekte.

---

## Lizenz

Dieses Projekt steht unter der [MIT License](LICENSE).
3D-Druck- und Gehäusedateien stehen unter [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/).
