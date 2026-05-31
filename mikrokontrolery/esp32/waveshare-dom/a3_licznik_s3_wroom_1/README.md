# Audi A3 8L — emulator licznika (ESP32-S3)

Sterowanie licznikiem **Audi A3 8L (Jaeger)** na biurku: prędkość, podświetlenie tarczy, zegar na wskazówce, WiFi/NTP.

| Element | Wersja / model |
|---------|----------------|
| Licznik | Jaeger **09053310091** (VAG ~8L0 920 900 N/H), **half FIS**, LED, **bez szarego** gniazda T32 |
| Płytka | **Waveshare ESP32-S3-DEV-KIT-N8R8** (ESP32-S3-WROOM-1-N8R8) |
| Zasilanie licznika | KORAD **12 V** (2–3 A) |
| Interfejs | **2× TLP621/P621** (opto) |
| Framework | **ESP-IDF v6.0** |

---

## Szybki start

```bash
cd a3_licznik_s3_wroom_1
idf.py menuconfig    # WiFi: A3 Cluster — siec i czas → SSID + haslo
idf.py build flash monitor
```

- **Konsola:** UART0 (CH343), **115200** 8N1 — port COM w PuTTY (Local echo **Off**).
- **Nie** przełączać konsoli na USB-JTAG — brak logów CLI.

Po starze pojawi się prompt `>`.

---

## Okablowanie (ESP32 ↔ licznik)

```
KORAD +12 V ──┬── pin 1  (Kl.15, niebieski T32)
              ├── pin 23 (Kl.30, niebieski T32)
              └── pin 17 (długie światła — opcjonalnie, kontrolka)

KORAD GND ────┬── pin 7, 9, 24 (masa, niebieski T32)
              ├── GND ESP32
              └── emiter opto (pin 3) obu TLP621

+12 V ──[4,7 kΩ]── pin 28 (pull-up prędkości)

GPIO5 ──[330 Ω]── anoda opto #1 ── katoda opto → GND
                  kolektor opto #1 → pin 28 (prędkość)

GPIO4 ──[330 Ω]── anoda opto #2 ── katoda opto → GND
                  kolektor opto #2 → pin 30 ZIELONEGO T32 (58DE, dim)
                  + kondensator 10 µF: pin 30 (+) → GND (−)
```

**Masa wspólna:** GND ESP32 = GND KORAD = pin 7 licznika.

### Ważne — czego NIE podłączać

| Pin | Dlaczego |
|-----|----------|
| **Niebieski pin 15** (58d) | **Wyjście** licznika (~2 V PWM) — nie dim |
| **Niebieski pin 20** (58s) | **Wyjście** analogowe licznika |
| **+12 V na pin 30** | Wejście ADC — tylko GND przez opto + filtr RC |
| **Pin 11** (obroty) | U Ciebie **nie działa** — obroty idą przez **CAN** |

---

## Mapowanie GPIO (Waveshare ESP32-S3)

| GPIO ESP32 | Funkcja w projekcie | Połączenie |
|------------|---------------------|------------|
| **GPIO4** | Dim tarczy (PWM 100 Hz) | Opto → **zielony T32 pin 30** (58DE) |
| **GPIO5** | Prędkość (esp_timer, ułamkowe Hz) | Opto → **niebieski T32 pin 28** |
| **GPIO38** | NeoPixel status (WS2812) | Na płytce Waveshare |
| **UART0** | Konsola CLI | CH343, 115200 |

Inne GPIO wolne. GPIO4/5 na Waveshare to linie kamery I2C — w tym projekcie używane jako wyjścia.

---

## Pinout licznika — niebieski T32 (32 pin)

Widok od **gniazda** (styki), numery **fabryczne** na obudowie.

| Pin | Klema / funkcja | Uwagi |
|-----|-----------------|-------|
| **1** | Kl.15 (+12 V zapłon) | Wymagane |
| **7, 9, 24** | Kl.31 (masa) | Wspólna GND |
| **11** | Sygnał obrotów | U Ciebie **tylko CAN** — brak efektu |
| **15** | 58d | **Wyjście** cyfrowe licznika — nie dim |
| **17** | Długie światła | +12 V (kontrolka) |
| **20** | 58s | **Wyjście** analogowe licznika |
| **23** | Kl.30 (+12 V stałe) | Wymagane |
| **26** | Światło pozycyjne prawe | +12 V → kontrolka mijania |
| **27** | Światło pozycyjne lewe | j.w. |
| **28** | Wejście prędkości (tachimetro) | GPIO5 przez opto |
| **30** | — | Na **zielonym** T32 → **58DE** (dim) |

### Zielony T32 (T32a) — half FIS

| Pin | Funkcja |
|-----|---------|
| **22, 23** | CAN comfort (H/L) — ewentualnie obroty / FIS w przyszłości |
| **30** | **58DE** — wejście dimmera (pokrętło w aucie). **GND = jasność MAX** |

---

## Podświetlenie tarczy (58DE, pin 30 zielony)

### Jak działa w aucie

Pin 30 to **wejście analogowe ADC** z wewnętrznym pull-up (~3–4 kΩ → ~5 V). Pokrętło dimmera to potencjometr pin 30 → GND.

| Napięcie pin 30 | Efekt |
|-----------------|-------|
| **~4,8 V** (otwarty) | Podświetlenie **OFF** |
| **> ~3 V** | Cluster **ignoruje** (próg detekcji) |
| **< ~3 V** | Aktywny dim — im niższe V, tym jaśniej |
| **0 V** (GND) | **MAX** jasność |

Potwierdzone testem potencjometrem 10 kΩ: przy ~7 kΩ V spada poniżej 3 V i tarcza zaczyna się rozjaśniać.

### Emulator (ESP + opto + 10 µF)

- **GPIO4** → opto → pin 30: opto ON = pin na GND, OFF = licznik trzyma ~4,8 V.
- **Kondensator 10 µF** pin 30 → GND — filtr RC (wymagany, inaczej migotanie).
- PWM **100 Hz** (LEDC) + tabela LUT w `main.c` (`s_dim_lut[]`).

| `dim` (CLI) | Rzeczywisty duty | ~Jasność tarczy |
|-------------|------------------|-----------------|
| 0 | 0% | OFF |
| 1 | 9% | ~60% (pierwsze światło) |
| 50 | 25% | ~83% |
| 100 | 100% | MAX |

**Ograniczenie:** bez rezystora szeregowego 2,2–4,7 kΩ w linii opto brak płynnego zakresu 0–60% jasności (próg ~3 V + asymetria ładowania/rozładowania kondensatora).

Testy: `dim test on` (MAX), `dim test off` (MIN), `dim sweep`.

---

## Prędkościomierz (pin 28)

- Sygnał impulsowy przez **esp_timer** (ułamkowe Hz, nie LEDC).
- Kalibracja: **tabela 18 punktów** (5–250 km/h) + interpolacja liniowa w `main.c` → `s_speed_cal[]`.
- **Martwa strefa** wskazówki: ~**40 km/h** — poniżej brak ruchu mimo sygnału.

Przykłady kalibracji (z pomiarów ręcznych `hz`):

| km/h | Hz |
|------|-----|
| 100 | 96,0 |
| 200 | 186,5 |
| 250 | 231,5 |

Nowe punkty dopisuj w `s_speed_cal[]` (rosnąco po `kmh`).

---

## Zegar na wskazówce (`clock`)

Wymaga czasu (NTP przez WiFi lub `time HH:MM:SS`).

| Tryb | Czas | km/h na liczniku |
|------|------|------------------|
| **Godzina** (sekundy 5–59) | np. 17:36 | `(17 + 36/60) × 10` ≈ **176** |
| **Minuta** (pierwsze 5 s po :00) | :36:00–04 | **36** |

- Godzina: **10 km/h = 1 h** (17:00 → 170 km/h).
- W godzinie płynna interpolacja minut (17:59 → ~180 km/h).
- Minuta pokazywana **5 s** po każdej pełnej minucie.

---

## Obroty

Pin **11** (niebieski) — **brak efektu** (prawdopodobnie tylko **CAN**, piny **18/19** zielonego T32). Komenda `rpm` wyłączona w firmware. Kolejny krok: emulacja CAN 500 kbit/s.

---

## Komendy CLI

| Komenda | Opis |
|---------|------|
| `speed <km/h>` | Prędkość z tabeli kalibracji (pin 28) |
| `<liczba>` | Skrót do `speed` |
| `hz <Hz>` | Ręczne Hz na pin 28 (test) |
| `dim <0-100>` | Jasność tarczy (pin 30 zielony) |
| `dim` | Stan dim + mapowanie LUT |
| `dim test on` | MAX (GND na pin 30) |
| `dim test off` | MIN (puszczenie) |
| `dim sweep` | 0→100 co 3 s |
| `clock` | Zegar 24h na wskazówce |
| `time HH:MM:SS` | Ręczny zegar |
| `wifi` | Stan WiFi / NTP |
| `demo` | Sweep 0→250→0 km/h |
| `stop` | Wskazówki na 0 |
| `help` | Pomoc |

---

## WiFi i NTP

Menuconfig: **A3 Cluster — siec i czas**

- `A3_WIFI_SSID`, `A3_WIFI_PASSWORD`
- `A3_NTP_SERVER` (domyślnie `pool.ntp.org`)
- Strefa: **CET/CEST** (Polska)

**Uwaga:** nie commituj `sdkconfig` z hasłem WiFi do publicznego repo.

---

## Struktura projektu

```
a3_licznik_s3_wroom_1/
├── main/
│   ├── main.c              # CLI, kalibracja, speed, clock, dim
│   ├── wifi_time.c/h       # WiFi STA + SNTP
│   ├── status_led.c/h      # NeoPixel GPIO38
│   ├── board_flash.c/h     # Flash 8 MB + SPIFFS /storage
│   ├── led_strip_encoder.c # RMT WS2812
│   └── Kconfig.projbuild   # WiFi/NTP menuconfig
├── partitions_8mb.csv      # factory 4M + SPIFFS ~4M
├── sdkconfig.defaults
└── README.md               # ten plik
```

---

## Zasilanie licznika na biurku (minimum)

| Pin niebieski | Połączenie |
|---------------|------------|
| 1, 23 | +12 V |
| 7, 9, 24 | GND |
| 17 | +12 V (kontrolki / długie) |
| 28 | GPIO5 przez opto + pull-up 4,7 kΩ |
| 30 zielony | GPIO4 przez opto + 10 µF do GND |

Sweep LCD/FIS i wskazówki działają po Kl.15+30+masa.

---

## Znane ograniczenia

1. **Obroty** — wymagają CAN (nie pin 11).
2. **Prędkość < ~40 km/h** — wskazówka może stać.
3. **Dim tarczy** — bez R szeregowego w opto tylko zakres ~60–100% jasności (patrz sekcja podświetlenie).
4. **Niebieski pin 15/20** — wyjścia licznika, nie podłączać opto.
5. **Szare gniazdo T32** — brak w tej wersji licznika.

---

## Kolejne kroki (TODO)

- [ ] Emulacja **CAN** (obroty, ewentualnie FIS/backlight)
- [ ] Rezystor **2,2 kΩ** szeregowo opto → pin 30 (pełny zakres dim)
- [ ] Zapis kalibracji prędkości w **SPIFFS**
- [ ] Automatyczny dim w `clock` (np. niższy w nocy)

---

## Przydatne linki

- [Waveshare ESP32-S3-DEV-KIT-N8R8](https://docs.waveshare.com/ESP32-S3-DEV-KIT-N8R8)
- Regulacja podświetlenia VAG (58DE vs 58d): [a4-klub.pl](https://a4-klub.pl/topic/488241-regulacja-pod%C5%9Bwietlenia-licznika-a6c5/)

---

*Ostatnia aktualizacja dokumentacji: maj 2026 — stan po kalibracji prędkości, clock, WiFi/NTP i dim na pin 30 (58DE).*
