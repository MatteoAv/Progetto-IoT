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

card_id_corrente = None  # memorizza temporaneamente l'ultima carta letta

while True:
    data = master.readline()
    data_pulito = data.decode('utf-8').strip()

    if data_pulito:
        print(f"Ricevuto: {data_pulito}")

        # --- Gestione carta ---
        if data_pulito.startswith(PREFIX_CARD):
            card_id_corrente = data_pulito.replace(PREFIX_CARD, "")
            print(f"ID Carta ricevuto: {card_id_corrente}")

            # Controllo se la carta esiste nel database
            utente = collection.find_one({"card_id": card_id_corrente})
            if utente:
                comando_risposta = "CARTA_VALIDA\n"
                print("Status: Carta Valida. Invio comando per richiedere PIN.")
            else:
                comando_risposta = "CARTA_NON_VALIDA\n"
                card_id_corrente = None  # nessuna carta valida
                print("Status: Carta Non Valida. Accesso negato.")

            master.write(comando_risposta.encode('utf-8'))

        # --- Gestione PIN ---
        elif data_pulito.startswith(PREFIX_PIN):
            pin_inserito = data_pulito.replace(PREFIX_PIN, "")
            print(f"PIN ricevuto: {pin_inserito}")

            if card_id_corrente:
                # Cerca direttamente l'utente con card_id e pin
                utente = collection.find_one({"card_id": card_id_corrente, "pin": pin_inserito})

                if utente:
                    nome = utente.get("nome", "")
                    cognome = utente.get("cognome", "")
                    comando_risposta = f"BENVENUTO {nome} {cognome}\n"
                    print(f"Status: Accesso Consentito. Messaggio inviato ad Arduino: {comando_risposta.strip()}")
                else:
                    comando_risposta = "ACCESSO_NEGATO\n"
                    print("Status: Carta o PIN non validi. Invio ACCESSO_NEGATO ad Arduino.")
            else:
                comando_risposta = "ACCESSO_NEGATO\n"
                print("Status: Nessuna carta valida letta prima del PIN.")

            master.write(comando_risposta.encode('utf-8'))

    time.sleep(0.01)
