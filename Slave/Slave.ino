#include <Wire.h>
#include "Keypad.h"
#include <SPI.h>
#include <MFRC522.h>

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

String lastData = "";       // ultimo dato da inviare
volatile bool dataReady = false; // segnala al master che c'è un dato nuovo

void setup() {
  Wire.begin(8);              // slave I2C
  Wire.onRequest(sendData);   // invio dati quando richiesto
  SPI.begin();
  mfrc522.PCD_Init();
}

void loop() {
  // Lettura tastierino
  char key = keypad.getKey();
  if (key && !dataReady) {
    lastData = "K:" + String(key) + "\n"; // prefisso K: per tasto
    dataReady = true;
  }

  // Lettura RFID
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial() && !dataReady) {
    String cardID = "";
    for (byte i=0; i<mfrc522.uid.size; i++) {
      if (mfrc522.uid.uidByte[i]<0x10) cardID += "0";
      cardID += String(mfrc522.uid.uidByte[i], HEX);
    }
    cardID.toUpperCase();
    lastData = "C:" + cardID + "\n"; // prefisso C: per card
    dataReady = true;
  }

  // loop vuoto: invio dati avviene solo tramite onRequest()
}

void sendData() {
  if (dataReady && lastData.length() > 0) {
    Wire.write(lastData.c_str(), lastData.length());
    lastData = "";
    dataReady = false; // reset flag dopo invio
  }
  // se non c'è dato, non inviare nulla
}
