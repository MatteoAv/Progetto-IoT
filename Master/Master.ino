#include <Wire.h>
#include <LiquidCrystal.h>

#define LED_VERDE_PIN 8
#define LED_ROSSO_PIN 9
#define PREFIX_PIN "PIN_INSERITO:" // Prefisso per l'invio del PIN a Python

LiquidCrystal mylcd(12, 11, 5, 4, 3, 2);

String pinInserito = "";
String currentCard = "";
String lastCardID = "";    
bool cartaPresente = false;

const String ID_AUTORIZZATO = "E27AEB1B"; 

enum Stato { ATTESA, INSERIMENTO_PIN, ACCESSO_CONCESSO, ACCESSO_NEGATO };
Stato stato = ATTESA;

unsigned long lastRequest = 0;
const unsigned long REQUEST_INTERVAL = 200; // Intervallo per la richiesta I2C

// --- Funzioni di visualizzazione ---

void display_Attesa() {
    mylcd.clear();
    mylcd.setCursor(0, 0);
    mylcd.print("Inserire carta");
    digitalWrite(LED_VERDE_PIN, LOW);
    digitalWrite(LED_ROSSO_PIN, LOW);
}

void display_AccessoConcesso() {
    digitalWrite(LED_VERDE_PIN, HIGH);
    digitalWrite(LED_ROSSO_PIN, LOW);
    mylcd.clear();
    mylcd.setCursor(0, 0);
    mylcd.print("Benvenuto");
    mylcd.setCursor(0, 1);
    mylcd.print("Accesso Concesso");
}

void display_AccessoNegato() {
    digitalWrite(LED_VERDE_PIN, LOW);
    digitalWrite(LED_ROSSO_PIN, HIGH);
    mylcd.clear();
    mylcd.setCursor(0, 0);
    mylcd.print("PIN Errato/Negato");
    mylcd.setCursor(0, 1);
    mylcd.print("Accesso Negato");
}

// --- Setup ---

void setup() {
    Wire.begin();             
    mylcd.begin(16, 2);
    Serial.begin(9600); // Inizializzazione Seriale per comunicazione con Python
    
    pinMode(LED_VERDE_PIN, OUTPUT);
    pinMode(LED_ROSSO_PIN, OUTPUT);

    display_Attesa();
}

// --- Loop ---

void loop() {
    unsigned long now = millis();

    // 1. GESTIONE RICEZIONE COMANDI DA PYTHON (Seriale)
    if (Serial.available() > 0) {
        String comando = Serial.readStringUntil('\n');

        if (comando.startsWith("ACCESSO_CONCESSO")) {
            stato = ACCESSO_CONCESSO;
            display_AccessoConcesso();
            // NON STAMPARE QUI! Altrimenti Python riceve un nuovo dato e si confonde.
        } else if (comando.startsWith("ACCESSO_NEGATO")) {
            stato = ACCESSO_NEGATO;
            display_AccessoNegato();
            // NON STAMPARE QUI!
        }
    }

    // 2. GESTIONE DATI DAL SENSORE (I2C)
    if (now - lastRequest >= REQUEST_INTERVAL) {
        lastRequest = now;

        Wire.requestFrom(8, 32); // Richiesta dati dall'Arduino Slave (indirizzo 8)
        if (Wire.available()) {
            String riga = "";
            while (Wire.available()) {
                char c = Wire.read();
                if (c == '\n') break;
                riga += c;
            }

            if (riga.length() > 0) {
                // --- Gestione RFID ---
                if (riga.startsWith("C:")) {
                    String cardID = riga.substring(2);

                    if (cardID == "REMOVED") {
                        // Carta rimossa
                        cartaPresente = false;
                        lastCardID = "";
                        stato = ATTESA;
                        display_Attesa();
                        pinInserito = "";
                    } else if (cardID != lastCardID) {
                        // Nuova carta inserita
                        lastCardID = cardID;
                        cartaPresente = true;

                        if (cardID == ID_AUTORIZZATO) {
                            // Carta autorizzata: richiedi PIN
                            digitalWrite(LED_VERDE_PIN, HIGH);
                            digitalWrite(LED_ROSSO_PIN, LOW);

                            stato = INSERIMENTO_PIN;
                            pinInserito = "";
                            mylcd.clear();
                            mylcd.setCursor(0, 0);
                            mylcd.print("Inserisci PIN:");
                            mylcd.setCursor(0, 1);
                            mylcd.print("                ");
                        } else {
                            // Carta NON autorizzata: accesso negato
                            stato = ACCESSO_NEGATO;
                            display_AccessoNegato();
                        }
                    }
                }

                // --- Gestione tastierino ---
                else if (riga.startsWith("K:")) {
                    char key = riga[2];

                    if (stato == INSERIMENTO_PIN && cartaPresente) {
                        if (key >= '0' && key <= '9') {
                            pinInserito += key;
                            mylcd.setCursor(0, 1);
                            mylcd.print(pinInserito);

                            if (pinInserito.length() == 4) {
                                // PIN COMPLETO: Invio a Python con prefisso
                                Serial.println(PREFIX_PIN + pinInserito);
                            }
                        }
                    }
                }
            }
        }
    }
}