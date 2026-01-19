#include <DHT.h>
#include <LiquidCrystal.h>

const byte oneWireBus = 11;                                  // Tutaj jest określony port magistrali 1-wire.
DHT dht(oneWireBus, DHT11);                                  // Tutaj dostarcza się bibliotece obsługi wilgotnościomierza wiedzy, gdzie co jest podłączone i z jakim typem mamy do czynienia.

const byte lcdRS = 2;                                        // Tutaj są określone poszczególne adresy linii wyświetlacza.
const byte lcdEN = 3;
const byte lcdD4 = 4;
const byte lcdD5 = 5;
const byte lcdD6 = 6;
const byte lcdD7 = 7;
const byte lcdKolumny = 16;                                  // To jest ilość kolumn w naszym wyświetlaczu.
const byte lcdWiersze = 2;                                   // To jest ilość wierszy w naszym wyświetlaczu.
LiquidCrystal lcd(lcdRS, lcdEN, lcdD4, lcdD5, lcdD6, lcdD7); // Tutaj dostarcza się bibliotece obsługi wyświetlacza wiedzy, gdzie co jest podłączone.

float temperatura;                                           // Tutaj przechowuje się wartość temperatury odczytaną z termometru.

void setup()
{
  dht.begin();                                               // Inicjuj wilotnościomierz.
  lcd.begin(lcdKolumny, lcdWiersze);                         // Inicjuj wyświetlacz, powiadamiając go o ilości kolumn i wierszy.
}

void loop()
{
  lcd.home();                                                // Ustaw kursor na początku.
  lcd.print(F("Wilgotnosc   "));                             // Rysuj komunikat.
  lcd.print(dht.readHumidity(), 0);                          // Wyświetl wilgotność.
  lcd.print(F("% "));                                        // Rysuj znak % i spację.

  lcd.setCursor(0, 1);                                       // Ustaw kursor w nowym wierszu.
  lcd.print(F("Cieplo    "));                                // Rysuj komunikat.
  temperatura = dht.readTemperature();                       // Odczytaj temperaturę.

  if (temperatura >= 0)                                      // Gdy wartość jest nieujemna...
  {
    lcd.print(F(" "));                                       // ...rysuj dodatkową spację.
  }

  if (abs(temperatura) < 10)                                 // Gdy wartość bezwzględna temperatury jest mniejsza od 10...
  {
    lcd.print(F(" "));                                       // ...rysuj dodatkową spację.
  }

  lcd.print(temperatura, 1);                                 // Wyświetl temperaturę.
  lcd.print((char)223);                                      // Rysuj znak stopni.

  delay(500);                                                // Pętla opóźniająca mająca na celu wyeliminowanie niestabilności odczytów.
}
