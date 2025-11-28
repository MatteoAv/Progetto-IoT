import socket
import json
import os
import certifi
from dotenv import load_dotenv
from pymongo.mongo_client import MongoClient

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

# ---- Socket Server ----
HOST = "0.0.0.0"  # ascolta su tutte le interfacce
PORT = 5000

print(f"[SERVER] In ascolto su porta {PORT}")

# --- Mantieni le connessioni attive
clients = []

# Funzione per gestire un singolo client
def gestisci_client(conn, addr):
    print(f"[SERVER] Connessione da {addr}")
    utente_corrente = None

    buffer = ""

    while True:
        try:
            data = conn.recv(1024)
            print(data)
            if not data:
                print(f"[SERVER] {addr} ha chiuso la connessione")
                break

            buffer += data.decode('utf-8', errors='ignore')

            # gestisci eventuali righe complete
            while '\n' in buffer:
                line, buffer = buffer.split('\n', 1)
                line = line.strip()

                if line == "":
                    continue  # ignora righe vuote

                try:
                    msg = json.loads(line)
                except json.JSONDecodeError:
                    print(f"[SERVER] JSON non valido: {line}")
                    continue

                tipo = msg.get("type")
                valore = msg.get("value")
                risposta = {}

                # --- Gestione carta ---
                if tipo == "card":
                    try:
                        utente_corrente = collection.find_one({"card_id": valore})
                    except Exception as e:
                        print("Errore DB:", e)
                        risposta["status"] = "ERRORE_DB"
                        conn.sendall((json.dumps(risposta) + "\n").encode('utf-8'))
                        continue

                    if utente_corrente:
                        risposta["status"] = "CARTA_VALIDA"
                        print(f"Carta valida: {valore}")
                    else:
                        utente_corrente = None
                        risposta["status"] = "CARTA_NON_VALIDA"
                        print(f"Carta non valida: {valore}")

                # --- Gestione PIN ---
                elif tipo == "pin":
                    if utente_corrente:
                        if str(valore) == str(utente_corrente["pin"]):
                            risposta["status"] = "ACCESSO_CONCESSO"
                            risposta["nome"] = utente_corrente["nome"]
                            risposta["cognome"] = utente_corrente["cognome"]
                            risposta["saldo"] = str(utente_corrente["saldo"])
                            print(f"PIN valido per {utente_corrente['nome']} {utente_corrente['cognome']}")
                        else:
                            risposta["status"] = "ACCESSO_NEGATO"
                            print(f"PIN errato per {utente_corrente['nome']} {utente_corrente['cognome']}")
                    else:
                        risposta["status"] = "ACCESSO_NEGATO"
                        print("PIN ricevuto senza carta valida")

                else:
                    risposta["status"] = "ERRORE"
                    print("Tipo non riconosciuto:", tipo)

                # invia risposta al client
                conn.sendall((json.dumps(risposta) + "\n").encode('utf-8'))

        except ConnectionResetError:
            print(f"[SERVER] {addr} ha chiuso bruscamente la connessione")
            break
        except Exception as e:
            print(f"[SERVER] Errore sconosciuto con {addr}: {e}")
            break

    conn.close()


# --- Loop principale ---
with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.bind((HOST, PORT))
    s.listen(5)
    print("[SERVER] Pronto ad accettare connessioni...")

    while True:
        conn, addr = s.accept()
        gestisci_client(conn, addr)
