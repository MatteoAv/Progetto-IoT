#include <Wire.h>
#include <LiquidCrystal.h>

LiquidCrystal mylcd(12,11,5,4,3,2);

String lastID = "";
String pinInserito = "";

const String ID_AUTORIZZATO = "E27AEB1B";
const String PIN_CORRETTO = "1234";

enum Stato {ATTESA, INSERIMENTO_PIN, ACCESSO_CONCESSO, ACCESSO_NEGATO};
Stato stato = ATTESA;

// Variabili per polling lento
unsigned long lastRequest = 0;
const unsigned long REQUEST_INTERVAL = 500; // ms

void setup() {
  Wire.begin();             // master I2C
  mylcd.begin(16,2);
  mylcd.setCursor(0,0);
  mylcd.print("Inserire");
  mylcd.setCursor(0,1);
  mylcd.print("                ");
  Serial.begin(9600);
}

void loop() {
  unsigned long now = millis();
  if (now - lastRequest >= REQUEST_INTERVAL) {
    lastRequest = now;

    Wire.requestFrom(8,32); // richiede dati allo slave
    if (Wire.available()) {
      String riga = "";
      while (Wire.available()) {
        char c = Wire.read();
        if (c == '\n') break; // fine stringa
        riga += c;
      }

      if (riga.length() > 0) { // ignora stringhe vuote
        Serial.println("Ricevuto: " + riga);

        if (riga.startsWith("C:")) { // card ID
          String cardID = riga.substring(2);
          if (cardID == ID_AUTORIZZATO) {
            stato = INSERIMENTO_PIN;
            pinInserito = "";
            mylcd.clear();
            mylcd.setCursor(0,0);
            mylcd.print("Inserisci PIN:");
            mylcd.setCursor(0,1);
            mylcd.print("                ");
          } else {
            stato = ACCESSO_NEGATO;
            mylcd.clear();
            mylcd.setCursor(0,0);
            mylcd.print("Accesso negato");
            mylcd.setCursor(0,1);
            mylcd.print("                ");
          }
        }
        else if (riga.startsWith("K:")) { // tastierino
          if (stato == INSERIMENTO_PIN) {
            char key = riga[2];
            if (key >= '0' && key <= '9') {
              pinInserito += key;
              mylcd.setCursor(0,1);
              mylcd.print(pinInserito);
              if (pinInserito.length() == 4) {
                if (pinInserito == PIN_CORRETTO) {
                  stato = ACCESSO_CONCESSO;
                  mylcd.clear();
                  mylcd.setCursor(0,0);
                  mylcd.print("Benvenuto");
                  mylcd.setCursor(0,1);
                  mylcd.print("Gianluca");
                } else {
                  stato = ACCESSO_NEGATO;
                  mylcd.clear();
                  mylcd.setCursor(0,0);
                  mylcd.print("PIN Errato");
                  mylcd.setCursor(0,1);
                  mylcd.print("Accesso negato");
                }
              }
            }
          }
        }
      }
    }
  }

  // reset automatico
  if (stato == ACCESSO_CONCESSO || stato == ACCESSO_NEGATO) {
    delay(2000);
    stato = ATTESA;
    mylcd.clear();
    mylcd.setCursor(0,0);
    mylcd.print("Inserire carta");
    mylcd.setCursor(0,1);
    mylcd.print("                ");
  }
}
