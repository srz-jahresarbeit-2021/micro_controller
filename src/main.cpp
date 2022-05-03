
// #include <WiFiManager.h>

// void loop() {

//   String measurements = temperature + ", " + pressure + ", " + humidity;
//   Serial.println(measurements);

//   delay(10000);
// }

#include <Arduino.h>
#include <Bme280.h>
// Used to configure wifi and save credentials, see https://github.com/tzapu/WiFiManager
#include <WiFiManager.h>
// mqtt client, see https://github.com/knolleary/pubsubclient
#include <PubSubClient.h>
#define UPDATE_TIME 60000
#define SENSOR_ID "1"
#define MQTT_BROKER "192.168.178.54"

// Create mqtt client globally
WiFiClient espClient;
PubSubClient client(espClient);

// Object for the Bme280 pressure/humidity/temperature sensor
Bme280TwoWire bmesensor;

void setup() {
    WiFi.mode(WIFI_STA);

    Serial.begin(9600);

    WiFiManager wm;

    bool res;
    res = wm.autoConnect("AutoConnectAP","password");   // TODO: Change password to conf 

    if(!res) {
        Serial.println("Failed to connect");
        // Maybe restart the device in order to retry the wifi connection, not necessary though
        ESP.restart();   
    } 

    Serial.println("Connection succeeded");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    client.setServer(MQTT_BROKER, 8883);
    client.setKeepAlive(65);

    Wire.begin(D2, D1);

    Serial.println();

    bmesensor.begin(Bme280TwoWireAddress::Primary);
    bmesensor.setSettings(Bme280Settings::indoor());
}

void upload(String sensor_type, String data){
    auto json_string = String("{\"sensor_type\":\"" + sensor_type + "\",\"data\":\"" + data + "\",\"controller_id\":" + SENSOR_ID + "}");
    char json_char_array[json_string.length() + 1];
    json_string.toCharArray(json_char_array, json_string.length() + 2);
    Serial.println(json_char_array);


    client.publish("/sensors/data", json_char_array);
}

void loop() {
    if (!client.connected()) {
        while (!client.connected()) {
            client.connect(SENSOR_ID , "admin", "il19");
            delay(100);
        }
    }

    client.loop();

    auto temperature = String(bmesensor.getTemperature()) + " °C";
    upload("temp", temperature);
    auto pressure = String(bmesensor.getPressure() / 100.0) + " hPa";
    upload("press", pressure);
    auto humidity = String(bmesensor.getHumidity()) + " %";
    upload("hum", humidity);

    delay(UPDATE_TIME);
}