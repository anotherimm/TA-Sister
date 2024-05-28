#include <ESP8266WiFi.h>
#include <DHT.h>
#include <MQTT.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#define DHTPIN D5
#define DHTTYPE DHT11
#define RELAY_PIN D6
#define FAN_PIN D3

// WiFi credentials
const char* ssid = "Unix";
const char* password = "kamunanya!";

// MQTT Broker settings
const char* mqtt_broker = "free.mqtt.iyoti.id";
const char* topic_pub_temp = "smartkos/pub/dht/temp";
const char* topic_pub_humid = "smartkos/pub/dht/humid";
const char* topic_sub_relay = "smartkos/sub/relay";
const char* mqtt_username = "test";
const char* mqtt_password = "1234";
const int mqtt_port = 1883;
const char* will_topic = "smartkos/status";
const char* will_message = "disconnected";

WiFiClient net;
MQTTClient client;
DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);

float temp, humid;
unsigned long previousMillis = 0;
const long interval = 5000; // 5 seconds interval
bool fanStatus = LOW;
unsigned long displayMessageUntil = 0;
const long displayMessageDuration = 3000; // 3 seconds to display message

void connectToWiFi() {
    Serial.println();
    Serial.print("Connecting to WiFi");

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Connecting WiFi");

    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println();
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi connected");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP());
    delay(displayMessageDuration);
}

void connectToMQTT() {
    Serial.print("Connecting to MQTT broker");

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Connecting MQTT");

    while (!client.connect(mqtt_username, mqtt_password)) {
        Serial.print(".");
        delay(1000);
    }
    Serial.println();
    Serial.println("Connected to MQTT broker");

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Connected MQTT");
    delay(displayMessageDuration);

    client.subscribe(topic_sub_relay);
}

void displayMessage(const String &message) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("MQTT Message:");
    lcd.setCursor(0, 1);
    lcd.print(message);
    displayMessageUntil = millis() + displayMessageDuration;
}

void messageReceived(String &topic, String &payload) {
    Serial.println("Incoming message:");
    Serial.println(topic + ": " + payload);
    
    displayMessage(topic + ": " + payload);

    if (topic == topic_sub_relay) {
        StaticJsonDocument<200> doc;
        DeserializationError error = deserializeJson(doc, payload);
        
        if (error) {
            Serial.print("deserializeJson() failed: ");
            Serial.println(error.c_str());
            return;
        }

        const char* state = doc["ON"];
        if (state && strcmp(state, "ON") == 0) {
            digitalWrite(RELAY_PIN, HIGH);
            Serial.println("Relay turned ON");
            displayMessage("Relay turned ON");
        } else {
            state = doc["OFF"];
            if (state && strcmp(state, "OFF") == 0) {
                digitalWrite(RELAY_PIN, LOW);
                Serial.println("Relay turned OFF");
                displayMessage("Relay turned OFF");
            }
        }
    }
}

void readSensor() {
    humid = dht.readHumidity();
    temp = dht.readTemperature();

    if (!isnan(humid) && !isnan(temp)) {
        Serial.print("Humidity: ");
        Serial.print(humid);
        Serial.print(" %\t");
        Serial.print("Temperature: ");
        Serial.println(temp);

        //QoS 1
        client.publish(topic_pub_temp, String(temp), true, 1);
        client.publish(topic_pub_humid, String(humid), true, 1);

        // Menyalakan kipas jika suhu melebihi 32 derajat Celsius
        if (temp > 32 && fanStatus == LOW) { // Periksa apakah kipas mati dan suhu melebihi 32
            digitalWrite(FAN_PIN, HIGH);
            Serial.println("Fan turned ON");
            client.publish("smartkos/pub/fan", "Fan turned ON");
            fanStatus = HIGH; // Perbarui status terakhir kipas menjadi nyala
            displayMessage("Fan turned ON");
        } else if (temp <= 32 && fanStatus == HIGH) { // Periksa apakah kipas menyala dan suhu tidak lagi melebihi 32
            digitalWrite(FAN_PIN, LOW);
            Serial.println("Fan turned OFF");
            client.publish("smartkos/pub/fan", "Fan turned OFF");
            fanStatus = LOW; // Perbarui status terakhir kipas menjadi mati
            displayMessage("Fan turned OFF");
        }

        // Only update the LCD if no MQTT message is being displayed
        if (millis() > displayMessageUntil) {
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Temp: ");
            lcd.print(temp);
            lcd.print(" C");
            lcd.setCursor(0, 1);
            lcd.print("Humid: ");
            lcd.print(humid);
            lcd.print(" %");
        }
    } else {
        Serial.println("Failed to read from DHT sensor!");
    }
}

void setup() {
    Serial.begin(9600);
    pinMode(RELAY_PIN, OUTPUT);
    pinMode(FAN_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW); // Ensure relay is off at start
    digitalWrite(FAN_PIN, LOW);

    lcd.init();
    lcd.backlight();
    
    WiFi.begin(ssid, password);
    dht.begin();
    client.begin(mqtt_broker, mqtt_port, net);
    client.onMessage(messageReceived); // Set the message received callback
    
    // Set Last Will and Testament (LWT) before connecting
    client.setWill(will_topic, will_message, true, 1);

    connectToWiFi();
    connectToMQTT();
    previousMillis = millis();
}

void loop() {
    if (WiFi.status() != WL_CONNECTED) {
        connectToWiFi();
    }

    if (!client.connected()) {
        connectToMQTT();
    }

    client.loop();

    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= interval) {
        previousMillis = currentMillis;
        readSensor();
    }
}
