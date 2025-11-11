#include <Wire.h>
#include <LiquidCrystal.h>
//
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

void display_AccessoConcesso() {
    digitalWrite(LED_VERDE_PIN, HIGH);
    digitalWrite(LED_ROSSO_PIN, LOW);
    mylcd.clear();
    mylcd.setCursor(0, 0);
    mylcd.print("Accesso");
    mylcd.setCursor(0, 1);
    mylcd.print("Consentito");
}

void display_AccessoNegato() {
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
    mylcd.print("Inserisci PIN:");
    mylcd.setCursor(0, 1);
    mylcd.print(pinInserito);
    // Aggiungi indicazioni per l'utente
    for (int i = pinInserito.length(); i < 4; i++) {
        mylcd.print("_");
    }
    mylcd.print(" A=OK B=Canc");
}

void display_PINErrato() {
    digitalWrite(LED_VERDE_PIN, LOW);
    digitalWrite(LED_ROSSO_PIN, HIGH);
    mylcd.clear();
    mylcd.setCursor(0, 0);
    mylcd.print("PIN ERRATO");
    mylcd.setCursor(0, 1);
    mylcd.print("Riprova...");
    delay(1000); // Aspetta 2 secondi
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
        comando.trim();

        if (comando.startsWith("ACCESSO_CONCESSO")) {// Pin valido
            stato = ACCESSO_CONCESSO;
            display_AccessoConcesso();
        } else if (comando.startsWith("ACCESSO_NEGATO")) { // Pin errato ma carta valida
            // Mostra errore PIN e torna all'inserimento
            display_PINErrato();
        } else if (comando.startsWith("CARTA_NON_VALIDA")) { // Carta non registrata
            stato = ACCESSO_NEGATO;
            display_AccessoNegato();
        } else if (comando.startsWith("CARTA_VALIDA")) { //Carta registrata
            // Carta valida: procedi con l'inserimento del PIN
            digitalWrite(LED_VERDE_PIN, HIGH);
            digitalWrite(LED_ROSSO_PIN, LOW);
            stato = INSERIMENTO_PIN;
            pinInserito = "";
            display_InserimentoPIN();
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
                        // Nuova carta inserita - invia al server per verifica
                        lastCardID = cardID;
                        cartaPresente = true;

                        // Invia l'ID della carta al server
                        Serial.println(PREFIX_CARD + cardID);
                    }
                }

                // --- Gestione tastierino ---
                else if (riga.startsWith("K:")) {
                    char key = riga[2];

                    if (stato == INSERIMENTO_PIN && cartaPresente) {
                        if (key >= '0' && key <= '9') {
                            // Inserimento numeri (solo se non abbiamo già 4 cifre)
                            if (pinInserito.length() < 4) {
                                pinInserito += key;
                                display_InserimentoPIN();
                            }
                        }
                        else if (key == 'A') {
                            // Tasto A: Conferma PIN
                            if (pinInserito.length() == 4) {
                                // PIN COMPLETO: Invio a Python con prefisso
                                Serial.println(PREFIX_PIN + pinInserito);
                            } else {
                                // PIN non completo, puoi mostrare un messaggio di errore
                                mylcd.clear();
                                mylcd.setCursor(0, 0);
                                mylcd.print("PIN troppo corto");
                                mylcd.setCursor(0, 1);
                                mylcd.print("Inserire 4 cifre");
                                delay(2000);
                                display_InserimentoPIN();
                            }
                        }
                        else if (key == 'B') {
                            // Tasto B: Cancella ultimo carattere
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