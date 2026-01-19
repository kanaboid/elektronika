const byte woltomierz = A0;                                  // Port, który będzie mierzyć napięcie.
int napiecie;                                                // Wartość tego napięcia bez jednostek.
float napiecieV;                                             // Wartość tego napięcia w woltach.

void setup()
{
  Serial.begin(9600);                                        // Inicjuj przesyłanie danych portem szeregowym.
}

void loop()
{
  napiecie = analogRead(woltomierz);                         // Pobierz wartość napięcia z portu woltomierza.
  napiecieV = napiecie * (5.0 / 1024.0);                     // Przelicz dziesięciobitową wartość na wolty.
  Serial.println(napiecieV);                                 // Wyświetl napięcie.
}
