import serial
import time

PIN_CORRETTO_SERVER = "1234"
CARTA_CORRETTA = "E27AEB1B"
PREFIX_PIN = "PIN_INSERITO:"
PREFIX_CARD = "CARD_ID:"

master = serial.Serial("COM6", 9600, timeout=0.1)
time.sleep(2)

print("Pronto a ricevere")

while True:
    data = master.readline()
    data_pulito = data.decode('utf-8').strip()

    if data_pulito:
        print(f"Ricevuto: {data_pulito}")

        if data_pulito.startswith(PREFIX_CARD):
            # Gestione verifica carta
            card_id = data_pulito.replace(PREFIX_CARD, "")
            print(f"ID Carta ricevuto: {card_id}")

            if card_id == CARTA_CORRETTA:
                comando_risposta = "CARTA_VALIDA\n"
                print("Status: Carta Valida. Invio comando per richiedere PIN.")
            else:
                comando_risposta = "CARTA_NON_VALIDA\n"
                print("Status: Carta Non Valida. Accesso negato.")

            master.write(comando_risposta.encode('utf-8'))

        elif data_pulito.startswith(PREFIX_PIN):
            # Gestione verifica PIN (come prima)
            pin_inserito = data_pulito.replace(PREFIX_PIN, "")
            print(f"PIN ricevuto: {pin_inserito}")

            if pin_inserito == PIN_CORRETTO_SERVER:
                comando_risposta = "ACCESSO_CONCESSO\n"
                print("Status: Accesso Consentito. Invio il comando ad Arduino.")
            else:
                comando_risposta = "ACCESSO_NEGATO\n"
                print("Status: PIN Errato. Invio il comando di Negazione ad Arduino.")

            master.write(comando_risposta.encode('utf-8'))

    time.sleep(0.01)