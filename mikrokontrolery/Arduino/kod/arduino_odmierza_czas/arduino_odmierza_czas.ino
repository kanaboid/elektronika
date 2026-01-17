int PstryczekPierwszy = 8;                                   // Używamy trzech pstryczków do wywoływania komunikatów.
int PstryczekDrugi = 9;
int PstryczekTrzeci = 10;
int lcdRS = 2;                                               // "lcd" to oczywiście wyświetlacz, którego wyprowadzenia ponazywaliśmy tutaj.
int lcdEN = 3;
int lcdD4 = 4;
int lcdD5 = 5;
int lcdD6 = 6;
int lcdD7 = 7;
int lcdKolumny = 16;                                         // "Kolumny" to ilość kolumn na naszym wyświetlaczu,
int lcdWiersze = 2;                                          // a "Wiersze" to ilość wierszy.

#include <LiquidCrystal.h>                                   // To jest biblioteka obsługi wyświetlacza, którą należy zaimportować poleceniem #include.
#include <TimeLib.h>

LiquidCrystal lcd(lcdRS, lcdEN, lcdD4, lcdD5, lcdD6, lcdD7); // Tutaj dostarcza się tej bibliotece wiedzy, gdzie co jest podłączone.

void setup()                                                 // Tu zaczyna się program. Ta część wykona się jednorazowo.
{
  lcd.begin(lcdKolumny, lcdWiersze);                         // Inicjujemy wyświetlacz, powiadamiając go o ilości kolumn i wierszy.
  lcd.setCursor(0, 1);                                       // Ustawiamy kursor na samym początku drugiego wiersza wyświetlacza.
  lcd.print("  WC komputer   ");                             // Wysyłamy do wyświetlacza tekst "WC komputer".
  pinMode(PstryczekPierwszy, INPUT_PULLUP);                  // Deklarujemy linie pstryczków jako wejścia podciągnięte wewnętrznie do wysokiego stanu.
  pinMode(PstryczekDrugi, INPUT_PULLUP);
  pinMode(PstryczekTrzeci, INPUT_PULLUP);
}

void loop()                                                  // Tutaj zaczyna się część programu wykonująca się bez końca.
{
  if (digitalRead(PstryczekPierwszy) == LOW) {               // Sprawdzamy czy wciśnięto pstryczek pierwszy.
    wyzeruj();                                               // Ustawiamy kursor i zerujemuy zegar.
    lcd.print("    Zajete!     ");                           // i wysyłamy do niego tekst "Zajęte".
  }
  while (digitalRead(PstryczekPierwszy) == LOW) {            // Czekaj, dopóki przycisk nie zostanie puszczony.
    sekundnik();                                             // Rysujemy sekundnik.
  }
  if (digitalRead(PstryczekDrugi) == LOW) {                  // To samo robimy z pstryczkiem drugim...
    wyzeruj();
    lcd.print(" Jeszcze moment ");
  }
  while (digitalRead(PstryczekDrugi) == LOW) {
    sekundnik();
  }
  if (digitalRead(PstryczekTrzeci) == LOW) {                 // oraz z trzecim.
    wyzeruj();
    lcd.print("Ciezka sprawa...");
  }
  while (digitalRead(PstryczekTrzeci) == LOW) {
    sekundnik();
  }
  wyzeruj();
  lcd.print("    Wolne :)    ");                             // A tutaj lądujemy, gdy żaden z pstryczków nie jest wciśnięty.
  while (digitalRead(PstryczekPierwszy) == HIGH && digitalRead(PstryczekDrugi) == HIGH && digitalRead(PstryczekTrzeci) == HIGH) {
    sekundnik();
  }
}

void sekundnik()                                             // Tu się rysuje sekundnik.
{
  lcd.setCursor(0, 1);                                       // Ustawiamy kursor na samym początku drugiego wiersza wyświetlacza.
  lcd.print(" Juz ");                                        // Napiszemy "Już...
  lcd.print(now());                                          // Wyciągniemy ilość sekund od zresetowania timera
  lcd.print(" sekund   ");                                   // I dopiszemy "... sekund".
}

void wyzeruj()                                               // A tu się ustawia kursor i zeruje zegar.
{
  lcd.home();                                                // Ustawiamy kursor na początku wyświetlacza.
  setTime (0);                                               // Zerujemy czas.
}
