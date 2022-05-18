#include <LittleFS.h>                 //this needs to be first, or it all crashes and burns...

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino

#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager

#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

#include <Arduino.h>

#include <I2S.h>

//API for the bme280 temp/hum/pres sensor
#include <Bme280.h>
//MQTT client
#include <PubSubClient.h>

#include <ESP_DoubleResetDetector.h>

#define DRD_TIMEOUT 20
#define DRD_address 0 
#define UPDATE_TIME 60000
#define MICROPHONE_SAMPLES 128


DoubleResetDetector drd(DRD_TIMEOUT, DRD_address);

//defaults
char mqtt_server[40];
char mqtt_port[6] = "8883";
char mqtt_pass[40];
char mqtt_user[40];

//flag for saving data
bool shouldSaveConfig = false;

//Global wifi and mqtt client
WiFiClient espClient;
PubSubClient client(espClient);

// Object for the Bme280 pressure/humidity/temperature sensor using i^2
Bme280TwoWire bmesensor;


//using mac address as id
String device_macaddress;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void configModeCallback(WiFiManager *wifiManager){
  Serial.println("Entered config mode!");
  Serial.println(WiFi.softAPIP());

  //drd.stop();
}

void setup() {
  // put your setup code here, to run once:
  WiFi.mode(WIFI_STA);
  device_macaddress = WiFi.macAddress();


  Serial.begin(9600);

  Serial.println();

  //read configuration from FS json
  Serial.println("mounting filesystem...");
  
  if (LittleFS.begin()) {
    Serial.println("mounted file system");
    if (LittleFS.exists("/mqtt_config.json")) {
      Serial.println("reading config file");
      File configFile = LittleFS.open("/mqtt_config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);

        DynamicJsonDocument json(1024);
        auto deserializeError = deserializeJson(json, buf.get());
        serializeJson(json, Serial);

        if ( ! deserializeError ) {
          Serial.println();
          Serial.println("parsed json");
          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(mqtt_pass, json["mqtt_pass"]);
          strcpy(mqtt_user, json["mqtt_user"]);
        } else {
          Serial.println("failed to load json config");
        }
        configFile.close();
      }
    }
  } else {
    Serial.println("failed to mount FS");
    Serial.println("restarting device"); 
    ESP.restart();
  }
  //end read

  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);
  WiFiManagerParameter custom_mqtt_pass("pass", "mqtt password", mqtt_pass, 40);
  WiFiManagerParameter custom_mqtt_user("user", "mqtt username", mqtt_user, 40);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.setAPCallback(configModeCallback);

  //add all your parameters here
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_pass);
  wifiManager.addParameter(&custom_mqtt_user);


  //fetches ssid, password and tries to connect to that AP
  if(drd.detectDoubleReset()){
    Serial.println("Double reset detected! Refetching config");
    wifiManager.startConfigPortal("AutoConnectAP", "password");
  }else{
    Serial.println("No double reset!");
    if (!wifiManager.autoConnect("AutoConnectAP", "password")) {
        Serial.println("failed to connect and hit timeout");
        delay(3000);
        //reset and try again
        ESP.reset();
        delay(5000);
    }
  }


  //if you get here you have connected to the WiFi
  Serial.println("connected to wifi AP...");

  //read updated parameters
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_pass, custom_mqtt_pass.getValue());
  strcpy(mqtt_user, custom_mqtt_user.getValue());

  Serial.println("The values in the file are: ");
  Serial.println("\tmqtt_server : " + String(mqtt_server));
  Serial.println("\tmqtt_port : " + String(mqtt_port));
  Serial.println("\tmqtt_pass : " + String(mqtt_pass));
  Serial.println("\tmqtt_user : " + String(mqtt_user));
  

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonDocument json(1024);
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_pass"] = mqtt_pass;
    json["mqtt_user"] = mqtt_user;

    File configFile = LittleFS.open("/mqtt_config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    serializeJson(json, Serial);
    serializeJson(json, configFile);
    configFile.close();
    Serial.println();
    //end save
  }

  Serial.println("local ip");
  Serial.println(WiFi.localIP());


  client.setServer(mqtt_server, String(mqtt_port).toInt());
  client.setKeepAlive(65);

  Wire.begin(D2, D1);

  bmesensor.begin(Bme280TwoWireAddress::Primary);
  bmesensor.setSettings(Bme280Settings::indoor());

  drd.stop();
}

void upload(String sensor_type, String data){
    auto json_string = String("{\"sensor_type\":\"" + sensor_type + "\",\"data\":\"" + data + "\",\"controller_id\":\"" + device_macaddress.c_str() + "\"}");
    char json_char_array[json_string.length() + 1];
    json_string.toCharArray(json_char_array, json_string.length() + 2);
    Serial.println(json_char_array);


    client.publish("/sensors/data", json_char_array);
}

void loop() {

    if (!client.connected()) {
        while (!client.connected()) {
            client.connect(device_macaddress.c_str() , mqtt_user, mqtt_pass);
            delay(100);
        }
        client.subscribe(("sensors/config/" + device_macaddress).c_str());
    }
    client.loop();

    auto temperature = String(bmesensor.getTemperature());
    upload("temp", temperature);
    auto pressure = String(bmesensor.getPressure() / 100.0);
    upload("press", pressure);
    auto humidity = String(bmesensor.getHumidity());
    upload("hum", humidity);

    delay(UPDATE_TIME);
}