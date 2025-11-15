import serial
import time
from dotenv import load_dotenv
import os
import certifi
from pymongo.mongo_client import MongoClient

load_dotenv()
port = os.getenv('SERIAL_PORT')
DB_USERNAME = os.getenv("DB_USERNAME")
DB_PASSWORD = os.getenv("DB_PASSWORD")
DB_CLUSTER = os.getenv("DB_CLUSTER")
DB_NAME = os.getenv("DB_NAME")

uri = f"mongodb+srv://{DB_USERNAME}:{DB_PASSWORD}@{DB_CLUSTER}/?appName=ClusterArduino"
client = MongoClient(uri, tlsCAFile=certifi.where())

db = client[DB_NAME]
collection = db['utenti']

PREFIX_PIN = "PIN_INSERITO:"
PREFIX_CARD = "CARD_ID:"

master = serial.Serial(port, 9600, timeout=0.1)
time.sleep(2)

print("Pronto a ricevere")

utente_corrente = None  # memorizza temporaneamente l'utente dopo la lettura della carta

while True:
    data = master.readline()
    data_pulito = data.decode('utf-8').strip()

    if data_pulito:

        # --- Gestione carta ---
        if data_pulito.startswith(PREFIX_CARD):
            card_id = data_pulito.replace(PREFIX_CARD, "")
            print(f"Inserita carta con ID: {card_id}")

            # Ricerca nel database
            utente_corrente = collection.find_one({"card_id": card_id})

            ## Caso in cui nel database è registrata la carta
            if utente_corrente:
                comando_risposta = "CARTA_VALIDA\n"
                print("Carta valida")

            ## Caso in cui nel database non è registrata la carta
            else:
                comando_risposta = "CARTA_NON_VALIDA\n"
                utente_corrente = None
                print("Carta non valida")

            master.write(comando_risposta.encode('utf-8')) ## Invia all'auduino se la carta è valida o meno

        # --- Gestione PIN ---
        elif data_pulito.startswith(PREFIX_PIN):
            pin_inserito = data_pulito.replace(PREFIX_PIN, "")
            print(f"Inserito PIN: {pin_inserito}")

            if utente_corrente:

                   ## Caso in cui il pin della carta è corretto
                if pin_inserito == utente_corrente["pin"]:
                    comando_risposta = (
                        f"ACCESSO_CONCESSO:"
                        f"{utente_corrente['nome']}|"
                        f"{utente_corrente['cognome']}|"
                        f"{utente_corrente['saldo']}\n"
                    )

                    print("Pin valido")
                    print(f"Accesso concesso utente:{utente_corrente['nome']} {utente_corrente['cognome']}\n")
                else:

                    ## Caso in cui il pin della carta non è corretto
                    comando_risposta = "ACCESSO_NEGATO:\n"
                    print("Pin non valido")
            else:
                comando_risposta = "ACCESSO_NEGATO:NOME_NON_DISPONIBILE\n"
                print("Status: Nessuna carta valida letta prima del PIN.")

            master.write(comando_risposta.encode('utf-8'))

    time.sleep(0.01)
