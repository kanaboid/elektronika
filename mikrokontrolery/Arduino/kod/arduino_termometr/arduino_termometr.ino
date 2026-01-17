#include <OneWire.h>
#include <DallasTemperature.h>
#include <LiquidCrystal.h>

const byte oneWireBus = 11;                                  // Tutaj jest określony port magistrali 1-wire.
OneWire oneWire(oneWireBus);                                 // Tutaj definiuje się port magistrali dla biblioteki.
DallasTemperature sensors(&oneWire);                         // Teraz można już przekazać bibliotece obsługi termometru namiary na bibliotekę obsługi magistrali.

const byte lcdRS = 2;                                        // Tutaj są określone poszczególne adresy linii wyświetlacza.
const byte lcdEN = 3;
const byte lcdD4 = 4;
const byte lcdD5 = 5;
const byte lcdD6 = 6;
const byte lcdD7 = 7;
const byte lcdKolumny = 16;                                  // To jest ilość kolumn w naszym wyświetlaczu.
const byte lcdWiersze = 2;                                   // To jest ilość wierszy w naszym wyświetlaczu.
LiquidCrystal lcd(lcdRS, lcdEN, lcdD4, lcdD5, lcdD6, lcdD7); // Tutaj dostarcza się bibliotece obsługi wyświetlacza wiedzy, gdzie co jest podłączone.

int temperatura;                                             // Tutaj przechowuje się wartość temperatury odczytaną z termometru, wymnożoną przez 10 i zaokrągloną.

void setup()
{
  sensors.begin();                                           // Inicjuj termometr.
  lcd.begin(lcdKolumny, lcdWiersze);                         // Inicjuj wyświetlacz, powiadamiając go o ilości kolumn i wierszy.
}

void loop()
{
  lcd.home();                                                // Ustaw kursor na początku.
  sensors.requestTemperatures();                             // Wyślij rozkaz zmierzenia temperatury.
  temperatura = 10 * (sensors.getTempCByIndex(0) + 0.05);    // Przekształć wartość odczytaną w całkowitą (dokładność: jedno miejsce po przecinku)

  if (temperatura < 0)                                       // Gdy wartość jest ujemna...
  {
    temperatura--;                                           // odejmij jeden ze względu na zaokrąglenia liczb ujemnych.
    lcd.print(F("-"));                                       // rysuj znak minusa.
  }
  else
  {
    lcd.print(F(" "));                                       // W przeciwnym razie rysuj spację.
  }

  temperatura = abs(temperatura);                            // Wyciągnij wartość bezwzględną.

  if (temperatura < 100)                                     // Jeśli temperatura będzie większa od 9 bądź mniejsza od -9 stopni...
  {
    lcd.print(F(" "));                                       // rysuj dodatkową spację.
  }

  lcd.print(float(temperatura) / 10, 1);                     // Zamień wartość całkowitą w zmiennoprzecinkową i rysuj z dokładnością do jednego miejsca po przecinku.
  lcd.print((char)223);                                      // Rysuj znak stopni
  lcd.print(F("C"));                                         // Rysuj znak C

  delay(500);                                                // Pętla opóźniająca mająca na celu wyeliminowanie niestabilności odczytów.

}
