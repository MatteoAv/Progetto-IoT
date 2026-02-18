# SafeGate

SafeGate is a secure IoT-based access control system implementing sequential two-factor authentication (2FA) on embedded hardware.  
The system combines RFID identification and PIN verification, supported by a secure communication architecture and a MongoDB-backed authentication server.

The core objective of this project is not only access control, but the secure design of communications between embedded devices and backend infrastructure.

---

## 🎯 Project Overview

SafeGate implements a two-step authentication process:

1. The user scans a valid RFID card.
2. If the card is recognized, the system requests a PIN associated with that card.
3. If both factors are correct, access is granted.
4. Otherwise, access is denied.

User credentials are securely stored in a MongoDB database.

Sensitive data is not stored in plaintext. Instead, credentials are protected using salted hashing.

For each user, the database stores:

- The hash of the RFID card ID combined with the salt    
- The hash of the PIN combined with the salt  

The system is designed around a master–slave embedded architecture with cryptographic protection applied at each communication layer.

---

## 🖥️ Hardware Architecture

SafeGate is built using two embedded boards:

### Slave (Input Device)

- Elegoo Mega (no WiFi capability)
- Connected components:
  - RFID reader
  - Numeric keypad

This board is responsible only for collecting user input.  
It does not perform authentication or communicate directly with the server.


### Master (Control & Network Device)

- Arduino Uno R4 WiFi
- Connected components:
  - LCD display
  - Status LEDs
  - Buzzer

This board:
- Communicates with the server
- Performs cryptographic operations
- Displays authentication results
- Controls output feedback

---

## 🔐 Secure Communication Between Slave (Elegoo Mega) and Master (Arduino Uno R4 WiFi)

### 📡 Transport Layer

Communication occurs via wired I²C serial connection.

By default, I²C communication is unencrypted and vulnerable to:
- Bus sniffing  
- Message injection  
- Replay attacks  
- Data tampering  

### 🛡️ Applied Security Mechanism

To protect this channel, SafeGate uses:

- ChaCha20-Poly1305 (ChaChaPoly) authenticated encryption

This provides:

- Confidentiality (messages are encrypted)  
- Integrity (tampering is detected)  
- Authentication (invalid messages are rejected)  

### ⚙️ Communication Process

1. The slave collects RFID and PIN input.
2. Before transmission, the message is:
   - Padded to a fixed length (32 bytes)
   - Encrypted using ChaCha20
   - Authenticated using Poly1305
3. The encrypted payload and authentication tag are sent via I²C.
4. The master verifies the authentication tag before decrypting.
5. If verification fails, the message is discarded.

This ensures that even if an attacker physically accesses the I2C bus, the communication remains confidential and tamper-resistant.

---

## 🌐 Secure Communication Between Master (Arduino Uno R4 WiFi) and Server

### Transport Layer

Communication occurs over WiFi.

Since WiFi traffic is exposed to remote interception risks, session-level cryptographic protection is required.

---

### 🗝️ Key Exchange: Elliptic Curve Diffie-Hellman (ECDH)

Before transmitting any sensitive authentication data between the master and the server, for each session, a secure symmetric key is established using Elliptic Curve Diffie-Hellman (ECDH).

The process works as follows:

1. Both the master and the server generate their own ephemeral key pairs (a private key and a corresponding public point on the elliptic curve).
2. Each side sends its key to the other side.
3. Upon receiving the counterpart's public key, each side multiplies it by its own private key. Due to the properties of elliptic curves, both sides end up computing the same shared secret point.
4. The shared secret is then used to derive the AES key: the first 128 bits of the shared secret are extracted and used as the AES-128 key for encrypting communication.

This approach ensures:

- The symmetric AES key is never transmitted in plaintext.
- Protection against passive eavesdropping, since an attacker cannot compute the shared secret without knowing one of the private keys.
- Secure and efficient key establishment suitable for constrained devices like microcontrollers.


### 🔒 Symmetric Encryption: AES-128

After the shared secret is derived:

- An AES-128 key is generated from the ECDH shared secret.
- Communication between master and server is encrypted using AES-128 in Electronic Codebook (ECB) mode.

This provides confidentiality for:

- RFID card IDs  
- PIN values  
- Authentication requests  

Although ECB does not provide semantic security for large structured data, in this project it is applied to fixed-size authentication payloads, reducing pattern leakage risks.

---

## 🏃‍♂️ Authentication Flow

The complete secure process is:

1. User scans RFID card.
2. Slave encrypts the card ID using ChaCha20-Poly1305 and sends it to the master.
3. Master verifies integrity and decrypts.
4. Master performs ECDH with the server (if a session is not already established).
5. Master encrypts the RFID ID with AES-128 and sends it to the server.
6. Server checks whether the RFID exists in MongoDB.
7. If valid, the system requests the PIN from the user.
8. Slave encrypts the PIN and sends it securely to the master.
9. Master encrypts the PIN using AES-128 and forwards it to the server.
10. Server verifies the PIN against the stored value.
11. Master receives the result and activates:
    - LCD display message  
    - Status LEDs  
    - Buzzer feedback  

---

## 🗄️ Database Design (MongoDB)

The MongoDB database stores user credentials using a simple structure:

- rfid_id  
- pin  

The server handles:

- Card existence validation  
- PIN verification  
- Authentication response generation  

## Security Design Rationale

SafeGate applies layered security principles.

### Embedded Layer Protection

I²C communication is protected using ChaCha20-Poly1305 to secure local hardware communication against physical interception and tampering.

### 🌐 Network Layer Protection

WiFi communication is protected using:

- ECDH for secure key establishment  
- AES-128 for payload encryption  

This layered approach ensures:

- Compromise of the local bus does not expose server credentials.  
- Network interception does not reveal authentication data.  
- Symmetric keys are never transmitted in plaintext.  

---

## ⚠️ Threat Model Considerations

SafeGate mitigates:

- I²C sniffing  
- Message tampering  
- Replay attacks  
- WiFi eavesdropping  
- Passive interception during key establishment  


