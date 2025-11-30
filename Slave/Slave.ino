#include <Wire.h>
#include "Keypad.h"
#include <SPI.h>
#include <MFRC522.h>
#include <Crypto.h>
#include <AES.h>

// ---------- Configurazione Tastierino ----------
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {9,8,7,6};             // Definiamo righe e colonne
byte colPins[COLS] = {5,4,3,2};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS); // Keypad gestisce la lettura dei tasti

// ---------- Configurazione RFID ----------
#define SS_PIN 53
#define RST_PIN 10
MFRC522 mfrc522(SS_PIN, RST_PIN); // Creiamo l'oggetto per gestire il sensore

// ---------- Variabili globali ----------
String lastData = "";
volatile bool dataReady = false;

bool cardPresente = false; // indica lo stato del lettore, è True se sul lettore è presente una carta, False altrimenti
String currentID = "";

// ---------- AES per I2C ----------
AES128 aes_i2c;
byte i2c_key[16] = {                           // Definiamo la chiave di cifratura a 128 bit
  0x1A,0x2B,0x3C,0x4D,0x5E,0x6F,0x7A,0x8B,
  0x9C,0xAD,0xBE,0xCF,0xD1,0xE2,0xF3,0x04
};

byte i2c_buffer[16]; // Buffer per cifratura

// Funzione per cifrare dati con AES-ECB
String encryptI2CData(String plaintext) {
    int len = plaintext.length();
    /*
    Nella trasmissione Wire di I2C il limite massimo di dati che possono essere trasmessi per volta è di 32 byte, nella nostra cifratura noi inviamo un solo
    blocco alla volta (16 byte), in quanto i dati che devono essere trasmessi sono più piccoli, questo vuol dire però che i dati effettivi che possono essere inviati per volta
    sono 15 byte, in quanto ci serve sempre almeno 1 byte per il padding
    */
    if (len > 15) len = 15; // Limita il testo in chiaro a 15 byte,
    
    // Azzera il buffer e copia i nuovi dati nel buffer
    memset(i2c_buffer, 0, sizeof(i2c_buffer));
    plaintext.getBytes(i2c_buffer, len + 1);
    
    // Aggiungiamo il padding PKCS7 in base alla lunghezza del messaggio
    int padding = 16 - len;
    for (int i = len; i < 16; i++) {
        i2c_buffer[i] = (byte)padding;
    }
    
    // Cifriamo il singolo blocco
    aes_i2c.encryptBlock(i2c_buffer, i2c_buffer);
    
    // Convertiamo i byte in esadecimale
    String result = "";
    for (int i = 0; i < 16; i++) {
        if (i2c_buffer[i] < 0x10) result += "0";
        result += String(i2c_buffer[i], HEX);
    }
    
    return result;
}

void sendData() {
  if (dataReady && lastData.length() > 0) {
    // Cifra i dati prima di inviarli
    String encrypted = encryptI2CData(lastData);

    // Invia i dati cifrati
    Wire.write(encrypted.c_str(), encrypted.length()); // Wire.write(i dati da inviare (convertiti in un puntatore a carattere), la dimensione dei dati)

    // Resettale variabili relative ai dati dopo averli inviati
    dataReady = false;
    lastData = "";
  } else {
    /* Se non ci sono dati, invia una stringa vuota cifrata
       Questo perchè quando il master fa una richiesta lo slave deve sempre rispondere, quindi se non ci sono dati
       da inviare, risponde con una stringa vuota, altrimenti se il master non riceve nessun byte si possono verificare errori o comportamenti strani
     */
    String emptyEncrypted = encryptI2CData("");
    Wire.write(emptyEncrypted.c_str(), emptyEncrypted.length());
  }
}

void setup() {
  Wire.begin(8);               // Imposta l'indirizzo I2C dello slave
  Wire.onRequest(sendData);    // Callback per invio dati
  SPI.begin();
  mfrc522.PCD_Init();
  Serial.begin(9600);

  // Inizializza AES con la chiave
  aes_i2c.setKey(i2c_key, sizeof(i2c_key));
}

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