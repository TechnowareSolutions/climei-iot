#define USE_ARDUINO_INTERRUPTS false

#include <ArduinoWiFiServer.h>
#include <BearSSLHelpers.h>
#include <CertStoreBearSSL.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiAP.h>
#include <ESP8266WiFiGeneric.h>
#include <ESP8266WiFiGratuitous.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266WiFiSTA.h>
#include <ESP8266WiFiScan.h>
#include <ESP8266WiFiType.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <WiFiClientSecureBearSSL.h>
#include <WiFiServer.h>
#include <WiFiServerSecure.h>
#include <WiFiServerSecureBearSSL.h>
#include <WiFiUdp.h>


#include <Wire.h>
#include <DHTesp.h> 
#include <PulseSensorPlayground.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>


// Pinos
#define PIN_DHT 3

// Variaveis
#define REPORTING_PERIOD_MS 1000
uint32_t tsLastReport = 0;

// WiFi
const char *ssid = "CGB 2"; // Enter your WiFi name
const char *password = "wifi@2023";  // Enter WiFi password
WiFiClient wifiClient;

// MQTT Broker
const int mqtt_port = 1883;
const char *mqtt_username = "";
const char *mqtt_password = "";
const char *id_mqtt = "fiap-iot";
const char *mqtt_broker = "broker.hivemq.com";
PubSubClient MQTT(wifiClient);


char EstadoSaida = '0';  //variável que armazena o estado atual da saída

// Topics
const char *MQTT_TOPIC_UMIDADE = "fiap/iot/turma/2tdspg/grupo/technoware/devtype/esp8266/devid/esp001/sensor/dht11/umidade";
const char *MQTT_TOPIC_TEMPERATURA = "fiap/iot/turma/2tdspg/grupo/technoware/devtype/esp8266/devid/esp001/sensor/dht11/temperatura";
const char *MQTT_TOPIC_BPM = "fiap/iot/turma/2tdspg/grupo/technoware/devtype/esp8266/devid/esp001/sensor/pulsesensor/bpm";

// MQTT
const int callbackSize = 248;
unsigned long publishUpdate = 0;

// DHT
DHTesp dht;
TempAndHumidity dhtValues;

// Heart
const int OUTPUT_TYPE = SERIAL_PLOTTER;

const int PULSE_INPUT = A0;
const int PULSE_BLINK = LED_BUILTIN;
const int PULSE_FADE = 5;
const int THRESHOLD = 550;

byte samplesUntilReport;
int BPM;
const byte SAMPLES_PER_SERIAL_SAMPLE = 10;

PulseSensorPlayground pulseSensor;

// Funções
void atualizarSensores() {
  dhtValues = dht.getTempAndHumidity();
}

void iniciarWifi(){
  Serial.println("Conectando-se a rede: "+String(ssid));
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(50);
    Serial.print(".");
  }

  Serial.println("Conectado com sucesso!");
  Serial.println("IP: "+WiFi.localIP().toString() + " - SSID: "+WiFi.SSID());
}

void iniciarMqtt(){
  MQTT.setServer(mqtt_broker, mqtt_port);
  MQTT.setCallback(callbackMqtt);
}

void callbackMqtt(char *topic, byte *payload, unsigned int length) {
  String message = String((char *)payload).substring(0, length);
  StaticJsonDocument<callbackSize> json;
  DeserializationError error = deserializeJson(json, message);

  if (error) {
    Serial.println("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }
}

void reconectarMqtt() {
  while (!MQTT.connected()) {
    Serial.println("Tentando se conectar ao broker MQTT: ");
    Serial.println(mqtt_broker);
    if (MQTT.connect(id_mqtt, mqtt_username, mqtt_password)) {
      Serial.println("Conectado com sucesso ao broker MQTT!");
    } else {
      Serial.println("Falha ao reconectar ao broker.");
      Serial.println("Tentando novamente em 5 segundos");
      delay(5000);
    }
  }
}

void verificarMqttWifi(){
  if (!MQTT.connected()) {
    reconectarMqtt();
  }

  if (WiFi.status() != WL_CONNECTED) {
    iniciarWifi();
  }
}

void setup() {
  Serial.begin(115200);

  iniciarWifi();
  iniciarMqtt();
  pulseSensor.analogInput(PULSE_INPUT);
  pulseSensor.blinkOnPulse(PULSE_BLINK);
  pulseSensor.fadeOnPulse(PULSE_FADE);

  pulseSensor.setSerial(Serial);
  pulseSensor.setOutputType(OUTPUT_TYPE);
  pulseSensor.setThreshold(THRESHOLD);


  samplesUntilReport = SAMPLES_PER_SERIAL_SAMPLE;

  // Now that everything is ready, start reading the PulseSensor signal.
  if (!pulseSensor.begin()) {

    for(;;) {
      digitalWrite(PULSE_BLINK, LOW);
      delay(50); Serial.println('!');
      digitalWrite(PULSE_BLINK, HIGH);
      delay(50);
    }
  }

  dht.setup(PIN_DHT, DHTesp::DHT11);
}

void publicarInformacoes(const char* topic, String sensor, float value, String variable, String unit){
  StaticJsonDocument<callbackSize> json;
  char jsonBuffer[callbackSize];

  json["sensor"] = sensor;
  json["value"] = value;
  json["variable"] = variable;
  json["unit"] = unit;
  serializeJson(json, jsonBuffer);

  MQTT.publish(topic, jsonBuffer);

  Serial.println("Publicando no topico: "+String(topic));
  Serial.println(jsonBuffer);
}

void loop() {
  verificarMqttWifi();
  atualizarSensores();

  if (pulseSensor.sawNewSample()) {
      if (--samplesUntilReport == (byte) 0) {
        samplesUntilReport = SAMPLES_PER_SERIAL_SAMPLE;

        BPM = pulseSensor.getBeatsPerMinute();

        if (pulseSensor.sawStartOfBeat()) {
          publicarInformacoes(MQTT_TOPIC_BPM, "pulsesensor", BPM, "batimento cardiaco", "bpm");
        }
      }
    }

  if (millis() - tsLastReport > REPORTING_PERIOD_MS) {

    // Temperatura e Umidade
    if (!isnan(dhtValues.temperature) && !isnan(dhtValues.humidity)) {
      publicarInformacoes(MQTT_TOPIC_UMIDADE, "dht22", dhtValues.humidity, "umidade", "%");
      publicarInformacoes(MQTT_TOPIC_TEMPERATURA, "dht22", dhtValues.temperature, "temperatura", "°C");
    }

    tsLastReport = millis();
  }
}