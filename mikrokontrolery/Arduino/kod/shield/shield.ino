const byte opoznienie = 100;  // Opóźnienie po wciśnięciu pstryczków.

const byte pstryczek1 = 9;  // Port klawiatury.
const byte pstryczek2 = 10;
const byte pstryczek3 = 11;
const byte pstryczek4 = 12;

const byte przekaznik1 = 4;  // Port przekaźników.
const byte przekaznik2 = 5;
const byte przekaznik3 = 6;
const byte przekaznik4 = 7;

bool przelacznik1 = false;  // Przełączniki.
bool przelacznik2 = false;
bool przelacznik3 = false;
bool przelacznik4 = false;

void setup() {
  pinMode(pstryczek1, INPUT_PULLUP);  // Deklaruj porty pstryczków jako wejścia podciągnięte wewnętrznie do wysokiego stanu.
  pinMode(pstryczek2, INPUT_PULLUP);
  pinMode(pstryczek3, INPUT_PULLUP);
  pinMode(pstryczek4, INPUT_PULLUP);

  pinMode(przekaznik1, OUTPUT);  // Deklaruj porty przekaźników jako wyjścia.
  pinMode(przekaznik2, OUTPUT);
  pinMode(przekaznik3, OUTPUT);
  pinMode(przekaznik4, OUTPUT);
}

void loop() {
  if (digitalRead(pstryczek1) == HIGH) {        // Jeśli wciśnięto pstryczek, to...
    przelacznik1 = !przelacznik1;               // Zaneguj stan przełącznika,
    digitalWrite(przekaznik1, przelacznik1);    // Wyślij na przekaźnik jego stan,
    delay(opoznienie);                          // Zaczekaj chwilę,
    while (digitalRead(pstryczek1) == HIGH) {}  // Czekaj na puszczenie pstryczka,
    delay(opoznienie);                          // Zaczekaj chwilę.
  }
  if (digitalRead(pstryczek2) == HIGH) {
    przelacznik2 = !przelacznik2;
    digitalWrite(przekaznik2, przelacznik2);
    delay(opoznienie);
    while (digitalRead(pstryczek2) == HIGH) {}
    delay(opoznienie);
  }
  if (digitalRead(pstryczek3) == HIGH) {
    przelacznik3 = !przelacznik3;
    digitalWrite(przekaznik3, przelacznik3);
    delay(opoznienie);
    while (digitalRead(pstryczek3) == HIGH) {}
    delay(opoznienie);
  }
  if (digitalRead(pstryczek4) == HIGH) {
    przelacznik4 = !przelacznik4;
    digitalWrite(przekaznik4, przelacznik4);
    delay(opoznienie);
    while (digitalRead(pstryczek4) == HIGH) {}
    delay(opoznienie);
  }
}
