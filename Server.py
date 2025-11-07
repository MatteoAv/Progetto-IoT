import serial
import time

PIN_CORRETTO_SERVER = "1234"
PREFIX_PIN = "PIN_INSERITO:"

master = serial.Serial("COM5", 9600, timeout=0.1)
time.sleep(2)

print("Pronto a ricevere")

while True:
    pin = master.readline()
    pin_pulito = pin.decode('utf-8').strip()

    if pin_pulito:
        print(pin_pulito)

        if pin_pulito.startswith(PREFIX_PIN):
            pin_inserito = pin_pulito.replace(PREFIX_PIN, "")

            if pin_inserito == PIN_CORRETTO_SERVER:
                comando_risposta = "ACCESSO_CONCESSO\n"
                print("Status: Accesso Concesso. Invio il comando ad Arduino.")
            else:
                comando_risposta = "ACCESSO_NEGATO\n"
                print("Status: PIN Errato. Invio il comando di Negazione ad Arduino.")

            master.write(comando_risposta.encode('utf-8'))

    time.sleep(0.01)