#include <Wire.h>
#include "Keypad.h"
#include <SPI.h>
#include <MFRC522.h>
#include <Crypto.h>
#include <AES.h>

// ---------- Tastierino ----------
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

// ---------- RFID ----------
#define SS_PIN 53
#define RST_PIN 10
MFRC522 mfrc522(SS_PIN, RST_PIN);

// ---------- Variabili globali ----------
String lastData = "";
volatile bool dataReady = false;

bool cardPresente = false;
String currentID = "";

// ---------- AES per I2C ----------
AES128 aes_i2c;
byte i2c_key[16] = {                   
  0x1A,0x2B,0x3C,0x4D,0x5E,0x6F,0x7A,0x8B,
  0x9C,0xAD,0xBE,0xCF,0xD1,0xE2,0xF3,0x04
};

byte i2c_buffer[16]; // Buffer per cifratura

// Funzione per cifrare dati con AES-ECB
String encryptI2CData(String plaintext) {
    int len = plaintext.length();
    if (len > 16) len = 16; // Limita a 16 byte
    
    // Azzera buffer e copia dati
    memset(i2c_buffer, 0, sizeof(i2c_buffer));
    plaintext.getBytes(i2c_buffer, len + 1);
    
    // Aggiungi padding PKCS7
    int padding = 16 - len;
    for (int i = len; i < 16; i++) {
        i2c_buffer[i] = (byte)padding;
    }
    
    // Cifra il blocco
    aes_i2c.encryptBlock(i2c_buffer, i2c_buffer);
    
    // Converti in esadecimale
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
    Wire.write(encrypted.c_str(), encrypted.length());
    
    Serial.print("Invio dati cifrati: ");
    Serial.println(encrypted);
    Serial.print("Dati originali: ");
    Serial.println(lastData);

    dataReady = false;
    lastData = "";
  } else {
    // Se non ci sono dati, invia stringa vuota cifrata
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
  
  Serial.println("Slave pronto - RFID e Tastierino con cifratura AES");
}

void loop() {
  // --- Lettura tastierino ---
  char key = keypad.getKey();
  if (key && !dataReady) {
    lastData = "K:" + String(key);
    dataReady = true;
    Serial.println("Tasto: " + String(key));
  }

  // --- Lettura RFID ---
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    String cardID = "";
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      if (mfrc522.uid.uidByte[i] < 0x10) cardID += "0";
      cardID += String(mfrc522.uid.uidByte[i], HEX);
    }
    cardID.toUpperCase();

    if (!cardPresente || cardID != currentID) {
      lastData = "C:" + cardID;
      dataReady = true;
      cardPresente = true;
      currentID = cardID;
      Serial.println("Carta letta: " + cardID);
    }
  } else if (cardPresente && !mfrc522.PICC_IsNewCardPresent()) {
    // Carta rimossa
    if (!dataReady) {
      lastData = "C:REMOVED";
      dataReady = true;
      cardPresente = false;
      currentID = "";
      Serial.println("Carta rimossa");
    }
  }
}