#include <Wire.h>
#include <Adafruit_MCP23X17.h> 

Adafruit_MCP23X17 mcp1; // Adres 0x20
Adafruit_MCP23X17 mcp2; // Adres 0x21
Adafruit_MCP23X17 mcp3; // Adres 0x22
Adafruit_MCP23X17 mcp4; // Adres 0x23
Adafruit_MCP23X17 mcp5; // Adres 0x24
Adafruit_MCP23X17 mcp6; // Adres 0x25
Adafruit_MCP23X17 mcp7; // Adres 0x26
Adafruit_MCP23X17 mcp8; // Adres 0x27


// --- UNIWERSALNA KLASA STEROWNIKA ---
class SterownikZaworu {
  private:
    Adafruit_MCP23X17* _mcp; // Wskaźnik (może być pusty!)
    byte pinPrzekaznik; 
    byte pinPrzycisk;   
    byte pinKrancowka;  
    
    enum Stan { SPOCZYNEK, OTWIERANIE, ZAKONCZONO, BLAD };
    Stan aktualnyStan = SPOCZYNEK;
    unsigned long czasStartuRuchu = 0;
    const unsigned long maxCzasRuchu = 10000;

  public:
    // Konstruktor
    // Jeśli sterujemy z Arduino, jako pierwszy argument podamy 'nullptr'
    SterownikZaworu(Adafruit_MCP23X17* mcp, byte przekaznik, byte przycisk, byte krancowka) {
      _mcp = mcp; 
      pinPrzekaznik = przekaznik;
      pinPrzycisk = przycisk;
      pinKrancowka = krancowka;
    }

    void begin() {
      if (_mcp != nullptr) {
        // --- Tryb EKSPANDER (MCP) ---
        _mcp->pinMode(pinPrzekaznik, OUTPUT);
        _mcp->pinMode(pinPrzycisk, INPUT_PULLUP);
        _mcp->pinMode(pinKrancowka, INPUT_PULLUP);
        _mcp->digitalWrite(pinPrzekaznik, LOW);
      } else {
        // --- Tryb ARDUINO (Native) ---
        pinMode(pinPrzekaznik, OUTPUT);
        pinMode(pinPrzycisk, INPUT_PULLUP);
        pinMode(pinKrancowka, INPUT_PULLUP);
        digitalWrite(pinPrzekaznik, LOW);
      }
    }

    void update() {
      // Pomocnicze zmienne do odczytu stanu (niezależnie skąd pochodzą)
      bool przyciskWcisniety = false;
      bool krancowkaAktywna = false;

      // 1. ODCZYT STANU (Abstrakcja sprzętowa)
      if (_mcp != nullptr) {
        // Czytamy z MCP
        przyciskWcisniety = (_mcp->digitalRead(pinPrzycisk) == LOW);
        krancowkaAktywna = (_mcp->digitalRead(pinKrancowka) == LOW);
      } else {
        // Czytamy z Arduino
        przyciskWcisniety = (digitalRead(pinPrzycisk) == LOW);
        krancowkaAktywna = (digitalRead(pinKrancowka) == LOW);
      }

      // 2. LOGIKA (Taka sama dla obu przypadków!)
      switch (aktualnyStan) {
        case SPOCZYNEK:
          if (przyciskWcisniety) { 
             if (krancowkaAktywna) {
                // Już otwarty
             } else {
                aktualnyStan = OTWIERANIE;
                czasStartuRuchu = millis();
                
                // STEROWANIE WYJŚCIEM
                if (_mcp != nullptr) _mcp->digitalWrite(pinPrzekaznik, HIGH);
                else digitalWrite(pinPrzekaznik, HIGH);
                
                Serial.println("Startuje zawor...");
             }
             delay(50); 
          }
          break;

        case OTWIERANIE:
          if (krancowkaAktywna) {
            // STOP
            if (_mcp != nullptr) _mcp->digitalWrite(pinPrzekaznik, LOW);
            else digitalWrite(pinPrzekaznik, LOW);
            
            aktualnyStan = ZAKONCZONO;
            Serial.println("Sukces!");
          }
          
          if (millis() - czasStartuRuchu > maxCzasRuchu) {
            // STOP AWARYJNY
            if (_mcp != nullptr) _mcp->digitalWrite(pinPrzekaznik, LOW);
            else digitalWrite(pinPrzekaznik, LOW);
            
            aktualnyStan = BLAD;
            Serial.println("Blad czasu!");
          }
          break;

        case ZAKONCZONO:
          if (millis() - czasStartuRuchu > 1000) aktualnyStan = SPOCZYNEK;
          break;

        case BLAD:
          if (przyciskWcisniety) {
            aktualnyStan = SPOCZYNEK;
            delay(500);
          }
          break;
      }
    }
};

// --- DEFINICJA ZAWORÓW ---

// 1. Zawór podpięty do MCP (Adresowanie I2C)
SterownikZaworu zaworMCP(&mcp1, 0, 1, 2);
SterownikZaworu zaworMCP2(&mcp2, 0, 1, 2);
SterownikZaworu zaworMCP7(&mcp2, 0, 1, 2);  

// 2. Zawór podpięty BEZPOŚREDNIO do Arduino (Piny cyfrowe 2, 3, 4)
// Podajemy 'nullptr' (lub NULL) jako pierwszy argument
SterownikZaworu zaworArduino(nullptr, 2, 3, 4); 

void setup() {
  Serial.begin(9600);
  
  // Inicjalizacja MCP
  if (!mcp1.begin_I2C(0x20)) {
    Serial.println("Blad MCP1 (0x20)!");
    while(1);
  }
  if (!mcp2.begin_I2C(0x21)) {
    Serial.println("Blad MCP2 (0x21)!");
    while(1);
  }
  if (!mcp3.begin_I2C(0x22)) {
    Serial.println("Blad MCP3 (0x22)!");
    while(1);
  }
  if (!mcp4.begin_I2C(0x23)) {
    Serial.println("Blad MCP4 (0x23)!");
    while(1);
  }
  if (!mcp5.begin_I2C(0x24)) {
    Serial.println("Blad MCP5 (0x24)!");
    while(1);
  }
  if (!mcp6.begin_I2C(0x25)) {
    Serial.println("Blad MCP6 (0x25)!");
    while(1);
  }
  if (!mcp7.begin_I2C(0x26)) {
    Serial.println("Blad MCP7 (0x26)!");
    while(1);
  }
  if (!mcp8.begin_I2C(0x27)) {
    Serial.println("Blad MCP8 (0x27)!");
    while(1);
  }
  // Uruchomienie zaworów
  zaworMCP.begin();
  zaworMCP2.begin();
  zaworMCP7.begin();     // Skonfiguruje piny na chipie
  zaworArduino.begin(); // Skonfiguruje piny na Arduino Mega
  
  Serial.println("System Hybrydowy Gotowy.");
}

void loop() {
  zaworMCP.update();
  zaworMCP2.update();
  zaworArduino.update();
  delay(10);
}