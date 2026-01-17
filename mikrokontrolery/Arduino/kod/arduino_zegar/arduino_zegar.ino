#include<LiquidCrystal.h>                               // To jest biblioteka obsługi wyświetlacza.
#include<TimeLib.h>                                     // To jest biblioteka obsługi zegara.

const byte LcdRS=2;                                     // Tutaj są określone poszczególne adresy linii wyświetlacza.
const byte LcdEN=3;
const byte LcdD4=4;
const byte LcdD5=5;
const byte LcdD6=6;
const byte LcdD7=7;
const byte LcdKolumny=16;                               // To jest ilość kolumn w naszym wyświetlaczu.
const byte LcdWiersze=2;                                // To jest ilość wierszy w naszym wyświetlaczu.

const byte PstryczekMinus=8;                            // Adres pstryczka zmniejszającego wartości.
const byte PstryczekZmien=9;                            // Adres pstryczka ustawień.
const byte PstryczekPlus=10;                            // Adres pstryczka zwiększającego wartości.

const byte OpoznienieDlugie=250;                        // Czas opóźnienia klawiatury.
const byte OpoznienieKrotkie=25;                        // Czas autorepetycji klawiatury.

byte Godzina;                                           // Zmienna godzin - dla ich ustawień i wyświelania.
byte Minuta;                                            // Jak wyżej, tylko dla minut.
byte Sekunda;                                           // Jak wyżej, tylko dla sekund.

LiquidCrystal lcd(LcdRS,LcdEN,LcdD4,LcdD5,LcdD6,LcdD7); // Tutaj dostarcza się bibliotece obsługi wyświetlacza wiedzy, gdzie co jest podłączone.

void setup(){
lcd.begin(LcdKolumny,LcdWiersze);                       // Inicjujemy wyświetlacz, powiadamiając go o ilości kolumn i wierszy.

pinMode(PstryczekMinus,INPUT_PULLUP);                   // Deklarujemy linie pstryczków jako wejścia podciągnięte wewnętrznie do wysokiego stanu.
pinMode(PstryczekPlus,INPUT_PULLUP);
pinMode(PstryczekZmien,INPUT_PULLUP);
}

void loop(){
WyswietlanieCzasu:

while(digitalRead(PstryczekZmien)==HIGH){               // Powtarzamy poniższe działania, dopóki nie zostanie wciśnięty PstryczekZmien
time_t CzasOdczytany=now();                             // Zapisujemy bieżący czas do zmiennej CzasOdczytany
Sekunda=second(CzasOdczytany);                          // Nadajemy zmiennej Sekundy wartość sekund.
Minuta=minute(CzasOdczytany);                           // Jak wyżej, tylko dla minut.
Godzina=hour(CzasOdczytany);                            // Jak wyżej, tylko dla godzin.
WyswietlCzas();                                         // Wyświetlamy godzinę, dwukropek i minuty...
WyswietlSekundy();                                      // oraz dwukropek i sekundy.
}
                                                        // Wciśnięto przycisk ustawień więc...
lcd.clear();                                            // czyścimy wyświetlacz i ustawiamy kursor na początku,
WyswietlCzas();                                         // wyświetlamy ostani aktualny czas (bez sekund)...
delay(OpoznienieDlugie);                                // wprowadzamy opóźnienie dla stabilnych odczytów pstryczka...
while(digitalRead(PstryczekZmien)==LOW){}               // i czekamy aż zostanie puszczony.

UstawianieCzasu:                                        // Tu zaczyna się pętla ustawiania czasu.

if(digitalRead(PstryczekPlus)==LOW){                    // Jeśli PstryczekPlus jest wciśnięty...
ZwiekszCzas();                                          // zwiększ wartość czasu,
WyswietlCzas();                                         // wyświetl go na ekranie,
delay(OpoznienieDlugie);                                // wprowadź opóźnienie...
}
while(digitalRead(PstryczekPlus)==LOW){                 // i powtarzaj tę procedurę, tylko szybciej.
ZwiekszCzas();
WyswietlCzas();
delay(OpoznienieKrotkie);
}    

if(digitalRead(PstryczekMinus)==LOW){                   // Jeśli PstryczekMinus jest wciśnięty...
ZmniejszCzas();                                         // zmniejsz wartość czasu,
WyswietlCzas();                                         // wyświetl go na ekranie,
delay(OpoznienieDlugie);                                // wprowadź opóźnienie...
}
while(digitalRead(PstryczekMinus)==LOW){                // i powtarzaj tę procedurę, tylko szybciej.
ZmniejszCzas();
WyswietlCzas();
delay(OpoznienieKrotkie);
}    

if(digitalRead(PstryczekZmien)==HIGH){                  // Jeśli PstryczekZmień jest puszczony...
goto UstawianieCzasu;}                                  // to idź do początku pętli ustawiania czasu.
else{                                                   // Jeśli jednak został naciśnięty,
setTime(Godzina,Minuta,00,01,01,2000);                  // Ustaw bieżący czas zgodnie z ustawionym,
delay(OpoznienieDlugie);                                // wprowadź opóźnienie...
goto WyswietlanieCzasu;                                 // i wyjdź z pętli ustawiania czasu.
}

}

void ZwiekszCzas(){                                     // Funkcja ta zwiększa czas o minutę.
Minuta++;                                               // Zwiększ liczbę minut.
if(Minuta==60){                                         // Jeśli osiągnięto wartość minut równą 60 to...
Minuta=0;                                               // Zeruj liczbę minut,
Godzina++;                                              // i zwiększ liczbę godzin.
if(Godzina==24){                                        // Jeśli osiągnięto wartość godzin równą 24 to...
Godzina=0;}}                                            // Zeruj liczbę godzin.
}

void ZmniejszCzas(){                                    // Funkcja ta zmniejsza czas o minutę.
Minuta--;                                               // Zmniejsz liczbę minut.
if(Minuta==255){                                        // Jeśli osiągnięto wartość minut równą 255 to...
Minuta=59;                                              // Ustaw liczbę minut równą 59,
Godzina--;                                              // i zmniejsz liczbę godzin.
if(Godzina==255){                                       // Jeśli osiągnięto wartość godzin równą 255 to...
Godzina=23;}}                                           // Ustaw liczbę godzin równą 23.
}

void WyswietlCzas(){                                    // Funkcja ta wyświetla czas na ekranie (bez sekund).
lcd.home();                                             // Ustaw kursor na początku.
if(Godzina<10){                                         // Jeśli liczba godzin jest mniejsza od 10 to...
lcd.print(F(" "));}                                     // rysuj spację.
lcd.print(Godzina);                                     // Rysuj liczbę godzin.
lcd.print(F(":"));                                      // Rysuj dwukropek.
if(Minuta<10){                                          // Jeśli liczba minut jest mniejsza od 10 to...
lcd.print(F("0"));}                                     // rysuj zero.
lcd.print(Minuta);                                      // Rysuj liczbę minut.
}

void WyswietlSekundy(){                                 // Funkcja ta wyświetla sekundy wraz z dwukropkiem.
lcd.print(F(":"));                                      // Rysuj dwukropek.
if(Sekunda<10){                                         // Jeśli liczba sekund jest mniejsza od 10 to...
lcd.print(F("0"));}                                     // rysuj zero.
lcd.print(Sekunda);                                     // Rysuj liczbę sekund.
}
