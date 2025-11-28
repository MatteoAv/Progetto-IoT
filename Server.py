import socket
import json
import time
from dotenv import load_dotenv
import os
import certifi
from pymongo.mongo_client import MongoClient

# ---- Connessione a MongoDB ----
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
HOST = "0.0.0.0"
PORT = 5000

print("[SERVER] In ascolto su porta", PORT)

utente_corrente = None  # memorizza temporaneamente l’utente dopo la lettura della carta

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.bind((HOST, PORT))
    s.listen(5)

    while True:
        conn, addr = s.accept()
        print(f"[SERVER] Connessione da {addr}")

        with conn:
            while True:
                data = conn.recv(1024)
                if not data:
                    break

                try:
                    msg = json.loads(data.decode('utf-8').strip())
                except json.JSONDecodeError:
                    print("[SERVER] JSON non valido:", data)
                    continue

                tipo = msg.get("type")
                valore = msg.get("value")

                risposta = {}

                # --- Gestione carta ---
                if tipo == "card":
                    utente_corrente = collection.find_one({"card_id": valore})
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
                        if valore == utente_corrente["pin"]:
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

                # Invia risposta JSON al client
                conn.sendall((json.dumps(risposta) + "\n").encode('utf-8'))
