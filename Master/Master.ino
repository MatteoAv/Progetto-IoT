#include <Wire.h> 
#include <LiquidCrystal.h> 
#include <WiFiS3.h> 
#include <ArduinoJson.h> 
#include "arduino_secrets.h" 

// --- LIBRERIA CRYPTO (rweather) - Solo AES base ---
#include <Crypto.h>
#include <AES.h>

// Dimensione chiave usata per cifrare in AES, 16 byte -> 128 bit'
const size_t KEY_SIZE = 16;

// --- Configurazione AES per WiFi---
byte aes_key[KEY_SIZE] = {0x6C, 0x61, 0x43, 0x68, 0x69, 0x61, 0x76, 0x65, 0x53, 0x65, 0x67, 0x72, 0x65, 0x74, 0x61, 0x31};
// Oggetto AES
AES128 aesWifi;

// --- Configurazione AES per I2C ---
byte i2c_key[KEY_SIZE] = {0x1A,0x2B,0x3C,0x4D,0x5E,0x6F,0x7A,0x8B,0x9C,0xAD,0xBE,0xCF,0xD1,0xE2,0xF3,0x04};
// Oggetto AES
AES128 aes_i2c;

// --- Pin e LCD ---
#define LED_VERDE_PIN 8 
#define LED_ROSSO_PIN 9 
LiquidCrystal mylcd(12, 11, 5, 4, 3, 2); 

// --- WiFi e server ---
char ssid[] = WIFI_SSID;
char pass[] = WIFI_PASSWORD;
char serverAddress[] = SERVER_ADDRESS; 
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

// --- Buffer per AES ---
byte aes_buffer[128]; // Buffer per AES con WiFi
byte i2c_buffer[32];  // Buffer per AES con I2C  -> Wire può trasportare al massimo 32 byte

// --- Funzioni crittografiche AES-ECB con WiFi---
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

// --- Funzioni AES per I2C ---
String decryptI2CData(String encryptedHex) {
    const int BLOCK_SIZE = 16;      // Dimensione dei blocchi
    
    // Controlliamo che la stringa da decifrare sia di 32 caratteri esadecimali (16 byte, grandezza di un blocco), se è diversa, la cifratura non è corretta
    if (encryptedHex.length() != 32) {
        Serial.println("Errore: Dati I2C cifrati di dimensione errata");
        return "";
    }
    
    // Usiamo memset per svuotare il buffer per evitare che ci possano essere dati residui da decifrature precedenti
    memset(i2c_buffer, 0, sizeof(i2c_buffer));
    // Convertiamo la stringa da esadecimale a byte e salviamola nel buffer, al posto della vecchia stringa
    for (int i = 0; i < 16; i++) {
        String byteStr = encryptedHex.substring(i * 2, i * 2 + 2);
        i2c_buffer[i] = (byte) strtol(byteStr.c_str(), NULL, 16);
    }
    
    // Decifriamo il blocco
    aes_i2c.decryptBlock(i2c_buffer, i2c_buffer);
    
    // Rimuoviamo il padding PKCS7
    byte padding = i2c_buffer[15];
    int dataLen = 16;
    if (padding > 0 && padding <= BLOCK_SIZE) { // Se nel blocco c'è un padding valido, lo prende dall'ultimo byte
        dataLen = 16 - padding;                 // e calcola la lunghezza della stringa vera e propria senza padding
    }
    /*
       NOTA: questa soluzione per togliere il padding funziona qui perchè nella cifratura viene inviato sempre e solo un blocco di dimensione inferiore a 16 byte.
       Se il blocco fosse di 16 byte o ci fossero più blocchi, questo metodo per calcolare il padding non funzionerebbe, perchè presuppone sempre
       che l'ultimo byte del blocco sia padding, quindi nel nostro caso va bene, perchè le informazioni che inviamo sono più piccole di 16 byte
    */
    
    // Convertiamo i byte (che ora sono in chiaro e senza padding) in stringa
    String decrypted = "";
    for (int i = 0; i < dataLen; i++) {
        // Filtra solo caratteri stampabili e validi per i nostri comandi
        char c = (char)i2c_buffer[i];
        if (c >= 32 && c <= 126) { // Caratteri ASCII stampabili
            decrypted += c;
        }
    }
    
    return decrypted;
}

// --- Funzioni display ---
void display_Attesa() {
    mylcd.clear();
    delay(10); // Piccolo delay per stabilizzare
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
    mylcd.print("Saldo: " + saldo + "€");
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
    
    // Mostra asterischi invece dei numeri reali
    for (int i = 0; i < pinInserito.length(); i++) {
        mylcd.print("*");
    }
    for (int i = pinInserito.length(); i < 6; i++) {
        mylcd.print("_");
    }
    
    mylcd.setCursor(0, 1);
    mylcd.print("A=OK B=Canc");
}

void setup() {
    Wire.begin();
    mylcd.begin(16, 2);
    pinMode(LED_VERDE_PIN, OUTPUT);
    pinMode(LED_ROSSO_PIN, OUTPUT);

    Serial.begin(115200); 
    
    // Inizializziamo AES con le chiavi
    aesWifi.setKey(aes_key, KEY_SIZE);   // Per WiFi
    aes_i2c.setKey(i2c_key, KEY_SIZE);  // Per I2C
    
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
    } else {
        mylcd.clear();
        delay(10);
        mylcd.setCursor(0,0);
        mylcd.print("Err. WiFi");
        mylcd.setCursor(0,1);
        mylcd.print("Riavvia manualm.");
        // Non resettiamo, solo messaggio di errore
        while(true) {
            delay(1000); // Loop infinito con messaggio di errore
        }
    }
}

// --- Funzione per inviare JSON cifrato al server ---
void inviaAlServer(String tipo, String valore) {

    // Controlla se il client è già connesso al server, se non lo è aspetta un intervallo di 100 e poi ritenta, se la connessione non va termina
    if (!client.connected()) {
        client.stop();
        delay(100);
        if (!client.connect(serverAddress, port)) {
            Serial.println("Connessione al server fallita!");
            return;
        }
    }

    // Crea il contenitore JSON di 200 byte in cui verranno messi i dati da inviare
    StaticJsonDocument<200> doc;
    doc["type"] = tipo;      
    doc["value"] = valore;


    // Converte il json in una stringa
    String json_payload;
    serializeJson(doc, json_payload);

    // Cifra il JSON
    String encrypted = encryptAES(json_payload);
    
    if (encrypted.length() == 0) {
        Serial.println("Errore nella cifratura!");
        return;
    }

    // Invia i dati cifrati al server
    client.println(encrypted);
}

// --- Loop principale ---
void loop() {
    // indica i millisecondi passati da quando la scheda è stata avviata o resettata
    unsigned long now = millis();

    // --- Ricezione risposta cifrata dal server ---
    if (client.connected() && client.available()) {                           // Se la connessione è attiva e c'è almeno un dato da leggere
        String encryptedResponse = client.readStringUntil('\n');              // legge la stringa fino al carattere newline
        encryptedResponse.trim();                                             // rimuove spazi e ritorni di linea inutili
        
        if (encryptedResponse.length() > 0) {
            // Decifra la risposta
            String decryptedResponse = decryptAES(encryptedResponse);
            
            if (decryptedResponse.length() > 0) {
                StaticJsonDocument<200> doc;
                DeserializationError error = deserializeJson(doc, decryptedResponse); // converte il JSON decifrato in un oggetto per leggerne i valori
                
                if (!error) {   // Se l'oggetto è valido e non ci sono errori controlla lo status restituito dal server
                    String status = doc["status"];
                    if (status == "CARTA_VALIDA") {
                        stato = INSERIMENTO_PIN;
                        pinInserito = "";
                        digitalWrite(LED_VERDE_PIN, HIGH);
                        digitalWrite(LED_ROSSO_PIN, LOW);
                        display_InserimentoPIN();
                    } else if (status == "CARTA_NON_VALIDA") {
                        stato = ACCESSO_NEGATO;
                        display_AccessoNegato("Carta non valida");
                    } else if (status == "ACCESSO_CONCESSO") {
                        stato = ACCESSO_CONCESSO;
                        String nome = doc["nome"];
                        String cognome = doc["cognome"];
                        String saldo = doc["saldo"];
                        display_AccessoConcesso(nome, cognome, saldo);
                    } else if (status == "ACCESSO_NEGATO") {
                        display_AccessoNegato("PIN errato");
                    }
                } else {
                    Serial.println("Errore deserializzazione JSON");
                }
            } else {
                Serial.println("Errore decifratura risposta");
            }
        }
    }

    // --- Lettura RFID e tastierino I2C ---
    if (now - lastRequest >= REQUEST_INTERVAL) {    // Controlla se è passato abbastanza tempo dall'ultima lettura I2C.
        lastRequest = now;                          // Serve a evitare di leggere continuamente i dati dallo slave e sovraccaricare il bus.

        Wire.requestFrom(8, 32);                    // (indirizzo dello slave, numero massimo di byte richiesti)
        delay(5); // Piccolo delay per stabilizzare I2C

        // Se ci sono dati disponibili, vengono letti uno alla volta
        if (Wire.available()) {
            String encryptedHex = "";
            while (Wire.available()) {
                char c = Wire.read();
                if (c != 0) encryptedHex += c; // Ignora byte nulli
            }
            encryptedHex.trim(); // Rimuoviamo eventuali spazi bianchi a inizio o fine stringa

            // Se abbiamo dati cifrati validi (32 caratteri esadecimali = 16 byte) li decifriamo
            if (encryptedHex.length() == 32) {
                String decrypted = decryptI2CData(encryptedHex);

                if (decrypted.length() > 0) {
                    // Se il messaggio comincia con C riguarda la carta RFID
                    if (decrypted.startsWith("C:")) {
                        String cardID = decrypted.substring(2);
                        if (cardID == "REMOVED") {              //se la carta è stata rimossa resetta tutto e torna in attesa
                            cartaPresente = false;
                            lastCardID = "";
                            stato = ATTESA;
                            display_Attesa();
                            pinInserito = "";
                        } else if (cardID != lastCardID) {      //se la carta è diversa da quella che stava inserita prima invia l'ID al server
                            lastCardID = cardID;
                            cartaPresente = true;
                            inviaAlServer("card", cardID);
                        }
                    }

                    // Se il messaggio comincia con K riguarda il tastierino
                    else if (decrypted.startsWith("K:")) {
                        char key = decrypted[2];
                        /*
                        prende il carattere digitato dal tastierino,
                        siccome i messaggi provenienti dal tastierino sono di questo tipo: K:"tasto", prendiamo il carattere 2
                        In base al tasto premuto dal tastierino si verificano i seguenti eventi
                        */
                        if (stato == INSERIMENTO_PIN && cartaPresente) {
                            if (key >= '0' && key <= '9' && pinInserito.length() < 6) {
                                pinInserito += key;
                                display_InserimentoPIN();
                            } else if (key == 'A') {
                                if (pinInserito.length() == 6) {
                                    inviaAlServer("pin", pinInserito);
                                } else {
                                    mylcd.clear();
                                    delay(10);
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
    
    // Piccola pausa per stabilizzare il loop
    delay(10);
}