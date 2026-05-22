#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <ArduinoJson.h>

// ── WiFi ─────────────────────────────────────────────────────────
const char* WIFI_SSID     = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";

// ── MQTT HiveMQ Cloud (TLS) ───────────────────────────────────────
const char* MQTT_BROKER   = "c175cd285334458eab19178117bdbc91.s1.eu.hivemq.cloud";
const int   MQTT_PORT     = 8883;
const char* MQTT_USER     = "FelipeRM564723";
const char* MQTT_PASSWORD = "Fiap@20072006";
const char* MQTT_CLIENT   = "fidelis-coleira-001";
const char* MQTT_TOPIC    = "fidelis/coleira/dados";

// ── Pinos ────────────────────────────────────────────────────────
#define DHT_PIN   21
#define DHT_TYPE  DHT22
#define TRIG_PIN  25
#define ECHO_PIN  26
#define LED_PIN   23

// ── Thresholds ───────────────────────────────────────────────────
#define TEMP_FEBRE    39.5f
#define DIST_INATIVO  30.0f
#define DIST_ATIVO    80.0f

// ── Intervalo de publicação ──────────────────────────────────────
#define INTERVALO_MS 5000

DHT dht(DHT_PIN, DHT_TYPE);
WiFiClientSecure wifiClient;
PubSubClient mqtt(wifiClient);

unsigned long ultimaPublicacao = 0;

// ────────────────────────────────────────────────────────────────
void conectarWiFi() {
  Serial.print("Conectando WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi OK — IP: " + WiFi.localIP().toString());
}

void conectarMQTT() {
  wifiClient.setInsecure();
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setKeepAlive(60);

  while (!mqtt.connected()) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi desconectado! Abortando tentativa de MQTT...");
      return;
    }

    Serial.print("Conectando MQTT...");
    if (mqtt.connect(MQTT_CLIENT, MQTT_USER, MQTT_PASSWORD)) {
      Serial.println("OK");
    } else {
      Serial.printf("Falhou (rc=%d), tentando em 3s...\n", mqtt.state());
      wifiClient.stop();
      delay(3000);
    }
  }
}

float lerDistancia() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duracao = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duracao == 0) return -1.0f;
  return duracao * 0.034f / 2.0f;
}

String classificarAtividade(float distancia) {
  if (distancia < 0)             return "desconhecido";
  if (distancia < DIST_INATIVO)  return "inativo";
  if (distancia < DIST_ATIVO)    return "moderado";
  return "ativo";
}

// ────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n=== Coleira Fidelis VET ===");
  Serial.println("Iniciando...");

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  dht.begin();
  Serial.println("DHT22 inicializado.");

  conectarWiFi();
  conectarMQTT();

  Serial.println("Setup completo. Publicando a cada 5s.");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Conexão WiFi perdida. Reconectando...");
    conectarWiFi();
  }

  if (!mqtt.connected()) {
    conectarMQTT();
  }

  mqtt.loop();

  unsigned long agora = millis();
  if (agora - ultimaPublicacao < INTERVALO_MS) return;
  ultimaPublicacao = agora;

  float temperatura = dht.readTemperature();
  float umidade     = dht.readHumidity();
  float distancia   = lerDistancia();

  if (isnan(temperatura) || isnan(umidade)) {
    Serial.println("Erro na leitura do DHT22");
    return;
  }

  String atividade = classificarAtividade(distancia);
  bool   alerta    = temperatura > TEMP_FEBRE;

  digitalWrite(LED_PIN, alerta ? HIGH : LOW);

  StaticJsonDocument<256> doc;
  doc["dispositivo"]  = MQTT_CLIENT;
  doc["temperatura"]  = serialized(String(temperatura, 1));
  doc["umidade"]      = serialized(String(umidade, 1));
  doc["distancia_cm"] = serialized(String(distancia, 1));
  doc["atividade"]    = atividade;
  doc["alerta_febre"] = alerta;
  doc["timestamp_ms"] = agora;

  char payload[256];
  serializeJson(doc, payload);

  mqtt.publish(MQTT_TOPIC, payload);
  Serial.println("Publicado: " + String(payload));
}