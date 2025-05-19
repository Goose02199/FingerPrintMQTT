# Script publicador (cliente)
import paho.mqtt.client as mqtt
import threading

broker_address = "localhost"
topic_pub = "server/to/esp32"
topic_sub = "esp32/to/server/callback"

# Variables de control
last_message_sent = None
ack_received = threading.Event()
instruction_received = threading.Event()
last_instruction = None

# Callback cuando se recibe un mensaje del topic_sub
def on_message(client, userdata, msg):
    global last_message_sent, last_instruction

    payload = msg.payload.decode()
    print(f"[MQTT] Mensaje recibido: {payload}")

    # Verificar si es un ACK válido
    if payload.startswith("ACK: "):
        ack_content = payload[5:]
        if ack_content == last_message_sent:
            print("ACK válido recibido.")
        else:
            print("ACK inválido: no coincide con el mensaje enviado.")
        ack_received.set()  # Señal para continuar

    else:
        # Instrucción final
        last_instruction = payload
        instruction_received.set()

# Callback de conexión
def on_connect(client, userdata, flags, rc):
    print("Conectado con código", rc)
    client.subscribe(topic_sub)

# Validar ID entre 1 y 127
def pedir_id():
    while True:
        try:
            id_str = input("Ingresa el ID (1-127): ")
            id_num = int(id_str)
            if 1 <= id_num <= 127:
                return str(id_num)
            else:
                print("El ID debe estar entre 1 y 127.")
        except ValueError:
            print("ID inválido. Ingresa un número.")

# Configurar cliente MQTT
client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message
client.connect(broker_address, 1883, 60)
client.loop_start()

# Bucle principal
try:
    while True:
        print("\nOpciones disponibles:")
        print("1. Capture (registrar huella)")
        print("2. Delete (borrar huella)")
        opcion = input("Elige una opción (Capture/Delete): ").strip().lower()

        if opcion == "capture":
            id_value = pedir_id()
            mensaje = "C" + id_value
        elif opcion == "delete":
            id_value = pedir_id()
            mensaje = "D" + id_value
        else:
            print("Opción inválida. Escribe 'Capture' o 'Delete'.")
            continue

        # Publicar mensaje y esperar respuestas
        last_message_sent = mensaje
        ack_received.clear()
        instruction_received.clear()

        client.publish(topic_pub, mensaje)
        print(f"Enviado: {mensaje}")

        print("Esperando ACK...")
        ack_received.wait()

        print("Esperando instrucción final...")
        instruction_received.wait()
        print(f"Instrucción final: {last_instruction}")

except KeyboardInterrupt:
    print("\nFinalizando cliente...")
    client.loop_stop()
    client.disconnect()

