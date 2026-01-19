
#include <LiquidCrystal.h>

const byte woltomierz = A0;
const byte lcdRS = 2;                                        // Tutaj są określone poszczególne adresy linii wyświetlacza.
const byte lcdEN = 3;
const byte lcdD4 = 4;
const byte lcdD5 = 5;
const byte lcdD6 = 6;
const byte lcdD7 = 7;
const byte lcdKolumny = 16;                                  // To jest ilość kolumn w naszym wyświetlaczu.
const byte lcdWiersze = 2;                                   // To jest ilość wierszy w naszym wyświetlaczu.
LiquidCrystal lcd(lcdRS, lcdEN, lcdD4, lcdD5, lcdD6, lcdD7); // Tutaj dostarcza się bibliotece obsługi wyświetlacza wiedzy, gdzie co jest podłączone.

                                             // Tutaj przechowuje się wartość temperatury odczytaną z termometru, wymnożoną przez 10 i zaokrągloną.

void setup()
{
                                             // Inicjuj termometr.
  lcd.begin(lcdKolumny, lcdWiersze);                         // Inicjuj wyświetlacz, powiadamiając go o ilości kolumn i wierszy.
  Serial.begin(9600);
  
}

void loop()
{
                                                  // Pętla opóźniająca mająca na celu wyeliminowanie niestabilności odczytów.
int napiecie = analogRead(woltomierz);
float napiecieV = napiecie * (5.0 / 1024.0);
lcd.print("V: " + String(napiecieV));
Serial.println(napiecieV);
delay(1000);
lcd.clear();
}
