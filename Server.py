import serial
import time
from dotenv import load_dotenv
import os
import certifi
from pymongo.mongo_client import MongoClient

load_dotenv()
port = os.getenv('SERIAL_PORT')
SERIAL_PORT = os.getenv("SERIAL_PORT")
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
        print(f"Ricevuto: {data_pulito}")

        # --- Gestione carta ---
        if data_pulito.startswith(PREFIX_CARD):
            card_id = data_pulito.replace(PREFIX_CARD, "")
            print(f"ID Carta ricevuto: {card_id}")

            # Ricerca nel database
            utente_corrente = collection.find_one({"card_id": card_id})

            if utente_corrente:
                comando_risposta = "CARTA_VALIDA\n"
                print("Status: Carta Valida. Invio comando per richiedere PIN.")
            else:
                comando_risposta = "CARTA_NON_VALIDA\n"
                utente_corrente = None  # nessun utente valido
                print("Status: Carta Non Valida. Accesso negato.")

            master.write(comando_risposta.encode('utf-8'))

        # --- Gestione PIN ---
        elif data_pulito.startswith(PREFIX_PIN):
            pin_inserito = data_pulito.replace(PREFIX_PIN, "")
            print(f"PIN ricevuto: {pin_inserito}")

            if utente_corrente:
                if pin_inserito == utente_corrente["pin"]:
                    # Usa i dati dal db per salutare l'utente
                    nome = utente_corrente.get("nome", "")
                    cognome = utente_corrente.get("cognome", "")
                    comando_risposta = f"BENVENUTO {nome} {cognome}\n"
                    print(f"Status: Accesso Consentito. Messaggio inviato ad Arduino: {comando_risposta.strip()}")
                else:
                    comando_risposta = "ACCESSO_NEGATO\n"
                    print("Status: PIN Errato. Invio il comando di Negazione ad Arduino.")
            else:
                comando_risposta = "ACCESSO_NEGATO\n"
                print("Status: Nessuna carta valida letta prima del PIN.")

            master.write(comando_risposta.encode('utf-8'))

    time.sleep(0.01)
