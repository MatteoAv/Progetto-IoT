#include <Wire.h>
#include <LiquidCrystal.h>

#define LED_VERDE_PIN 8
#define LED_ROSSO_PIN 9
#define PREFIX_PIN "PIN_INSERITO:" // Prefisso per l'invio del PIN a Python
#define PREFIX_CARD "CARD_ID:"     // Prefisso per l'invio dell'ID carta a Python

LiquidCrystal mylcd(12, 11, 5, 4, 3, 2);

String pinInserito = "";
String lastCardID = "";
bool cartaPresente = false;

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

void display_AccessoConcesso(String nome, String cognome, String saldo) {
    digitalWrite(LED_VERDE_PIN, HIGH);
    digitalWrite(LED_ROSSO_PIN, LOW);
    mylcd.clear();
    mylcd.setCursor(0, 0);
    mylcd.print(nome + " " + cognome);
    mylcd.setCursor(0, 1);
    mylcd.print(saldo);  // Mostra il nome dell'utente
}

void display_AccessoNegato(String nomeUtente = "") {
    digitalWrite(LED_VERDE_PIN, LOW);
    digitalWrite(LED_ROSSO_PIN, HIGH);
    mylcd.clear();
    mylcd.setCursor(0, 0);
    mylcd.print("Accesso");
    mylcd.setCursor(0, 1);
    mylcd.print("Negato");
}

void display_InserimentoPIN() {
    mylcd.clear();
    mylcd.setCursor(0, 0);
    mylcd.print("PIN: ");
    
    mylcd.print(pinInserito);
    // Aggiungi underscore per cifre mancanti
    for (int i = pinInserito.length(); i < 6; i++) {
        mylcd.print("_");
    }
    
    mylcd.setCursor(0, 1);
    mylcd.print(" A=OK    B=Canc");
}

void display_PINErrato() {
    digitalWrite(LED_VERDE_PIN, LOW);
    digitalWrite(LED_ROSSO_PIN, HIGH);
    mylcd.clear();
    mylcd.setCursor(0, 0);
    mylcd.print("PIN ERRATO");
    mylcd.setCursor(0, 1);
    mylcd.print("Riprova...");
    delay(1000);

    // Torna all'inserimento PIN
    digitalWrite(LED_VERDE_PIN, HIGH);
    digitalWrite(LED_ROSSO_PIN, LOW);
    stato = INSERIMENTO_PIN;
    pinInserito = "";
    display_InserimentoPIN();
}

// --- Setup ---

void setup() {
    Wire.begin();
    mylcd.begin(16, 2);
    Serial.begin(9600); // Comunicazione con Python

    pinMode(LED_VERDE_PIN, OUTPUT);
    pinMode(LED_ROSSO_PIN, OUTPUT);

    display_Attesa();
}

// --- Loop principale ---

void loop() {
    unsigned long now = millis();

    // 1. Gestione comandi da Python
    if (Serial.available() > 0) {
        String comando = Serial.readStringUntil('\n');
        comando.trim();

        if (comando.startsWith("ACCESSO_CONCESSO")) {
            stato = ACCESSO_CONCESSO;

            // Estraggo la parte dopo "ACCESSO_CONCESSO:"
            int separatorIndex = comando.indexOf(':');
            String datiUtente = (separatorIndex != -1) ? comando.substring(separatorIndex + 1) : "";

            // Divido la stringa usando '|' come separatore
            int p1 = datiUtente.indexOf('|');
            int p2 = datiUtente.indexOf('|', p1 + 1);

            String nome = (p1 != -1) ? datiUtente.substring(0, p1) : "";
            String cognome = (p1 != -1 && p2 != -1) ? datiUtente.substring(p1 + 1, p2) : "";
            String saldo = (p2 != -1) ? datiUtente.substring(p2 + 1) : "";

            // Chiamo la funzione diplay_AccessoConcesso
           display_AccessoConcesso(nome, cognome, saldo);

        } else if (comando.startsWith("ACCESSO_NEGATO")) {
            int separatorIndex = comando.indexOf(':');
            String nomeUtente = (separatorIndex != -1) ? comando.substring(separatorIndex + 1) : "";
            display_PINErrato();

        } else if (comando.startsWith("CARTA_NON_VALIDA")) {
            stato = ACCESSO_NEGATO;
            display_AccessoNegato();

        } else if (comando.startsWith("CARTA_VALIDA")) {
            digitalWrite(LED_VERDE_PIN, HIGH);
            digitalWrite(LED_ROSSO_PIN, LOW);
            stato = INSERIMENTO_PIN;
            pinInserito = "";
            display_InserimentoPIN();
        }
    }

    // 2. Gestione dati dal sensore (I2C)
    if (now - lastRequest >= REQUEST_INTERVAL) {
        lastRequest = now;

        Wire.requestFrom(8, 32); // Richiesta dati dall'Arduino Slave
        if (Wire.available()) {
            String riga = "";
            while (Wire.available()) {
                char c = Wire.read();
                if (c == '\n') break;
                riga += c;
            }

            if (riga.length() > 0) {
                // --- RFID ---
                if (riga.startsWith("C:")) {
                    String cardID = riga.substring(2);

                    if (cardID == "REMOVED") {
                        cartaPresente = false;
                        lastCardID = "";
                        stato = ATTESA;
                        display_Attesa();
                        pinInserito = "";
                    } else if (cardID != lastCardID) {
                        lastCardID = cardID;
                        cartaPresente = true;
                        Serial.println(PREFIX_CARD + cardID); // invio a Python
                    }
                }

                // --- Tastierino ---
                else if (riga.startsWith("K:")) {
                    char key = riga[2];

                    if (stato == INSERIMENTO_PIN && cartaPresente) {
                        if (key >= '0' && key <= '9') {
                            if (pinInserito.length() < 6) {
                                pinInserito += key;
                                display_InserimentoPIN();
                            }
                        } else if (key == 'A') {
                            if (pinInserito.length() == 6) {
                                Serial.println(PREFIX_PIN + pinInserito);
                            } else {
                                mylcd.clear();
                                mylcd.setCursor(0, 0);
                                mylcd.print("PIN troppo corto");
                                mylcd.setCursor(0, 1);
                                mylcd.print("Inserire 6 cifre");
                                delay(2000);
                                display_InserimentoPIN();
                            }
                        } else if (key == 'B') {
                            if (pinInserito.length() > 0) {
                                pinInserito.remove(pinInserito.length() - 1);
                                display_InserimentoPIN();
                            }
                        }
                    }
                }
            }
        }
    }
}
