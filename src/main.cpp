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

//detects double press of the reset button by setting a flag in ESP8266 RTC Memory
//double press triggers reconfiguration of the device 
#include <ESP_DoubleResetDetector.h>
 
//in seconds
#define DRD_TIMEOUT 1   
//only needed in constructor, could be removed
#define DRD_ADDRESS 0 
DoubleResetDetector drd(DRD_TIMEOUT, DRD_ADDRESS);

//TODO: maybe remove/make configurable
#define UPDATE_TIME 60000

//loc of config file in fs
char config_file[40] = "/config.json";

//parameters
//mqtt credentials
char mqtt_server[40];
char mqtt_port[6];
char mqtt_pass[40];
char mqtt_user[40];
//room and device name
char room_name[40];
char sensor_name[40];
//which sensors?
bool has_temp = false;
bool has_mic = false;
bool has_co2 = false;
//advanced options for pins (not going to be implemented, could break too much -> only necessary if you change board pinout before runtime)
unsigned int pinSda = D2;
unsigned int pinSdl = D1;

//flag for saving data
bool shouldSaveConfig = true;

//whether to (re)configure the device
bool shouldConfig = false;

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

  drd.stop();
}

bool read_config();
bool write_config();
void send_sensor_config();

void setup() {
  // put your setup code here, to run once:
  WiFi.mode(WIFI_STA);
  device_macaddress = WiFi.macAddress();

  Serial.begin(9600);

  Serial.println();

  read_config();

  pinMode(LED_BUILTIN, OUTPUT);

  if(!read_config() || drd.detectDoubleReset()){
    shouldConfig = true;
    //LED = were in config mode
  }else{
    //deactivate config mode
    pinMode(LED_BUILTIN, HIGH);
  }
  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);
  WiFiManagerParameter custom_mqtt_user("user", "mqtt username", mqtt_user, 40);
  WiFiManagerParameter custom_mqtt_pass("pass", "mqtt password", mqtt_pass, 40);
  WiFiManagerParameter custom_room("room", "room name", room_name, 40);
  WiFiManagerParameter custom_sensor("sensor", "sensor name", sensor_name, 40);
  char* checkbox_html_custom = "type=\"checkbox\"";
  char* checked_flag = "";
  if(has_co2)  checked_flag = "T";
  WiFiManagerParameter custom_has_co2("co2", "Has CO<sub>2</sub>", checked_flag, 2, checkbox_html_custom);
  checked_flag = "";
  if(has_temp)  checked_flag = "T";
  WiFiManagerParameter custom_has_temp("temp", "Has Temp/Press/Hum", checked_flag, 2, checkbox_html_custom);
  checked_flag = "";
  if(has_mic)  checked_flag = "T";
  WiFiManagerParameter custom_has_mic("mic", "Has microphone", checked_flag, 2, checkbox_html_custom);



  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.setAPCallback(configModeCallback);

  //add all your parameters here
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_pass);
  wifiManager.addParameter(&custom_room);
  wifiManager.addParameter(&custom_sensor);
  wifiManager.addParameter(&custom_has_co2);
  wifiManager.addParameter(&custom_has_mic);
  wifiManager.addParameter(&custom_has_temp);


  //fetches ssid, password and tries to connect to that AP
  if(shouldConfig){
    Serial.println("Refetching config");
    wifiManager.startConfigPortal("AutoConnectAP", "password");
  }else{
    Serial.println("Trying to connect to network");
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
  strcpy(room_name, custom_room.getValue());
  strcpy(sensor_name, custom_sensor.getValue());
  has_co2 = (strncmp(custom_has_co2.getValue(), "T", 1) == 0);
  has_temp = (strncmp(custom_has_temp.getValue(), "T", 1) == 0);
  has_mic = (strncmp(custom_has_mic.getValue(), "T", 1) == 0);


  Serial.println("The values in the file are: ");
  Serial.println("\tmqtt_server : " + String(mqtt_server));
  Serial.println("\tmqtt_port : " + String(mqtt_port));
  Serial.println("\tmqtt_pass : " + String(mqtt_pass));
  Serial.println("\tmqtt_user : " + String(mqtt_user));
  Serial.println("\troom_name : " + String(room_name));
  Serial.println("\tsensor_name : " + String(sensor_name));
  Serial.println("\thas_co2 : " + String(has_co2));
  Serial.println("\thas_temp : " + String(has_temp));
  Serial.println("\thas_mic : " + String(has_mic));
  

  //save the custom parameters to FS
  if(shouldSaveConfig) {
    write_config();
  }

  Serial.println("local ip");
  Serial.println(WiFi.localIP());


  client.setServer(mqtt_server, String(mqtt_port).toInt());
  client.setKeepAlive(65);

  Wire.begin(D2, D1);

  bmesensor.begin(Bme280TwoWireAddress::Secondary);
  bmesensor.setSettings(Bme280Settings::indoor());

  digitalWrite(LED_BUILTIN, HIGH);

  drd.stop();
}

void upload(String sensor_type, String data){
    auto json_string = String("{\"sensor_type\":\"" + sensor_type + "\",\"measure_value\":\"" + data + "\",\"controller_id\":\"" + device_macaddress.c_str() + "\"}");
    char json_char_array[json_string.length() + 1];
    json_string.toCharArray(json_char_array, json_string.length() + 2);
    Serial.println(json_char_array);

    client.publish("/sensors/measurements", json_char_array);
}

void loop() {
    if (!client.connected()) {
        while (!client.connected()) {
            client.connect(device_macaddress.c_str() , mqtt_user, mqtt_pass);
            delay(100);
        }
        send_sensor_config();

    }
    client.loop();

    auto temperature = String(bmesensor.getTemperature());
    upload("temp", temperature);
    auto pressure = String(bmesensor.getPressure() / 100.0);
    upload("pressure", pressure);
    auto humidity = String(bmesensor.getHumidity());
    upload("humidity", humidity);

    delay(UPDATE_TIME);
}

bool read_config(){
  //read configuration from FS json
  Serial.println("mounting filesystem...");
  
  if (LittleFS.begin()) {
    Serial.println("mounted file system");
    if (LittleFS.exists("/config.json")) {
      Serial.println("reading config file");
      File configFile = LittleFS.open(config_file, "r");
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
          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(mqtt_pass, json["mqtt_pass"]);
          strcpy(mqtt_user, json["mqtt_user"]);
          strcpy(room_name, json["room_name"]);
          strcpy(sensor_name, json["sensor_name"]);
          has_co2 = json["has_co2"];
          has_mic = json["has_mic"];
          has_temp = json["has_temp"];
          Serial.println("parsed json");
        } else {
          Serial.println("failed to load json config");
          return false;
        }
        configFile.close();
      }
    }else{
      Serial.println("No config file found!");
      return false;
    }
    return true;
  } else {
    Serial.println("failed to mount FS");
    return false;
    //Serial.println("restarting device"); 
    //ESP.restart();
  }
}

//TODO: catch errors
bool write_config(){
    Serial.println("saving config");
    DynamicJsonDocument json(1024);
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_pass"] = mqtt_pass;
    json["mqtt_user"] = mqtt_user;
    json["room_name"] = room_name;
    json["sensor_name"] = sensor_name;
    Serial.println(has_co2);
    json["has_co2"] = has_co2;
    Serial.println(has_temp);
    json["has_temp"] = has_temp;
    Serial.println(has_mic);
    json["has_mic"] = has_mic;


    File configFile = LittleFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
      return false;
    }

    serializeJson(json, Serial);
    serializeJson(json, configFile);
    configFile.close();
    Serial.println();
    return true;
  }

  void send_sensor_config(){
    DynamicJsonDocument json(1024);
    json["room_name"] = room_name;
    json["controller_id"] = device_macaddress;
    json["controller_name"] = sensor_name;
    json["has_co2"] = has_co2;
    json["has_temp"] = has_temp;
    json["has_humidity"] = has_temp;
    json["has_pressure"] = has_temp;
    json["has_db"] = has_mic;

    String json_string;
    serializeJson(json, json_string);

    client.publish("/sensors/config", json_string.c_str());
  }