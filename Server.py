import socket
import json
import os
import certifi
from dotenv import load_dotenv
from pymongo.mongo_client import MongoClient
from Crypto.Cipher import AES
from Crypto.Util.Padding import pad, unpad

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

# ---- Crittografia AES-ECB ----
# La stessa chiave usata nell'Arduino
AES_KEY = b'\x6C\x61\x43\x68\x69\x61\x76\x65\x53\x65\x67\x72\x65\x74\x61\x31'

# Funzione per cifrare
def encrypt_aes(plaintext):
    cipher = AES.new(AES_KEY, AES.MODE_ECB)                         # Crea un oggetto AES, con chiave e modalità di cifratura
    padded_data = pad(plaintext.encode('utf-8'), AES.block_size)    # Aggiunge il padding
    encrypted = cipher.encrypt(padded_data)                         # Cifra il testo (la dimensione dei blocchi è già definita)
    return encrypted.hex()                                          # Trasforma i byte cifrati in una stringa

# Funzione per decifrare
def decrypt_aes(encrypted_hex):
    cipher = AES.new(AES_KEY, AES.MODE_ECB)                         # Crea un oggetto AES
    encrypted_bytes = bytes.fromhex(encrypted_hex)                  # Converte il testo cifrato da stringa a byte
    decrypted = cipher.decrypt(encrypted_bytes)                     # Decifra il testo
    unpadded = unpad(decrypted, AES.block_size)                     # Rimuove il padding
    return unpadded.decode('utf-8')                                 # Converte il testo in chiaro da byte a stringa

def gestisci_client(conn, addr):
    print(f"[SERVER] Connessione da {addr}")
    utente_corrente = None

    buffer = ""

    while True:
        try:
            data = conn.recv(1024)  # legge fino a 1024 byte dal client
            if not data:            # se non riceve nulla la connessione viene chiusa
                print(f"[SERVER] {addr} ha chiuso la connessione")
                break

            # aggiunge i dati letti al buffer
            buffer += data.decode('utf-8', errors='ignore')

            # divide ogni messaggio terminato da \n in righe complete
            while '\n' in buffer:
                line, buffer = buffer.split('\n', 1)
                line = line.strip()

                if line == "":
                    continue

                try:
                    # decifra il messaggio proveniente dall'Arduino
                    decrypted_line = decrypt_aes(line)
                    # lo converte in JSON
                    msg = json.loads(decrypted_line)
                    print(f"[SERVER] Messaggio decifrato: {msg}")
                except Exception as e:
                    print(f"[SERVER] Errore decifratura/JSON: {e}")
                    continue

                # identifica il tipo di messaggio
                tipo = msg.get("type")
                valore = msg.get("value")
                risposta = {}

                # --- Gestione carta ---
                if tipo == "card":
                    try:
                        utente_corrente = collection.find_one({"card_id": valore}) # cerca l'utente nel DB tramite la carta
                    except Exception as e:
                        print("Errore DB:", e)
                        risposta["status"] = "ERRORE_DB"                            # in caso di errore setta lo stato a ERRORE DB
                        encrypted_response = encrypt_aes(json.dumps(risposta))      # anche eventuali risposte di errore vengono cifrate
                        conn.sendall((encrypted_response + "\n").encode('utf-8'))   # per mantenere coerenza quando saranno inviate all'arduino
                        continue

                    if utente_corrente:
                        risposta["status"] = "CARTA_VALIDA" # se lo trova setta lo stato a CARTA VALIDA
                        print(f"Carta valida: {valore}")
                    else:
                        utente_corrente = None
                        risposta["status"] = "CARTA_NON_VALIDA" # altrimeni a CARTA NON VALIDA
                        print(f"Carta non valida: {valore}")

                # --- Gestione PIN ---
                elif tipo == "pin":
                    if utente_corrente:
                        if str(valore) == str(utente_corrente["pin"]): # confronta il pin ricevuto con quello nel DB associato all'utente
                            risposta["status"] = "ACCESSO_CONCESSO"    # se corretto setta lo stato ad ACCESSO CONCESSO
                            risposta["nome"] = utente_corrente["nome"]
                            risposta["cognome"] = utente_corrente["cognome"]
                            risposta["saldo"] = str(utente_corrente["saldo"])
                            print(f"PIN valido per {utente_corrente['nome']} {utente_corrente['cognome']}")
                        else:
                            risposta["status"] = "ACCESSO_NEGATO"       # se sbagliato setta lo stato ad ACCESSO NEGATO
                            print(f"PIN errato per {utente_corrente['nome']} {utente_corrente['cognome']}")
                    else:
                        risposta["status"] = "ACCESSO_NEGATO"
                        print("PIN ricevuto senza carta valida")

                else:
                    risposta["status"] = "ERRORE"
                    print("Tipo non riconosciuto:", tipo)

                # Dopo aver ottenuti i dati dal database li inviamo all'arduino per mostrarli sullo schermo LCD, prima però vanno cifrati
                encrypted_response = encrypt_aes(json.dumps(risposta))      # Converte il JSON, con i dati ottenuti, in una stringa che viene cifrata
                conn.sendall((encrypted_response + "\n").encode('utf-8'))   # Aggiunge un carattere \n alla fine della stringa e la converte in byte, inviando i byte al client
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

print(f"[SERVER] In ascolto su porta {PORT}")

# --- Loop principale ---
with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:    # Crea un socket TCP
    s.bind((HOST, PORT))                                        # Associa il socket all'indirizzo e alla porta del server
    s.listen(5)                                                 # Mette il socket in modalità ascolto per accettare connessioni
    print("[SERVER] Pronto ad accettare connessioni...")

    while True:
        conn, addr = s.accept()     # Blocca il programma finché un client non si connette al server,
                                    # quando si connette, "conn" indica il nuovo socket e "addr" l'indirizzo del client
        gestisci_client(conn, addr) # Le informazioni sono mandate alla funzione di gestione del client