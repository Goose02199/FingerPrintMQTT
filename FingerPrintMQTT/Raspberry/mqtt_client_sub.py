import paho.mqtt.client as mqtt

broker_address = "localhost"  # broker en la misma Raspberry Pi
topic_sub = "esp32/to/server/actions"

def on_connect(client, userdata, flags, rc):
    print("Conectado con código "+str(rc))
    client.subscribe(topic_sub)

def on_message(client, userdata, msg):
    message = msg.payload.decode()
    print(f"Mensaje recibido: {message}")

client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

client.connect(broker_address, 1883, 60)
client.loop_forever()
