# app.py (Backend Python)
from flask import Flask, render_template, request, jsonify, Response
from flask_sqlalchemy import SQLAlchemy #base de datos
from datetime import datetime
from models import Deteccion, db  
import paho.mqtt.client as mqtt
import threading
import time
import queue
import json

app = Flask(__name__)

# Configuración de la base de datos
app.config['SQLALCHEMY_DATABASE_URI'] = 'sqlite:///huellas.db'  # Usamos SQLite
app.config['SQLALCHEMY_TRACK_MODIFICATIONS'] = False

# Inicialización de la base de datos
db.init_app(app)

# Crear la base de datos
with app.app_context():
    db.create_all()

# Registrar huella en la base de datos
from datetime import datetime

# Configuración MQTT
MQTT_BROKER = "localhost"
MQTT_PORT = 1883
TOPIC_SUB = "esp32/to/server/actions"
TOPIC_PUB = "server/to/esp32"

# Estado global
current_status = {
    'state': 'waiting',
    'id': None,
    'confidence': None
}

# Configuración MQTT
def on_connect(client, userdata, flags, rc):
    print("Conectado al broker MQTT")
    client.subscribe(TOPIC_SUB)

def on_message(client, userdata, msg):
    payload = msg.payload.decode()
    print("Mensaje MQTT recibido:", payload)
    
    if "ID detectado" in payload:  # Si el mensaje contiene la detección
        parts = payload.split(":")
        fid = parts[1].strip()  # Obtener el ID de la huella
        current_status['state'] = 'approved'
        current_status['id'] = fid
        
        # Usar el contexto de la aplicación para realizar operaciones de base de datos
        with app.app_context():  # Esto asegura que estamos dentro del contexto de Flask
            # Registrar la detección en la base de datos con la fecha de detección
            if fid.isdigit():
                # Verificar si ya existe un registro sin fecha (para el mismo huella_id)
                huella_a_actualizar = Deteccion.query.filter_by(huella_id=int(fid), fecha=None).first()

                if huella_a_actualizar:
                    # Si existe, actualizamos la fecha de detección
                    huella_a_actualizar.fecha = datetime.now()
                    db.session.commit()  # Guardar el cambio en la base de datos
                    print(f"Fecha de detección para huella_id {fid} actualizada a {datetime.now()}")
                else:
                    # Si no existe un registro sin fecha, podemos agregar uno nuevo si es necesario
                    nueva_deteccion = Deteccion(huella_id=int(fid), fecha=datetime.now())
                    db.session.add(nueva_deteccion)  # Añadir el nuevo registro
                    db.session.commit()  # Confirmar el nuevo registro en la base de datos
                    print(f"Nuevo registro de huella_id {fid} añadido con fecha de detección {datetime.now()}")
            else:
                print("ID de huella no válido")
    else:
        current_status['state'] = 'rejected'
    
    # Notificar a los clientes SSE con el estado actualizado
    send_event(current_status)


# Cliente MQTT
mqtt_client = mqtt.Client()
mqtt_client.on_connect = on_connect
mqtt_client.on_message = on_message

# Eventos SSE
clients = []

def send_event(data):
    for client in clients:
        client.put(data)

# Hilo MQTT
def mqtt_thread():
    mqtt_client.connect(MQTT_BROKER, MQTT_PORT)
    mqtt_client.loop_forever()

@app.route('/')
def index():
    # Obtener las últimas 5 detecciones
    detecciones = Deteccion.query.order_by(Deteccion.fecha.desc()).limit(5).all()
    
    # Renderizar el template y pasar las detecciones
    return render_template('index.html', detecciones=detecciones)


@app.route('/status')
def get_status():
    return jsonify(current_status)

@app.route('/stream')
def stream():
    def event_stream():
        client = queue.Queue(maxsize=5)
        clients.append(client)
        try:
            while True:
                data = client.get()
                yield f"data: {json.dumps(data)}\n\n"
        finally:
            clients.remove(client)
    return Response(event_stream(), mimetype="text/event-stream")

@app.route('/register', methods=['POST'])
def register():
    fid = request.form.get('id')
    if fid and fid.isdigit():
        # Registra el comando MQTT
        mqtt_client.publish(TOPIC_PUB, f"C{fid.zfill(3)}")
        
        # Inserta en la base de datos
        nueva_deteccion = Deteccion(huella_id=int(fid), fecha=datetime.now())
        db.session.add(nueva_deteccion)
        db.session.commit()

        return jsonify(success=True, message="Comando de registro enviado y huella registrada")
    return jsonify(success=False, message="ID inválido")

@app.route('/delete', methods=['POST'])
def delete():
    fid = request.form.get('id')
    
    if fid and fid.isdigit():
        # Publicar el comando MQTT para eliminar la huella
        mqtt_client.publish(TOPIC_PUB, f"D{fid.zfill(3)}")
        
        # Eliminar de la base de datos
        huella_a_eliminar = Deteccion.query.filter_by(huella_id=int(fid)).first()
        
        if huella_a_eliminar:
            db.session.delete(huella_a_eliminar)  # Eliminar el registro
            db.session.commit()  # Confirmar cambios en la base de datos
            return jsonify(success=True, message="Comando de eliminación enviado y huella eliminada de la base de datos")
        else:
            return jsonify(success=False, message="ID no encontrado en la base de datos")
    
    return jsonify(success=False, message="ID inválido")

# Seccion de tabla
@app.route('/all_detections', methods=['GET', 'POST'])
def all_detections():
    huella_id = request.args.get('id')  # Obtener el ID a filtrar desde la URL
    
    if huella_id:
        # Filtrar detecciones por ID de huella
        detecciones = Deteccion.query.filter_by(huella_id=huella_id).all()
    else:
        # Obtener todas las detecciones
        detecciones = Deteccion.query.all()

    return render_template('all_detections.html', detecciones=detecciones)

if __name__ == '__main__':
    mqtt_thread = threading.Thread(target=mqtt_thread)
    mqtt_thread.daemon = True
    mqtt_thread.start()
    # app.run(host='0.0.0.0', port=5000, debug=True)
    app.run(host='0.0.0.0', port=5000, debug=False, use_reloader=False)

