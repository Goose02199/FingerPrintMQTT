#include <Wire.h>
#include <WiFiManager.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_Fingerprint.h>
#include <PubSubClient.h>

#define mySerial Serial2  // Sensor: Usará RX2 (GPIO16) y TX2 (GPIO17)
#define BUTTON_PIN 4            // GPIO para pasar a modo AP (wifi-connect)
#define HOLD_TIME 5000         // Tiempo que debe mantenerse presionado (ms)

WiFiManager wm;

LiquidCrystal_I2C lcd(0x27, 16, 2);  // Dirección I2C, 16 columnas y 2 filas

unsigned long buttonPressStart = 0;
bool buttonHeld = false;
bool portalActivo = false;

bool fConfig = false;
uint8_t id;
uint8_t rBuff;
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

// Configuración del broker MQTT
const char* mqtt_server = "192.168.50.114";  // IP del broker (ej. Raspberry Pi)
const int mqtt_port = 1883;
const char* topic_sub = "server/to/esp32";
const char* topic_pub = "esp32/to/server/callback";
const char* topic_pub2 = "esp32/to/server/actions";

String instruccion = " "; //instrucción recibe del broker

WiFiClient espClient;
PubSubClient client(espClient);

void verificarBoton();                   //Función para verificar acción del botón
void verificarMQTT();                    //Función para verificar conexión MQTT
void procesarInstruccion();              //Función para procesar intrucción que se recibe del broker
void manejarCapturaHuella();             //Función para manejar la captura de huellas y su notificación al broker
uint8_t readnumber(void);                //Función para leer un número
void mostrarLCD(const String& linea1, const String& linea2);  //Función para imprimir en el LCD
uint8_t deleteFingerprint(uint8_t id);   //Función para eliminar huella
uint8_t getFingerprintID();              //Función para capturar huella
int getFingerprintIDez();                //Función rápida para capturar huella
void callback(char* topic, byte* payload, unsigned int length); // Función cuando se recibe un mensaje
void reconnect();                        //Función para reconectar al broker

void setup() {
  Wire.begin(); // Inicia la comunicación I2C
  lcd.begin(16, 2);   // Inicializa el LCD
  lcd.backlight();  // Activa el backlight del LCD

  Serial.begin(115200);

  while (!Serial);  // Espera a que se inicie el serial
  delay(100);

  Serial.println("\n\nAdafruit lectura de huella");

  // Inicializa Serial2 con pines específicos para ESP32
  mySerial.begin(57600, SERIAL_8N1, 16, 17); // RX=16, TX=17
  
  if (finger.verifyPassword()) {
    Serial.println("Sensor encontrado");
    mostrarLCD("Sensor conectado","Iniciando..."); 
  } else {
    Serial.println("No se detectó ningún sensor");
    mostrarLCD("Error: sensor","no encontrado");
    while (1) { delay(1); }
  }

  pinMode(BUTTON_PIN, INPUT_PULLUP); //configuración de botón para desplegar portal de acceso

  // Intentar conexión automática a WiFi conocido
  bool res = wm.autoConnect("ESP32_Config", "12345678");
  if (!res) {
    Serial.println("No se pudo conectar, reiniciando...");
    mostrarLCD("Sin conexion","reiniciando");
    ESP.restart();
  } else {
    Serial.println("¡Conectado a WiFi!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    mostrarLCD("Conexion","establecida");
  }

  //configuración del cliente mqtt
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  //Lectura de paramentros del sensor
  Serial.println(F("Leyendo parametros del sensor..."));
  finger.getParameters();
  Serial.print(F("Status: 0x")); Serial.println(finger.status_reg, HEX);
  Serial.print(F("Sys ID: 0x")); Serial.println(finger.system_id, HEX);
  Serial.print(F("Capacity: ")); Serial.println(finger.capacity);
  Serial.print(F("Security level: ")); Serial.println(finger.security_level);
  Serial.print(F("Device address: ")); Serial.println(finger.device_addr, HEX);
  Serial.print(F("Packet len: ")); Serial.println(finger.packet_len);
  Serial.print(F("Baud rate: ")); Serial.println(finger.baud_rate);

  //Información de cantidad de huellas registradas
  finger.getTemplateCount();
  if (finger.templateCount == 0) {
    Serial.println("No hay huellas registradas");
  } else {
    Serial.println("Huellas resgistradas: "); 
    Serial.print(finger.templateCount); 
    Serial.println(" huellas");
  }
  delay(2000);
}

void loop() {
  verificarBoton();
  verificarMQTT();
  procesarInstruccion();
  manejarCapturaHuella();
  delay(50);
}

void verificarBoton() {
  int buttonState = digitalRead(BUTTON_PIN);

  if (buttonState == LOW) {
    if (buttonPressStart == 0) {
      buttonPressStart = millis();
    } else if ((millis() - buttonPressStart >= HOLD_TIME) && !portalActivo) {
      Serial.println("Botón mantenido 5s: abriendo portal de configuración");
      mostrarLCD("Abriendo portal", "ESP32_Config");

      portalActivo = true;
      WiFi.disconnect();
      delay(500);

      wm.startConfigPortal("ESP32_Config", "12345678");

      Serial.println("Portal cerrado. Reintentando conexión WiFi...");
      portalActivo = false;

      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Estamos conectados a WiFi.");
        Serial.print("IP local: ");
        Serial.println(WiFi.localIP());
        mostrarLCD("Portal cerrado", "conexion exitosa");
      } else {
        Serial.println("Sin conexión WiFi.");
        mostrarLCD("Portal cerrado", "conexion fallida");
      }

      delay(2000);
    }
  } else {
    buttonPressStart = 0;
  }
}

void verificarMQTT() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
}

void procesarInstruccion() {
  if (instruccion.length() >= 2 && isAlpha(instruccion[0])) {
    char comando = instruccion[0];
    String sub = instruccion.substring(1, 4);

    bool esNumerico = true;
    for (int i = 0; i < sub.length(); i++) {
      if (!isDigit(sub[i])) {
        esNumerico = false;
        break;
      }
    }

    id = sub.toInt();
    if (!esNumerico) {
      Serial.println("Error: ID no válido (debe ser numérico).");
      client.publish(topic_pub, "Error: ID no válido (debe ser numérico)");
    } else if (id < 1 || id > 127) {
      Serial.println("Error: ID fuera del rango (1-127).");
      client.publish(topic_pub, "Error: ID fuera del rango (1-127)");
    } else {
      if (comando == 'C') {
        fConfig = true;
      } else if (comando == 'D') {
        mostrarLCD("Interrupcion del", "servidor");
        Serial.println("Se va a borrar la huella con id: #" + String(id));
        delay(2000);
        deleteFingerprint(id);
      } else {
        Serial.println("Error: Comando desconocido.");
        client.publish(topic_pub, "Comando desconocido");
      }
    }
  } else if (instruccion == " ") {
    // Instrucción vacía
  } else {
    Serial.println("Instrucción incorrecta.");
    client.publish(topic_pub, "Instrucción incorrecta");
  }
}

void manejarCapturaHuella() {
  if (fConfig) {
    Serial.println("Se va a registrar la huella con id: #" + String(id));
    mostrarLCD("Iniciando modo", "registro...");
    delay(1000);
    Serial.print("Guardando ID ");
    Serial.println(id);
    while (!getFingerprintEnroll());
    fConfig = false;
    delay(2000);
  } else {
    getFingerprintID();
  }

  instruccion = " ";
}

uint8_t deleteFingerprint(uint8_t id) { 

  uint8_t p = -1;

  p = finger.deleteModel(id);

  if (p == FINGERPRINT_OK) {
    Serial.println("Deleted!");
    String mensaje = "Success: borrar ID: #" + String(id);
    client.publish(topic_pub, mensaje.c_str());
  } 
  else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    Serial.println("Communication error");
    client.publish(topic_pub, "Error: borrar");
  }
  else if (p == FINGERPRINT_BADLOCATION) {
    Serial.println("Could not delete in that location");
    client.publish(topic_pub, "Error: borrar");
  } 
  else if (p == FINGERPRINT_FLASHERR) {
    Serial.println("Error writing to flash");
    client.publish(topic_pub, "Error: borrar");
  } 
  else {
    Serial.print("Unknown error: 0x"); Serial.println(p, HEX);
    client.publish(topic_pub, "Error: borrar");
  }

  return p;
}

uint8_t getFingerprintID() {

  uint8_t p = finger.getImage();

  switch (p) {

    case FINGERPRINT_OK:

      Serial.println("Huella capturada");
      mostrarLCD("Huella capturada","retire su dedo");
      sleep(1);

      break;

    case FINGERPRINT_NOFINGER:

      Serial.println("Esperando huella");
      mostrarLCD("Esperando huella","");

      return p;

    case FINGERPRINT_PACKETRECIEVEERR:

      Serial.println("Communication error");
      mostrarLCD("Error:","comunicacion");

      return p;

    case FINGERPRINT_IMAGEFAIL:

      Serial.println("Imaging error");
      mostrarLCD("Error:","imagen");

      return p;

    default:

      Serial.println("Unknown error");
      mostrarLCD("Error:","desconocido");

      return p;

  }

  // OK success!

  p = finger.image2Tz();

  switch (p) {

    case FINGERPRINT_OK:

      Serial.println("Image converted");

      break;

    case FINGERPRINT_IMAGEMESS:

      Serial.println("Image too messy");

      return p;

    case FINGERPRINT_PACKETRECIEVEERR:

      Serial.println("Communication error");

      return p;

    case FINGERPRINT_FEATUREFAIL:

      Serial.println("Could not find fingerprint features");

      return p;

    case FINGERPRINT_INVALIDIMAGE:

      Serial.println("Could not find fingerprint features");

      return p;

    default:

      Serial.println("Unknown error");

      return p;

  }

  // OK converted!

  p = finger.fingerSearch();

  if (p == FINGERPRINT_OK) {

    Serial.println("Found a print match!");
    mostrarLCD("Huella","encontrada");

  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {

    Serial.println("Communication error");
    return p;

  } else if (p == FINGERPRINT_NOTFOUND) {

    Serial.println("Did not find a match");
    mostrarLCD("Huella","desconocida");
    delay(2000);
    return p;

  } else {

    Serial.println("Unknown error");

    return p;

  }

  delay(2000);

  // found a match!

  Serial.print("Found ID #"); Serial.print(finger.fingerID);

  Serial.print(" with confidence of "); Serial.println(finger.confidence);

  mostrarLCD("Id detectado:", "ID #" + String(finger.fingerID));

  String mensaje = "ID detectado: " + String(finger.fingerID);
  client.publish(topic_pub2, mensaje.c_str());

  delay(2000);

  return finger.fingerID;

}

int getFingerprintIDez() {

  uint8_t p = finger.getImage();

  if (p != FINGERPRINT_OK)  return -1;



  p = finger.image2Tz();

  if (p != FINGERPRINT_OK)  return -1;



  p = finger.fingerFastSearch();

  if (p != FINGERPRINT_OK)  return -1;



  // found a match!

  Serial.print("Found ID #"); Serial.print(finger.fingerID);

  Serial.print(" with confidence of "); Serial.println(finger.confidence);

  return finger.fingerID;

}

uint8_t getFingerprintEnroll() {
  delay(2000);

  int p = -1;

  Serial.print("Waiting for valid finger to enroll as #"); Serial.println(id);

  while (p != FINGERPRINT_OK) {

    p = finger.getImage();

    switch (p) {

    case FINGERPRINT_OK:

      Serial.println("Image taken");

      break;

    case FINGERPRINT_NOFINGER:

      mostrarLCD("Registro activo","esperando dedo");

      break;

    case FINGERPRINT_PACKETRECIEVEERR:

      Serial.println("Communication error");
      mostrarLCD("Error:","comunicacion");

      break;

    case FINGERPRINT_IMAGEFAIL:

      Serial.println("Imaging error");
      mostrarLCD("Captura fallida","retire su dedo");

      delay(2000);

      break;

    default:

      Serial.println("Unknown error");

      break;

    }

  }

  // OK success!

  p = finger.image2Tz(1);

  switch (p) {

    case FINGERPRINT_OK:

      Serial.println("Image converted");

      break;

    case FINGERPRINT_IMAGEMESS:

      Serial.println("Image too messy");
      mostrarLCD("Error: Image","too messy");
      client.publish(topic_pub, "Error: registrar"); 

      return p;

    case FINGERPRINT_PACKETRECIEVEERR:

      Serial.println("Communication error");
      mostrarLCD("Error:","Communication");
      client.publish(topic_pub, "Error: registrar"); 

      return p;

    case FINGERPRINT_FEATUREFAIL:

      Serial.println("Could not find fingerprint features");
      mostrarLCD("Error: fgprint","freatures");
      client.publish(topic_pub, "Error: registrar"); 

      return p;

    case FINGERPRINT_INVALIDIMAGE:

      Serial.println("Could not find fingerprint features");
      mostrarLCD("Error: invalid","image");
      client.publish(topic_pub, "Error: registrar"); 

      return p;

    default:

      Serial.println("Unknown error");
      mostrarLCD("Error:","desconocido");
      client.publish(topic_pub, "Error: registrar"); 

      return p;

  }

  Serial.println("Remove finger");
  mostrarLCD("Imagen capturada","retire su dedo");

  delay(2000);

  p = 0;

  while (p != FINGERPRINT_NOFINGER) {

    p = finger.getImage();

  }

  Serial.print("ID "); Serial.println(id);

  p = -1;

  Serial.println("Place same finger again");

  while (p != FINGERPRINT_OK) {

    p = finger.getImage();

    switch (p) {

    case FINGERPRINT_OK:

      Serial.println("Image taken");

      break;

    case FINGERPRINT_NOFINGER:

      mostrarLCD("Vuelva a poner","el mismo dedo");

      break;

    case FINGERPRINT_PACKETRECIEVEERR:

      Serial.println("Communication error");
      mostrarLCD("Error:","comunicacion");

      break;

    case FINGERPRINT_IMAGEFAIL:

      Serial.println("Imaging error");
      mostrarLCD("Captura fallida","retire su dedo");

      delay(2000);

      break;

    default:

      Serial.println("Unknown error");

      break;

    }

  }

  // OK success!

  p = finger.image2Tz(2);

  switch (p) {

    case FINGERPRINT_OK:

      Serial.println("Image converted");

      break;

    case FINGERPRINT_IMAGEMESS:

      Serial.println("Image too messy");
      mostrarLCD("Error: Image","too messy");
      client.publish(topic_pub, "Error: registrar"); 

      return p;

    case FINGERPRINT_PACKETRECIEVEERR:

      Serial.println("Communication error");
      mostrarLCD("Error:","Communication");
      client.publish(topic_pub, "Error: registrar"); 

      return p;

    case FINGERPRINT_FEATUREFAIL:

      Serial.println("Could not find fingerprint features");
      mostrarLCD("Error: fgprint","freatures");
      client.publish(topic_pub, "Error: registrar"); 

      return p;

    case FINGERPRINT_INVALIDIMAGE:

      Serial.println("Could not find fingerprint features");
      mostrarLCD("Error: invalid","image");
      client.publish(topic_pub, "Error: registrar"); 

      return p;

    default:

      Serial.println("Unknown error");
      mostrarLCD("Error:","desconocido");
      client.publish(topic_pub, "Error: registrar"); 

      return p;

  }

  // OK converted!

  Serial.print("Creating model for #");  Serial.println(id);
  p = finger.createModel();

  if (p == FINGERPRINT_OK) {
    Serial.println("Prints matched!");
  } 
  else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    Serial.println("Communication error");
    client.publish(topic_pub, "Error: registrar"); 
    return p;
  } 
  else if (p == FINGERPRINT_ENROLLMISMATCH) {
    Serial.println("Fingerprints did not match");
    mostrarLCD("Error: los dedos","no coincidieron");
    client.publish(topic_pub, "Error: registrar"); 
    return p;
  } 
  else {
    Serial.println("Unknown error");
    client.publish(topic_pub, "Error: registrar"); 
    return p;
  }

  Serial.print("ID "); Serial.println(id);
  p = finger.storeModel(id);

  if (p == FINGERPRINT_OK) {
    Serial.println("Stored!");
    mostrarLCD("Huella registrada","con exito");
    String mensaje = "Success: registrar ID: #" + String(id);
    client.publish(topic_pub, mensaje.c_str());
    return 1;
  } 
  else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    Serial.println("Communication error");
    mostrarLCD("Error:","comunicacion");
    client.publish(topic_pub, "Error: registrar"); 
    return p;
  } 
  else if (p == FINGERPRINT_BADLOCATION) {
    Serial.println("Could not store in that location");
    mostrarLCD("Error:","almacenamiento");
    client.publish(topic_pub, "Error: registrar"); 
    return p;
  } 
  else if (p == FINGERPRINT_FLASHERR) {
    Serial.println("Error writing to flash");
    mostrarLCD("Error:","mem flash");
    client.publish(topic_pub, "Error: registrar"); 
    return p;
  } 
  else {
    Serial.println("Unknown error");
    mostrarLCD("Error:","desconocido");
    client.publish(topic_pub, "Error: registrar"); 
    return p;
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.println("Mensaje recibido [");
  Serial.print(topic);
  Serial.print("]: ");

  String message = "";
  for (int i = 0; i < length; i++) {
    char c = (char)payload[i];
    message += c;
    Serial.print(c);
  }

  instruccion = message.substring(0, 4);

  mostrarLCD("servidor dice:", instruccion);
  delay(2000);

  // Publicar respuesta al otro tópico
  String respuesta = "ACK: " + message;
  client.publish(topic_pub, respuesta.c_str());
  Serial.println("Se envió respuesta: ACK");
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Conectando al broker MQTT...");
    if (client.connect("ESP32Cliente")) {
      Serial.println("conectado!");
      client.subscribe(topic_sub);
      Serial.print("Suscrito a: ");
      Serial.println(topic_sub);
      mostrarLCD("Reconectado al","servidor");
      delay(2000);
    } else {
      Serial.print("Fallo, rc=");
      Serial.print(client.state());
      Serial.println(" intentando de nuevo en 5 segundos");
      mostrarLCD("Error: mqtt-s","reintentando...");
      delay(5000);
    }
  }
}

void mostrarLCD(const String& linea1, const String& linea2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(linea1);
  lcd.setCursor(0, 1);
  lcd.print(linea2);
}

uint8_t readnumber(void) {
  uint8_t num = 0;
  while (num == 0) {
    while (!Serial.available());
    num = Serial.parseInt();
  }
  return num;
}