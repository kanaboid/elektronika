# Wskazówki projektowe - PCB z 8 ekspanderami MCP23017

## 📋 Przegląd projektu
- **8 ekspanderów MCP23017** (adresy 0x20-0x27)
- **128 GPIO** (16 pinów × 8 ekspanderów)
- **Gniazda śrubowe** do podłączenia przekaźników i krańcówek
- **Komunikacja I2C** z Arduino

---

## 🔌 1. WZORCE ADRESOWANIA I2C

### Konfiguracja pinów adresowych (A2, A1, A0):
```
MCP1: A2=GND, A1=GND, A0=GND → 0x20
MCP2: A2=GND, A1=GND, A0=VCC → 0x21
MCP3: A2=GND, A1=VCC, A0=GND → 0x22
MCP4: A2=GND, A1=VCC, A0=VCC → 0x23
MCP5: A2=VCC, A1=GND, A0=GND → 0x24
MCP6: A2=VCC, A1=GND, A0=VCC → 0x25
MCP7: A2=VCC, A1=VCC, A0=GND → 0x26
MCP8: A2=VCC, A1=VCC, A0=VCC → 0x27
```

### ⚠️ WAŻNE dla PCB:
- **Każdy MCP musi mieć unikalną kombinację A2/A1/A0**
- Użyj rezystorów pull-up/pull-down (10kΩ) do stabilnego adresowania
- Rozważ użycie **DIP switch** lub **jumperów** do łatwej zmiany adresów podczas debugowania

---

## 🔌 2. POŁĄCZENIA I2C

### Magistrala I2C (wspólna dla wszystkich):
```
SDA → Wszystkie MCP (pin 12)
SCL → Wszystkie MCP (pin 13)
VCC → Wszystkie MCP (pin 9)
GND → Wszystkie MCP (pin 10, 18)
```

### Rezystory pull-up I2C:
- **4.7 kΩ** na SDA (do VCC)
- **4.7 kΩ** na SCL (do VCC)
- **JEDEN zestaw rezystorów** na całej magistrali (nie na każdym MCP!)
- Umieść rezystory **blisko Arduino/mastera**, nie przy MCP

### ⚠️ UWAGA - Pojemność szyny:
- 8 urządzeń + długie ścieżki = zwiększona pojemność
- Dla stabilnej pracy przy 400 kHz: użyj **2.2 kΩ** zamiast 4.7 kΩ
- Lub obniż szybkość do **100 kHz** dla większej tolerancji

---

## 🔌 3. ZASILANIE

### Wymagania:
- **MCP23017**: 2.7V - 5.5V (typ. 3.3V lub 5V)
- **Prąd**: ~1-2 mA na chip (bez obciążenia GPIO)
- **Prąd GPIO**: max 25 mA na pin, max 125 mA na port (A/B), max 150 mA całkowity

### Zalecenia:
- **Kondensatory odsprzęgające**: 
  - **100 nF ceramiczny** przy każdym MCP (VCC-GND)
  - **10 µF tantalowy** co 2-3 MCP
- **Separacja zasilania**: Rozważ osobne ścieżki zasilania dla:
  - Logiki (MCP + Arduino)
  - Przekaźników (osobne zasilanie 12V/24V)
- **Dioda Schottky'ego** na wejściu zasilania (ochrona przed odwrotną polaryzacją)

---

## 🔌 4. WYPROWADZENIA GPIO DO GNIAZD ŚRUBOWYCH

### Organizacja pinów:
Każdy MCP23017 ma **16 GPIO**:
- **Port A**: GPA0-GPA7 (piny 21-28)
- **Port B**: GPB0-GPB7 (piny 1-8)

### Zalecenia dla gniazd śrubowych:
1. **Oznaczenia na PCB**:
   - MCP1_GP0, MCP1_GP1, ..., MCP1_GP15
   - MCP2_GP0, MCP2_GP1, ..., MCP2_GP15
   - itd.

2. **Typ gniazd**:
   - **5.08 mm pitch** (standard przemysłowy)
   - **2-pinowe** dla każdego GPIO (pin + GND)
   - Lub **wielopinowe** (np. 16-pinowe) z oznaczeniami

3. **Ochrona GPIO**:
   - **Rezystory szeregowe** 100-220Ω (ochrona przed zwarciem)
   - **Dioda TVS** (Transient Voltage Suppressor) dla linii długich
   - **Dioda Schottky'ego** (ochrona przed ujemnym napięciem)

4. **GND dla każdego gniazda**:
   - Każde gniazdo powinno mieć dostęp do GND
   - Użyj **płaszczyzny GND** na PCB dla lepszej masy

---

## 🔌 5. PRZEKAŹNIKI

### Sterowanie przekaźnikami z GPIO:
- **Przekaźniki wymagają prądu**: 5-20 mA (zależnie od typu)
- **MCP23017 może dostarczyć**: max 25 mA na pin

### Rozwiązania:

#### Opcja A: Bezpośrednie sterowanie (małe przekaźniki):
```
GPIO → Rezystor 220Ω → Baza tranzystora NPN (2N2222/BC547)
Emiter → GND
Kolektor → Cewka przekaźnika → +12V/+24V
Dioda flyback: Katoda do +12V, Anoda do kolektora
```

#### Opcja B: Bufor (dla większych przekaźników):
```
GPIO → ULN2803 (8-kanałowy bufor Darlington)
ULN2803 → Przekaźniki
```

#### Opcja C: Moduły przekaźnikowe z optoizolacją:
- Gotowe moduły (np. 8-kanałowe)
- GPIO → Moduł → Przekaźnik
- **Zaleta**: Izolacja galwaniczna

### ⚠️ WAŻNE:
- **Dioda flyback** (1N4007) przy każdej cewce przekaźnika
- **Osobne zasilanie** dla przekaźników (nie z MCP!)
- **Filtrowanie zasilania** przekaźników (kondensatory)

---

## 🔌 6. KRAŃCÓWKI (Krańcówki mechaniczne)

### Podłączenie krańcówek:
- Krańcówki to **przyciski NO (Normal Open)** lub **NC (Normal Closed)**
- Podłączone między **GPIO a GND** (lub VCC, zależnie od logiki)

### Konfiguracja w kodzie:
```cpp
mcp.pinMode(pinKrancowka, INPUT_PULLUP);  // Pull-up wewnętrzny
// Odczyt: LOW = aktywna, HIGH = nieaktywna
```

### Zalecenia:
- **Rezystor pull-up zewnętrzny** (10kΩ) jako backup
- **Kondensator 100nF** równolegle (odporność na zakłócenia)
- **Dioda TVS** dla długich linii

---

## 🔌 7. LAYOUT PCB - DOBRE PRAKTYKI

### Routing I2C:
- **SDA i SCL** jako **pary różnicowe** (blisko siebie)
- **Szerokość ścieżek**: min 0.3 mm (dla prądów I2C wystarczy)
- **Długość ścieżek**: jak najkrótsze, unikaj długich pętli
- **Unikaj** przechodzenia pod komponentami wysokiej częstotliwości

### Płaszczyzny:
- **Płaszczyzna GND** na całej PCB (warstwa dolna lub środkowa)
- **Płaszczyzna VCC** dla zasilania (opcjonalnie)
- **Via stitching** co 5-10 mm dla lepszej masy

### Separacja:
- **Sekcja analogowa/cyfrowa**: oddziel logikę od przekaźników
- **Sekcja zasilania**: osobna dla MCP i przekaźników

### Komponenty:
- Umieść **kondensatory odsprzęgające** jak najbliżej pinów VCC/GND MCP
- **Rezystory pull-up I2C** blisko Arduino/mastera
- **Gniazda śrubowe** na krawędzi PCB (łatwy dostęp)

---

## 🔌 8. TESTOWANIE I DEBUGOWANIE

### Testowanie adresów:
```cpp
// Skrypt do skanowania I2C
#include <Wire.h>
void setup() {
  Wire.begin();
  Serial.begin(9600);
  for (byte addr = 0x08; addr < 0x78; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print("Znaleziono urządzenie: 0x");
      Serial.println(addr, HEX);
    }
  }
}
```

### Testowanie GPIO:
- **LED testowe** na każdym GPIO (opcjonalnie, przez rezystor 220Ω)
- **Multimetr** do sprawdzenia stanów
- **Oscyloskop** do sprawdzenia sygnałów I2C (jeśli problemy)

### Debugowanie:
- **Testpointy** na SDA, SCL, VCC, GND
- **LED status** dla każdego MCP (power indicator)
- **DIP switch** do zmiany adresów podczas testów

---

## 🔌 9. LISTA CZĘŚCI (BOM)

### Dla każdego MCP23017:
- 1× MCP23017 (SOIC-28 lub DIP-28)
- 1× Kondensator 100 nF ceramiczny (X7R, 50V)
- 3× Rezystory 10 kΩ (pull-up/pull-down dla A2/A1/A0)

### Wspólne:
- 2× Rezystory 2.2-4.7 kΩ (pull-up I2C)
- 1× Kondensator 10 µF tantalowy (zasilanie)
- 1× Dioda Schottky'ego (ochrona zasilania)

### Dla GPIO (opcjonalnie):
- 128× Rezystory 100-220Ω (ochrona GPIO)
- 128× Dioda TVS (dla długich linii)

### Dla przekaźników:
- 128× Tranzystor NPN (2N2222/BC547) lub ULN2803
- 128× Dioda flyback (1N4007)
- Moduły przekaźnikowe (zależnie od wyboru)

---

## 🔌 10. TYPOWE PROBLEMY I ROZWIĄZANIA

### Problem: MCP nie odpowiada
- ✅ Sprawdź adresy A2/A1/A0
- ✅ Sprawdź rezystory pull-up I2C
- ✅ Sprawdź zasilanie (VCC, GND)
- ✅ Sprawdź połączenia SDA/SCL

### Problem: Niestabilna komunikacja
- ✅ Obniż szybkość I2C do 100 kHz
- ✅ Zmniejsz rezystory pull-up (2.2 kΩ)
- ✅ Sprawdź długość ścieżek I2C
- ✅ Dodaj kondensatory odsprzęgające

### Problem: GPIO nie działa
- ✅ Sprawdź konfigurację pinMode (INPUT/OUTPUT)
- ✅ Sprawdź czy pin nie jest uszkodzony
- ✅ Sprawdź obciążenie (przekaźnik może być za duży)

### Problem: Zakłócenia od przekaźników
- ✅ Oddziel zasilanie przekaźników
- ✅ Użyj optoizolacji
- ✅ Dodaj filtry na liniach GPIO
- ✅ Popraw płaszczyznę GND

---

## 📝 PODSUMOWANIE - CHECKLISTA PCB

- [ ] 8× MCP23017 z unikalnymi adresami (0x20-0x27)
- [ ] Rezystory pull-up I2C (2.2-4.7 kΩ) blisko mastera
- [ ] Kondensatory odsprzęgające przy każdym MCP (100 nF)
- [ ] Płaszczyzna GND na całej PCB
- [ ] Gniazda śrubowe z oznaczeniami (MCPx_GPx)
- [ ] Ochrona GPIO (rezystory, diody TVS)
- [ ] Separacja zasilania (logika vs. przekaźniki)
- [ ] Testpointy do debugowania
- [ ] Długie ścieżki I2C jak najkrótsze
- [ ] Dioda flyback przy każdym przekaźniku

---

**Powodzenia z projektem! 🚀**
