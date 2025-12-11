#include <Wire.h>
#include <LiquidCrystal.h>
#include <WiFiS3.h>
#include <ArduinoJson.h>
#include "master_secrets.h"

// --- LIBRERIE CRITTOGRAFICHE ---
#include <Crypto.h>
#include <AES.h>             // AES per WiFi
#include <ChaChaPoly.h>      // ChaChaPoly per I2C
#include <Curve25519.h>


// --- Configurazione AES per WiFi---
AES128 aesWifi; // oggetto AES

// --- Configurazione I2C ---
#define SLAVE_ADDR 8 // indirizzo I2X dello slave
#define MAX_I2C_LEN 32  // il master richiede allo slave 32 byte: 16 byte dati + 16 byte tag


// --- Pin e LCD ---
#define LED_VERDE_PIN 8
#define LED_ROSSO_PIN 9
LiquidCrystal mylcd(12, 11, 5, 4, 3, 2);

// --- Pin Buzzer ---
#define BUZZER_PIN 7

// --- WiFi e server ---
char ssid[] = WIFI_SSID;
char pass[] = WIFI_PASSWORD;
char serverAddress[] = SERVER_ADDRESS;
int port = 5000;
WiFiClient client;

// --- Stato e variabili ---
String pinInserito = ""; // contiene il pin inserito dall'untente
String lastCardID = ""; // contiene l'ultimo ID della carta
bool cartaPresente = false; // indica se c'è una carta vicino al sensore

enum Stato { ATTESA, INSERIMENTO_PIN, ACCESSO_CONCESSO, ACCESSO_NEGATO };
Stato stato = ATTESA;

unsigned long lastRequest = 0;
const unsigned long REQUEST_INTERVAL = 200;

byte aes_buffer[128]; // Buffer per AES (WiFi)
byte aesKey[16];          // AES-128 per WiFi, derivata dal Diffie-Hellman
byte sharedSecret[32];    // Shared secret derivata da Curve25519

// Chiavi accordo DH
uint8_t masterPriv[32];
uint8_t masterPub[32];
uint8_t serverPub[32];


// ---------- Funzione di decifratura ChaChaPoly ----------
String decryptAndCheckI2C(uint8_t* buffer, size_t receivedLen) {
    // Minimo 17 byte: almeno 1 byte dati + 16 byte tag
    if (receivedLen < 17) return "";

    // Separiamo il tag (ultimi 16 byte) dal ciphertext
    size_t dataLen = receivedLen - 16;
    uint8_t* ciphertext = buffer;
    uint8_t* receivedTag = buffer + dataLen;

    // Allochiamo buffer plaintext
    uint8_t* plaintext = new uint8_t[dataLen + 1];

    // ChaChaPoly
    ChaChaPoly chacha;
    chacha.setKey(I2CMASTER_KEY, sizeof(I2CMASTER_KEY));
    chacha.setIV(I2CMASTER_IV, sizeof(I2CMASTER_IV));

    // Decifra
    chacha.decrypt(plaintext, ciphertext, dataLen);

    // Verifica tag
    if (!chacha.checkTag(receivedTag, 16)) {
        Serial.println("Errore I2C: Tag ChaChaPoly non valido. Dati corrotti.");
        delete[] plaintext; // dealloca plaintext
        return "";
    }

    // Chiudi stringa
    plaintext[dataLen] = '\0';

    // Filtra caratteri stampabili
    String decrypted = "";
    for (size_t i = 0; i < dataLen; i++) {
        char c = (char)plaintext[i];
        if (c >= 32 && c <= 126) decrypted += c;
    }

    delete[] plaintext;
    return decrypted;
}


// ---------- Funzione per richiedere dati allo slave ----------
void requestDataAndProcess() {
    const size_t CHUNK_SIZE = 16;
    uint8_t tempBuffer[128];
    size_t totalLen = 0;
    bool allZeros = true;

    // Richiediamo esattamente 32 byte (MAX_I2C_LEN)
    Wire.requestFrom(SLAVE_ADDR, MAX_I2C_LEN);

    while (Wire.available()) {
        uint8_t b = Wire.read();
        tempBuffer[totalLen++] = b;
        if (b != 0x00) allZeros = false;
        if (totalLen >= sizeof(tempBuffer)) break;
    }

    if (allZeros) return;

    // DEBUG: stampa solo i byte effettivamente ricevuti
    Serial.print("Pacchetto I2C raw (");
    Serial.print(totalLen);
    Serial.print(" byte): ");
    for (size_t i = 0; i < totalLen; i++) {
        if (tempBuffer[i] < 0x10) Serial.print("0");
        Serial.print(tempBuffer[i], HEX);
        Serial.print(" ");
    }
    Serial.println();

    // Pulisci i byte di padding FF
    size_t effectiveLen = totalLen;
    // Controlla se ci sono padding FF alla fine
    while (effectiveLen > 0 && tempBuffer[effectiveLen - 1] == 0xFF) {
        effectiveLen--;
    }

    // Se dopo aver rimosso i padding FF non abbiamo almeno 17 byte, errore
    if (effectiveLen < 17) {
        Serial.println("ERRORE: Pacchetto troppo corto dopo rimozione padding");
        return;
    }

    String decrypted = decryptAndCheckI2C(tempBuffer, effectiveLen);

    if (decrypted.length() > 0) {
        Serial.print("Pacchetto I2C decifrato: ");
        Serial.println(decrypted);

        // --- Gestione dati decifrati ---
        if (decrypted.startsWith("C:")) {
            String cardID = decrypted.substring(2);
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
        } else if (decrypted.startsWith("K:")) {
            char key = decrypted[2];

            if (stato == INSERIMENTO_PIN && cartaPresente) {
                if (key >= '0' && key <= '9' && pinInserito.length() < 6) {
                    pinInserito += key;
                    display_InserimentoPIN();
                    suono_press_tastiera();
                } else if (key == 'A') {
                    suono_press_tastiera();
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
                    suono_press_tastiera();
                }
            }
        }
    } else {
        Serial.println("Pacchetto I2C decifrato: dati corrotti o tag non valido");
    }
}



// ---------- Cifratura AES-ECB ----------
String encryptAES(String plaintext) {
    const int BLOCK_SIZE = 16;                      // definiamo la dimensione dei blocchi a 16 byte (128 bit)--
    int len = plaintext.length();                   // calcoliamo la lunghezza del messaggio da cifrare --

/*
    se la lunghezza del messaggio supera i 100 byte, quando si aggiunge il padding la lunghezza potrebbe andare oltre i 128 byte
    e il loop che cifra i blocchi potrebbe andare oltre il limite di 128 byte corrompendo la RAM
*/
    if (len > 100) { // Limita la dimensione per sicurezza
        Serial.println("Errore: Payload troppo grande.");
        return "";
    }
/*

    calcola quanto deve essere lungo il padding da aggiungere all'ultimo blocco affinché sia esattamente di 16 byte
*/
    int padding_len; // definiamo la lunghezza del padding
    if (len % BLOCK_SIZE == 0) {
        padding_len = BLOCK_SIZE;  // testo già multiplo di 16 → aggiungi un blocco di padding, così chi decifra sa comunque quanti byte togliere
    } else {
        padding_len = BLOCK_SIZE - (len % BLOCK_SIZE);  // completa l'ultimo blocco
    }
    int paddedLen = len + padding_len; // lunghezza totale messaggio+padding

    // Azzera il buffer per evitare che ci possano essere dati residui da cifrature precedenti
    // nello specifico memset setta a 0 tutti i 128 byte del buffer
    memset(aes_buffer, 0, sizeof(aes_buffer));
    // Copia il testo in chiaro nel buffer, len+1 indica il numero di byte da copiare, include anche il terminatore della stringa
    plaintext.getBytes(aes_buffer, len + 1);

    // Aggiungi il padding PKCS7
    // Nel buffer, dopo aver inserito il testo in chiaro, aggiungiamo come padding nei byte successivi il valore del padding stesso
    // Esempio: se il messagio occupava 10 byte e quindi il padding è di 6 allora nel buffer avremo: [10 byte] 06 06 06 06 06 06
    for (int i = 0; i < padding_len; i++) {
        aes_buffer[len + i] = (byte)padding_len;
    }

    // Cifra a blocchi di 16 byte
    for (int i = 0; i < paddedLen; i += BLOCK_SIZE) {
        aesWifi.encryptBlock(aes_buffer + i, aes_buffer + i);
        /*
        aes_buffer+i è il puntatore all'i-esimo blocco del buffer, la funzione prende quel blocco, lo cifra e
        sostituisce il vecchio contenuto del blocco (testo in chiaro) con quello nuovo (testo cifrato)
        alla fine nel buffer al posto del testo in chiaro sarà presente il testo cifrato
        */
    }

    // Converti i byte in esadecimale
    String hex_cipher = "";
    for (int i = 0; i < paddedLen; i++) {
        if (aes_buffer[i] < 0x10) hex_cipher += "0"; // se il byte letto è più piccolo di 16, quindi in esadecimale avrebbe una sola cifra, aggiungi 0 davanti
        hex_cipher += String(aes_buffer[i], HEX);
    }

    return hex_cipher;
}


// ---------- Decifratura AES-ECB ----------
String decryptAES(String encryptedHex) {
    const int BLOCK_SIZE = 16;

    int encryptedLen = encryptedHex.length() / 2; // lunghezza della stringa da decifrare
    // L'input della funzione è una stringa esadecimale (2 caratteri per ogni byte), per sapere quanti byte ci sono dividiamo la lunghezza per 2

    // Controlliamo che i dati da decifrare non siano più grande del buffer definito prima
    if (encryptedLen > sizeof(aes_buffer)) {
        Serial.println("Errore: Dati cifrati troppo grandi.");
        return "";
    }

    // Converti esadecimali in bytes
    memset(aes_buffer, 0, sizeof(aes_buffer)); // azzera il buffer (come nella cifratura)
    for (int i = 0; i < encryptedLen; i++) {
        String byteStr = encryptedHex.substring(i * 2, i * 2 + 2); // Ogni coppia di caratteri in esadecimale viene convertita in byte
        aes_buffer[i] = (byte) strtol(byteStr.c_str(), NULL, 16);  // Ora il buffer contiene i byte cifrati
    }

    // Decifra a blocchi di 16 byte
    for (int i = 0; i < encryptedLen; i += BLOCK_SIZE) {
        aesWifi.decryptBlock(aes_buffer + i, aes_buffer + i); // stesso funzionamento e stessa logica della funzione di cifratura
    }

    // Rimuovi padding
    byte padding = aes_buffer[encryptedLen - 1]; // l'ultimo byte indica quanti byte di padding sono stati aggiunti
    int dataLen = encryptedLen;
    if (padding > 0 && padding <= BLOCK_SIZE) { // Se il padding è valido (compreso tra 1 e 16)
        dataLen = encryptedLen - padding; // calcola la lunghezza reale del messaggio (senza padding)
    }

    // Converti in stringa
    String decrypted = "";
    for (int i = 0; i < dataLen; i++) {
        decrypted += (char)aes_buffer[i];
    }
    // cicliamo sui byte decifrati del messaggio e convertiamo ogni byte in carattere, aggiungendolo alla stringa decrypted

    return decrypted;
}


// ---------- Funzioni buzzer ----------
void suono_press_tastiera(){
    tone(BUZZER_PIN, 1000, 100);
    delay(100);
}

void suono_accesso_concesso(){
    for(int i = 0; i < 4; i++){
        tone(BUZZER_PIN, 400 + (i * 200), 200);
        delay(250);
    }
    noTone(BUZZER_PIN);
}

void suono_accesso_negato(){
    for(int i = 0; i < 3; i++){
        tone(BUZZER_PIN, 300, 150);
        delay(200);
        tone(BUZZER_PIN, 100, 150);
        delay(200);
    }
    noTone(BUZZER_PIN);
}

void suono_carta_valida(){
    tone(BUZZER_PIN, 1000, 150);
    delay(100);
}

// --- Funzioni display ---
void display_Attesa() {
    mylcd.clear();
    delay(10);
    mylcd.setCursor(0, 0);
    mylcd.print("Inserire carta");
    digitalWrite(LED_VERDE_PIN, LOW);
    digitalWrite(LED_ROSSO_PIN, LOW);
}

void display_AccessoConcesso(String nome, String cognome, String saldo) {
    digitalWrite(LED_VERDE_PIN, HIGH);
    digitalWrite(LED_ROSSO_PIN, LOW);
    mylcd.clear();
    delay(10);
    mylcd.setCursor(0, 0);
    mylcd.print(nome + " " + cognome);
    mylcd.setCursor(0, 1);
    mylcd.print("Saldo: " + saldo + " EUR"); // Ho messo EUR invece di € per evitare problemi di caratteri su LCD
}

void display_AccessoNegato(String msg = "") {
    digitalWrite(LED_VERDE_PIN, LOW);
    digitalWrite(LED_ROSSO_PIN, HIGH);
    mylcd.clear();
    delay(10);
    mylcd.setCursor(0, 0);
    mylcd.print("Accesso Negato");
    mylcd.setCursor(0, 1);
    if (msg != "") {
        mylcd.print(msg);
    } else {
        mylcd.print("Riprova");
    }
}

void display_InserimentoPIN() {
    mylcd.clear();
    delay(10);
    mylcd.setCursor(0, 0);
    mylcd.print("PIN: ");

    for (int i = 0; i < pinInserito.length(); i++) {
        mylcd.print("*");
    }
    for (int i = pinInserito.length(); i < 6; i++) {
        mylcd.print("_");
    }

    mylcd.setCursor(0, 1);
    mylcd.print("A=OK B=Canc");
}


// --- Funzione per convertire da byte a esadecimali ---

String bytesToHex(uint8_t* bytes, size_t len) {
    String hexStr = "";
    for (size_t i = 0; i < len; i++) {
        if (bytes[i] < 0x10) hexStr += "0";
        hexStr += String(bytes[i], HEX);
    }
    return hexStr;
}


// --- Funzione per inviare la chiave pubblica dell'accordo al server ---

void inviaChiavePubblica() {
    if (!client.connected()) {
        client.stop();
        delay(100);
        if (!client.connect(serverAddress, port)) {
            Serial.println("Connessione al server fallita!");
            return;
        }
    }

    String pubkeyHex = bytesToHex(masterPub, 32);

    // Creiamo un semplice JSON da inviare in chiaro
    String json = "{\"type\":\"pubkey\",\"value\":\"" + pubkeyHex + "\"}";

    client.println(json); // invio in chiaro
}


// --- Funzione per ricevere la chiave pubblica dell'accordo dal server ---

bool riceviChiaveServer(){
    String json = client.readStringUntil('\n');
        json.trim();

    if (json.length() == 0) {
        return false;
    }

    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, json);

    if(error){
        return false;
    }
    if (!doc.containsKey("type") || String(doc["type"]) != "pubkey") {
        return false;
    }

    String serverPubHex = doc["value"];
    if (serverPubHex.length() != 64) { // 32 byte * 2
        return false;
    }

    // Converti da esadecimale a byte
    for (int i = 0; i < 32; i++) {
        serverPub[i] = strtol(serverPubHex.substring(i * 2, i * 2 + 2).c_str(), NULL, 16);
    }

    return true;
}


void setup() {
    Wire.begin();
    mylcd.begin(16, 2);
    pinMode(LED_VERDE_PIN, OUTPUT);
    pinMode(LED_ROSSO_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    Serial.begin(115200);

    // La chiave ChaChaPoly viene impostata all'interno della funzione di decrittazione
    // La chiave dell'AES con il server viene generata tramite accordo ECDH

    // Display iniziale con delay
    delay(100);
    display_Attesa();

    // --- Connessione WiFi ---
    WiFi.begin(ssid, pass);
    mylcd.clear();
    delay(10);
    mylcd.setCursor(0,0);
    mylcd.print("Connessione WiFi");

    int tentativi = 0;
    while (WiFi.status() != WL_CONNECTED && tentativi < 20) {
        delay(500);
        mylcd.print(".");
        tentativi++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        mylcd.clear();
        delay(10);
        mylcd.setCursor(0,0);
        mylcd.print("WiFi connessa!");
        delay(1000);
        display_Attesa();

        //Accordo su chiavi Diffie-Hellman con curve ellittiche
        Curve25519::dh1(masterPub, masterPriv);
        inviaChiavePubblica();

        if(riceviChiaveServer()){
            uint8_t shared[32];
            memcpy(shared, serverPub, 32);

            if(!Curve25519::dh2(shared,masterPriv)){
                //la chiave del server ricevuta e' invalida
                while (true) {
                    delay(1000); // loop infinito, Arduino resta fermo
                }
            }

            memcpy(sharedSecret, shared, 32);

            // Prendo i primi 16 byte come AES‑128 key
            memcpy(aesKey, sharedSecret, 16);
            aesWifi.setKey(aesKey, 16);
        }

    } else {
        mylcd.clear();
        delay(10);
        mylcd.setCursor(0,0);
        mylcd.print("Err. WiFi");
        mylcd.setCursor(0,1);
        mylcd.print("Riavvia manualm.");
        while(true) {
            delay(1000);
        }
    }

}

// --- Funzione per inviare JSON cifrato al server ---
void inviaAlServer(String tipo, String valore) {

    if (!client.connected()) {
        client.stop();
        delay(100);
        if (!client.connect(serverAddress, port)) {
            Serial.println("Connessione al server fallita!");
            return;
        }
    }

    StaticJsonDocument<200> doc;
    doc["type"] = tipo;
    doc["value"] = valore;

    String json_payload;
    serializeJson(doc, json_payload);

    String encrypted = encryptAES(json_payload);

    if (encrypted.length() == 0) {
        Serial.println("Errore nella cifratura!");
        return;
    }

    client.println(encrypted);
}

// --- Loop principale ---
void loop() {
    unsigned long now = millis();

    // --- Ricezione risposta cifrata dal server (Logica AES, Invariata) ---
    if (client.connected() && client.available()) {
        String encryptedResponse = client.readStringUntil('\n');
        Serial.print(encryptedResponse);
        encryptedResponse.trim();

        if (encryptedResponse.length() > 0) {
            String decryptedResponse = decryptAES(encryptedResponse);

            if (decryptedResponse.length() > 0) {
                StaticJsonDocument<200> doc;
                DeserializationError error = deserializeJson(doc, decryptedResponse);

                if (!error) {
                    String status = doc["status"];
                    if (status == "CARTA_VALIDA") {
                        stato = INSERIMENTO_PIN;
                        pinInserito = "";
                        digitalWrite(LED_VERDE_PIN, HIGH);
                        digitalWrite(LED_ROSSO_PIN, LOW);
                        display_InserimentoPIN();
                        suono_carta_valida();
                    } else if (status == "CARTA_NON_VALIDA") {
                        stato = ACCESSO_NEGATO;
                        display_AccessoNegato("Carta non valida");
                        suono_accesso_negato();
                    } else if (status == "ACCESSO_CONCESSO") {
                        stato = ACCESSO_CONCESSO;
                        String nome = doc["nome"];
                        String cognome = doc["cognome"];
                        String saldo = doc["saldo"];
                        display_AccessoConcesso(nome, cognome, saldo);
                        suono_accesso_concesso();
                    } else if (status == "ACCESSO_NEGATO") {
                        display_AccessoNegato("PIN errato");
                        suono_accesso_negato();
                    }
                } else {
                    Serial.println("Errore deserializzazione JSON");
                }
            } else {
                Serial.println("Errore decifratura risposta");
            }
        }
    }

    // --- Lettura RFID e tastierino I2C (Logica ChaChaPoly) ---
    if (now - lastRequest >= REQUEST_INTERVAL) {
        lastRequest = now;
        requestDataAndProcess(); // Chiama la nuova funzione ChaChaPoly
    }

    // Piccola pausa per stabilizzare il loop
    delay(10);
}