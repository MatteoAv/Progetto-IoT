import socket
import json
import os
import certifi
from dotenv import load_dotenv
from pymongo.mongo_client import MongoClient
from Crypto.Cipher import AES
from Crypto.Util.Padding import pad, unpad
import hashlib
from nacl.public import PrivateKey
from nacl.bindings import crypto_scalarmult

# ---- Caricamento variabili ambiente ----
load_dotenv()

DB_USERNAME = os.getenv("DB_USERNAME")
DB_PASSWORD = os.getenv("DB_PASSWORD")
DB_CLUSTER = os.getenv("DB_CLUSTER")
DB_NAME = os.getenv("DB_NAME")

uri = f"mongodb+srv://{DB_USERNAME}:{DB_PASSWORD}@{DB_CLUSTER}/?appName=ClusterArduino"
client = MongoClient(uri, tlsCAFile=certifi.where())
db = client[DB_NAME]
collection = db['utenti']


# ---- Funzioni AES ----
def encrypt_aes(plaintext, aes_key):
    cipher = AES.new(aes_key, AES.MODE_ECB)                         # Crea un oggetto AES, con chiave e modalità di cifratura
    padded_data = pad(plaintext.encode('utf-8'), AES.block_size)    # Aggiunge il padding
    return cipher.encrypt(padded_data).hex()                        # Cifra il testo (la dimensione dei blocchi è già definita) e restituisce il cifrato convertito da byte a stringa


def decrypt_aes(encrypted_hex, aes_key):
    cipher = AES.new(aes_key, AES.MODE_ECB)                         # Crea un oggetto AES
    decrypted = cipher.decrypt(bytes.fromhex(encrypted_hex))        # Converte il testo cifrato da stringa a byte e lo decifra
    return unpad(decrypted, AES.block_size).decode('utf-8')         # Rimuove il padding, riconverte il testo in chiaro da byte a stringa e lo restituisce


# ---- Funzione gestione client ----
def gestisci_client(conn, addr):
    print(f"[SERVER] Connessione da {addr}")

    # --- Genera coppia DH server ---
    server_priv = PrivateKey.generate().encode()
    server_pub = PrivateKey(server_priv).public_key.encode()

    # --- Ricezione chiave pubblica del client ---
    try:
        # Leggi fino al primo newline (può includere \r\n)
        data = b""
        while True:
            chunk = conn.recv(1)
            if not chunk:
                raise ConnectionError("Connessione chiusa prima della chiave")
            data += chunk
            if data.endswith(b'\n'):
                break

        # Rimuove eventuali \r e \n dalla fine
        data = data.strip()
        print(f"[SERVER] Ricevuto raw: {data}")

        msg_json = json.loads(data.decode())
        if msg_json["type"] != "pubkey" or len(msg_json["value"]) != 64:
            raise ValueError("Chiave pubblica client invalida")
        client_pub_bytes = bytes.fromhex(msg_json["value"])
        print(f"[SERVER] Chiave pubblica client ricevuta: {msg_json['value'][:16]}...")
    except Exception as e:
        print(f"[SERVER] Errore ricezione chiave client: {e}")
        conn.close()
        return

    # --- Invio chiave pubblica server al client ---
    response = json.dumps({"type": "pubkey", "value": server_pub.hex()}) + "\n"
    conn.sendall(response.encode())
    print(f"[SERVER] Chiave pubblica server inviata: {server_pub.hex()[:16]}...")

    # --- Calcola shared secret e AES key ---
    try:
        shared_secret = crypto_scalarmult(server_priv, client_pub_bytes)
        aes_key = shared_secret[:16]  # AES-128
        print(f"[SERVER] AES key derivata: {aes_key.hex()}")
    except Exception as e:
        print(f"[SERVER] Errore calcolo shared secret: {e}")
        conn.close()
        return

    utente_corrente = None
    buffer = b""

    # --- Loop principale per messaggi cifrati ---
    while True:
        try:
            data = conn.recv(1024)
            if not data:
                print(f"[SERVER] {addr} ha chiuso la connessione")
                break

            buffer += data

            while b'\n' in buffer:
                line, buffer = buffer.split(b'\n', 1)
                line = line.strip()
                if not line:
                    continue

                # --- Decifra e parse JSON ---
                try:
                    # Converti in stringa hex prima di decifrare
                    encrypted_hex = line.decode('ascii')
                    decrypted_line = decrypt_aes(encrypted_hex, aes_key)
                    msg = json.loads(decrypted_line)
                    print(f"[SERVER] Messaggio decifrato: {msg}")
                except Exception as e:
                    print(f"[SERVER] Errore decifratura/JSON: {e}")
                    continue

                # identifica il tipo di messaggio
                tipo = msg.get("type")
                valore = msg.get("value")
                valore_hash = hashlib.sha256(valore.encode()).hexdigest() #calcolo l'hash del messaggio inviato in modo da confrontarlo con l'hash presente sul db
                risposta = {}

                # --- Gestione carta ---
                if tipo == "card":
                    utente_corrente = collection.find_one({"card_id": valore_hash}) # cerca l'utente nel DB tramite la carta
                    if utente_corrente:
                        risposta["status"] = "CARTA_VALIDA" # se lo trova setta lo stato a CARTA VALIDA
                        print(f"Carta valida: {valore_hash}")
                    else:
                        utente_corrente = None
                        risposta["status"] = "CARTA_NON_VALIDA" # altrimeni a CARTA NON VALIDA
                        print(f"Carta non valida: {valore_hash}")

                # --- Gestione PIN ---
                elif tipo == "pin":
                    if utente_corrente and valore_hash == utente_corrente["pin"]:   # confronta il pin ricevuto con quello nel DB associato all'utente
                        risposta["status"] = "ACCESSO_CONCESSO"                     # se corretto setta lo stato ad ACCESSO CONCESSO
                        risposta["nome"] = utente_corrente["nome"]
                        risposta["cognome"] = utente_corrente["cognome"]
                        risposta["saldo"] = str(utente_corrente["saldo"])
                        print(f"PIN corretto per {utente_corrente['nome']} {utente_corrente['cognome']}")
                    else:
                        risposta["status"] = "ACCESSO_NEGATO"           # se sbagliato setta lo stato ad ACCESSO NEGATO
                        print("PIN errato o carta non valida")

                else:
                    risposta["status"] = "ERRORE"
                    print("Tipo non riconosciuto:", tipo)

                # Dopo aver ottenuti i dati dal database li inviamo all'arduino per mostrarli sullo schermo LCD, prima però vanno cifrati
                encrypted_response = encrypt_aes(json.dumps(risposta), aes_key) # Converte il JSON, con i dati ottenuti, in una stringa che viene cifrata
                conn.sendall((encrypted_response + "\n").encode())              # Aggiunge un carattere \n alla fine della stringa e la converte in byte, inviando i byte al client
                print(f"[SERVER] Risposta inviata: {risposta}")

        except ConnectionResetError:
            print(f"[SERVER] {addr} ha chiuso bruscamente la connessione")
            break
        except Exception as e:
            print(f"[SERVER] Errore sconosciuto con {addr}: {e}")
            break

    conn.close()


# ---- Socket Server ----
HOST = "0.0.0.0"
PORT = 5000

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:        # Crea un socket TCP
    s.bind((HOST, PORT))                                            # Associa il socket all'indirizzo e alla porta del server
    s.listen(5)                                                     # Mette il socket in modalità ascolto per accettare connessioni
    print(f"[SERVER] In ascolto su porta {PORT}")
    print("[SERVER] Pronto ad accettare connessioni...")

    while True:
        conn, addr = s.accept()         # Blocca il programma finché un client non si connette al server,
                                        # quando si connette, "conn" indica il nuovo socket e "addr" l'indirizzo del client
        gestisci_client(conn, addr)     # Le informazioni sono mandate alla funzione di gestione del client