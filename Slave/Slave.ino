#include <Wire.h>        // Comunicazione I2C
#include "Keypad.h"      // Lettura tastierino a matrice
#include <SPI.h>         // Comunicazione SPI per RFID
#include <MFRC522.h>     // Lettura RFID/NFC RC522
#include <ChaChaPoly.h>  // Crittografia ChaCha20-Poly1305
#include "slave_secrets.h"

// ---------- Configurazione Tastierino ----------
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {9,8,7,6};
byte colPins[COLS] = {5,4,3,2};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);


// ---------- Configurazione RFID ----------
#define SS_PIN 53
#define RST_PIN 10
MFRC522 mfrc522(SS_PIN, RST_PIN);


// ---------- Variabili globali ----------
String lastData = "";      // contiene i dati che vogliamo inviare
bool dataReady = false;    // flag che indica se ci sono dati da mandare
bool cardPresente = false; // indica se c'è una carta vicino al lettore
String currentID = "";     // ID dell'ultima carta letta


// ---------- Funzione di cifratura ChaChaPoly ----------
void encryptI2CData(const uint8_t* plaintext, size_t len, uint8_t* ciphertext, uint8_t* tag) { //testo in chiaro, lunghezza testo in chiaro, testo cifrato, tag
    ChaChaPoly chacha; //crea un oggetto ChaChaPoly
    chacha.setKey(I2CSLAVE_KEY, sizeof(I2CSLAVE_KEY)); // setta la chiave
    chacha.setIV(I2CSLAVE_IV, sizeof(I2CSLAVE_IV)); // setta il nonce
    chacha.encrypt(ciphertext, plaintext, len);  //cifra il testo e lo inserisce i ciphertext
    chacha.computeTag(tag, 16); // calcola il tag per verificare integrità e autenticità del messaggio
}



// ---------- Funzione per inviare dati via I2C ----------
void sendData() {
    if (dataReady && lastData.length() > 0) { // se ci sono dati
        size_t len = lastData.length(); // calcola la lunghezza del dato
        uint8_t* buffer = (uint8_t*) malloc(len); // alloca un buffer
        memcpy(buffer, lastData.c_str(), len); // copia i len byte della stringa lastData dentro buffer

        uint8_t* ciphertext = (uint8_t*) malloc(len); // alloca un buffer cuphertext di dimensione len
        uint8_t tag[16];
        encryptI2CData(buffer, len, ciphertext, tag); // cifra i dati e inserisce in ciphertext, calcola inoltre il tag

        // Calcola dimensione totale
        size_t totalSize = len + 16;
        
        // Invia tutto in una volta con la dimensione corretta
        Wire.write(ciphertext, len);
        Wire.write(tag, 16);
        
        // Se necessario, aggiungi padding fino a 32 byte
        if (totalSize < 32) {
            uint8_t padding = 0xFF;
            for (size_t i = totalSize; i < 32; i++) {
                Wire.write(padding);
            }
        }

        free(buffer); // libera la memoria allocata per il buffer
        free(ciphertext); // libera la memoria allocata per ciphertext
        dataReady = false; // setta dataReady a falso
        lastData = ""; // svuota lastData
    } else {
        // Invia pacchetto IDLE (tutti zeri)
        uint8_t idle[32] = {0};
        Wire.write(idle, 32);
    }
}



// ---------- Setup ----------
void setup() {
    Wire.begin(8);            // Indirizzo I2C slave
    Wire.onRequest(sendData); // Callback invio dati
    SPI.begin();
    mfrc522.PCD_Init();
    Serial.begin(9600);
}

// ---------- Loop principale ----------
void loop() {
  // --- Lettura tastierino ---
  char key = keypad.getKey();
  if (key && !dataReady) {
    lastData = "K:" + String(key);
    dataReady = true;
  }

  // --- Lettura RFID ---
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
  // (se è presente una carta vicino al lettore && se il codice della carta è stato letto correttamente)
    String cardID = "";
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      if (mfrc522.uid.uidByte[i] < 0x10) cardID += "0";     // aggiungiamo 0 davanti ai byte minori di 16 per avere sempre due cifre esadecimali
      cardID += String(mfrc522.uid.uidByte[i], HEX);        // convertiamo i byte in esadecimale e infine trasformiamo in lettere maiuscole
    }
    cardID.toUpperCase();

    if (!cardPresente || cardID != currentID) { //  Se la carta letta è nuova (con un ID diverso dall'ultimo letto):
      lastData = "C:" + cardID;                 //  aggiorniamo le variabili con  i dati della nuova carta
      dataReady = true;
      cardPresente = true;
      currentID = cardID;
    }
  } else if (cardPresente && !mfrc522.PICC_IsNewCardPresent()) {    // Se invece la carta viene rimossa, dopo aver aggiornato le variabili booleane
    if (!dataReady) {                                               // l'ID salvato viene azzerato
      lastData = "C:REMOVED";
      dataReady = true;
      cardPresente = false;
      currentID = "";
    }
  }
}