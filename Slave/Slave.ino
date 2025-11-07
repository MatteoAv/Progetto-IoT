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

String lastData = "";
volatile bool dataReady = false;

bool cardPresente = false;
String currentID = "";

void setup() {
  Wire.begin(8);              
  Wire.onRequest(sendData);   
  SPI.begin();
  mfrc522.PCD_Init();
  Serial.begin(9600);
}

void loop() {
  // --- Lettura tastierino ---
  char key = keypad.getKey();
  if (key && !dataReady) {
    lastData = "K:" + String(key) + "\n";
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
      lastData = "C:" + cardID + "\n";
      dataReady = true;
      cardPresente = true;
      currentID = cardID;
      Serial.println("Carta letta: " + cardID);
    }
  } else if (cardPresente && !mfrc522.PICC_IsNewCardPresent()) {
    // Carta rimossa
    if (!dataReady) {
      lastData = "C:REMOVED\n";
      dataReady = true;
      cardPresente = false;
      currentID = "";
      Serial.println("Carta rimossa");
    }
  }
}

void sendData() {
  if (dataReady && lastData.length() > 0) {
    Wire.write(lastData.c_str(), lastData.length());
    dataReady = false;
    lastData = "";
  }
}
