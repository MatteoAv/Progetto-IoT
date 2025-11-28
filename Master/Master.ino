#include <Wire.h> //per la comunicazione I2C
#include <LiquidCrystal.h> //per il display
#include <WiFiS3.h> //per connettersi al wifi
#include <ArduinoJson.h> //per creare e leggere JSON
#include "arduino_secrets.h" //contiene nome, password e ip

// --- Pin e LCD ---
#define LED_VERDE_PIN 8 //pin del led verde
#define LED_ROSSO_PIN 9 //pin del led rosso
LiquidCrystal mylcd(12, 11, 5, 4, 3, 2); //pin del display

// --- WiFi e server ---
char ssid[] = WIFI_SSID;
char pass[] = WIFI_PASSWORD;
char serverAddress[] = SERVER_ADDRESS; // IP del server Python
int port = 5000;
WiFiClient client;

// --- Stato e variabili ---
String pinInserito = "";
String lastCardID = "";
bool cartaPresente = false;

enum Stato { ATTESA, INSERIMENTO_PIN, ACCESSO_CONCESSO, ACCESSO_NEGATO };
Stato stato = ATTESA;

unsigned long lastRequest = 0;
const unsigned long REQUEST_INTERVAL = 200;

// --- Funzioni display ---
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
    mylcd.print(saldo);
}

void display_AccessoNegato(String msg = "") {
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
    for (int i = pinInserito.length(); i < 6; i++) mylcd.print("_");
    mylcd.setCursor(0, 1);
    mylcd.print("A=OK B=Canc");
}

// --- Setup ---
void setup() {
    Wire.begin();
    mylcd.begin(16, 2);
    pinMode(LED_VERDE_PIN, OUTPUT);
    pinMode(LED_ROSSO_PIN, OUTPUT);

    Serial.begin(115200);
    display_Attesa();

    // --- Connessione WiFi ---
    WiFi.begin(ssid, pass);
    mylcd.clear();
    mylcd.setCursor(0,0);
    mylcd.print("Connessione WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(300);
        mylcd.print(".");
    }
    mylcd.clear();
    mylcd.setCursor(0,0);
    mylcd.print("WiFi connessa!");
    delay(1000);
    display_Attesa();
}

// --- Funzione invio JSON al server ---
void inviaAlServer(String tipo, String valore) {
    if (!client.connected()) {
        client.stop();
        client.connect(serverAddress, port);
    }

    StaticJsonDocument<200> doc;
    doc["type"] = tipo;      // "card" o "pin"
    doc["value"] = valore;

    String output;
    serializeJson(doc, output);
    client.println(output);
}

// --- Loop principale ---
void loop() {
    unsigned long now = millis();

    // --- Ricezione risposta dal server ---
    if (client.connected() && client.available()) {
        String r = client.readStringUntil('\n');

        StaticJsonDocument<200> doc;
        DeserializationError error = deserializeJson(doc, r);
        if (!error) {
            String status = doc["status"];
            if (status == "CARTA_VALIDA") {
                stato = INSERIMENTO_PIN;
                pinInserito = "";
                digitalWrite(LED_VERDE_PIN, HIGH);
                digitalWrite(LED_ROSSO_PIN, LOW);
                display_InserimentoPIN();
            } else if (status == "CARTA_NON_VALIDA") {
                stato = ACCESSO_NEGATO;
                display_AccessoNegato();
            } else if (status == "ACCESSO_CONCESSO") {
                stato = ACCESSO_CONCESSO;
                String nome = doc["nome"];
                String cognome = doc["cognome"];
                String saldo = doc["saldo"];
                display_AccessoConcesso(nome, cognome, saldo);
            } else if (status == "ACCESSO_NEGATO") {
                display_AccessoNegato();
            }
        }
    }

    // --- Lettura RFID e tastierino I2C ---
    if (now - lastRequest >= REQUEST_INTERVAL) {
        lastRequest = now;

        Wire.requestFrom(8, 32);
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
                        inviaAlServer("card", cardID);
                    }
                }

                // --- Tastierino ---
                else if (riga.startsWith("K:")) {
                    char key = riga[2];
                    if (stato == INSERIMENTO_PIN && cartaPresente) {
                        if (key >= '0' && key <= '9' && pinInserito.length() < 6) {
                            pinInserito += key;
                            display_InserimentoPIN();
                        } else if (key == 'A') {
                            if (pinInserito.length() == 6) {
                                inviaAlServer("pin", pinInserito);
                            } else {
                                mylcd.clear();
                                mylcd.setCursor(0,0);
                                mylcd.print("PIN troppo corto");
                                mylcd.setCursor(0,1);
                                mylcd.print("Inserire 6 cifre");
                                delay(2000);
                                display_InserimentoPIN();
                            }
                        } else if (key == 'B' && pinInserito.length() > 0) {
                            pinInserito.remove(pinInserito.length() - 1);
                            display_InserimentoPIN();
                        }
                    }
                }
            }
        }
    }
}
