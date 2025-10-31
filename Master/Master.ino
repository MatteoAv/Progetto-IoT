#include <Wire.h>
#include <LiquidCrystal.h>
#define LED_VERDE_PIN 8
#define LED_ROSSO_PIN 9

LiquidCrystal mylcd(12,11,5,4,3,2);

String pinInserito = "";
String currentCard = "";
String lastCardID = "";    // 🟢 nuova variabile per ricordare l’ultima card letta
bool cartaPresente = false;

const String ID_AUTORIZZATO = "E27AEB1B";
const String PIN_CORRETTO = "1234";

enum Stato {ATTESA, INSERIMENTO_PIN, ACCESSO_CONCESSO, ACCESSO_NEGATO};
Stato stato = ATTESA;

unsigned long lastRequest = 0;
const unsigned long REQUEST_INTERVAL = 200; // 

void setup() {
  Wire.begin();             
  mylcd.begin(16,2);
  Serial.begin(9600);
  
  pinMode(LED_VERDE_PIN, OUTPUT);
  pinMode(LED_ROSSO_PIN, OUTPUT);
  digitalWrite(LED_VERDE_PIN, LOW);
  digitalWrite(LED_ROSSO_PIN, LOW);

  mylcd.clear();
  mylcd.setCursor(0,0);
  mylcd.print("Inserire carta");
}

void loop() {
  unsigned long now = millis();
  if (now - lastRequest >= REQUEST_INTERVAL) {
    lastRequest = now;

    Wire.requestFrom(8,32);
    if (Wire.available()) {
      String riga = "";
      while (Wire.available()) {
        char c = Wire.read();
        if (c == '\n') break;
        riga += c;
      }

      if (riga.length() > 0) {
        Serial.println("Ricevuto: " + riga);

        // --- Gestione RFID ---
        if (riga.startsWith("C:")) {
          String cardID = riga.substring(2);

          if (cardID == "REMOVED") {
            cartaPresente = false;
            lastCardID = "";   // reset
            digitalWrite(LED_VERDE_PIN, LOW);
            digitalWrite(LED_ROSSO_PIN, LOW);
            stato = ATTESA;
            mylcd.clear();
            mylcd.setCursor(0,0);
            mylcd.print("Inserire carta");
            pinInserito = "";
          }
          else if (cardID != lastCardID) {   // 🟢 solo se cambia card
            lastCardID = cardID;             // aggiorna ID corrente
            cartaPresente = true;
            currentCard = cardID;

            if (cardID == ID_AUTORIZZATO) {
              digitalWrite(LED_VERDE_PIN, HIGH);
              digitalWrite(LED_ROSSO_PIN, LOW);

              stato = INSERIMENTO_PIN;
              pinInserito = "";
              mylcd.clear();
              mylcd.setCursor(0,0);
              mylcd.print("Inserisci PIN:");
              mylcd.setCursor(0,1);
              mylcd.print("                ");
            }
            else {
              digitalWrite(LED_VERDE_PIN, LOW);
              digitalWrite(LED_ROSSO_PIN, HIGH);

              stato = ACCESSO_NEGATO;
              mylcd.clear();
              mylcd.setCursor(0,0);
              mylcd.print("Accesso negato");
              mylcd.setCursor(0,1);
              mylcd.print("                ");
            }
          }
        }

        // --- Gestione tastierino ---
        else if (riga.startsWith("K:")) {
          char key = riga[2];

          if (stato == INSERIMENTO_PIN && cartaPresente) {
            if (key >= '0' && key <= '9') {
              pinInserito += key;
              mylcd.setCursor(0,1);
              mylcd.print(pinInserito);
              if (pinInserito.length() == 4) {
                if (pinInserito == PIN_CORRETTO) {
                  stato = ACCESSO_CONCESSO;
                  digitalWrite(LED_VERDE_PIN, HIGH);
                  digitalWrite(LED_ROSSO_PIN, LOW);
                  mylcd.clear();
                  mylcd.setCursor(0,0);
                  mylcd.print("Benvenuto");
                  mylcd.setCursor(0,1);
                  mylcd.print("Gianluca");
                } else {
                  stato = ACCESSO_NEGATO;
                  digitalWrite(LED_VERDE_PIN, LOW);
                  digitalWrite(LED_ROSSO_PIN, HIGH);
                  mylcd.clear();
                  mylcd.setCursor(0,0);
                  mylcd.print("PIN Errato");
                  mylcd.setCursor(0,1);
                  mylcd.print("Accesso negato");
                }
              }
            }
          } else {
            Serial.println("Tasto ignorato (no carta o non INSERIMENTO_PIN)");
          }
        }
      }
    }
  }

}
