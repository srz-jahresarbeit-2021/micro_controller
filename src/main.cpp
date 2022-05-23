#include <LittleFS.h>                 /

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino

#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager

#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

#include <Arduino.h>

#include <SoftwareSerial.h>

#include <I2S.h>

#include <vector>

//API for the bme280 temp/hum/pres sensor
#include <Bme280.h>               //https://github.com/malokhvii-eduard/arduino-bme280
//MQTT client
#include <PubSubClient.h>         //https://github.com/knolleary/pubsubclient

//detects double press of the reset button by setting a flag in ESP8266 RTC Memory
//double press triggers reconfiguration of the device 
#include <ESP_DoubleResetDetector.h>  //https://github.com/khoih-prog/ESP_DoubleResetDetector

#include <MHZ19.h>                //https://github.com/WifWaf/MH-Z19
 
//in seconds
#define DRD_TIMEOUT 1   
//only needed in constructor, could be removed
#define DRD_ADDRESS 0 
DoubleResetDetector drd(DRD_TIMEOUT, DRD_ADDRESS);

//TODO: maybe remove/make configurable
#define UPDATE_TIME 60000
#define PPM_RANGE 2000

//max number of samples
#define SAMPLES 256

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

// MHZ19 (CO2)
MHZ19 myMHZ19;                       
SoftwareSerial mySerial;                   // create device to MH-Z19 serial

//time since last upload of data
unsigned long lastMillis;

//microphone samples
std::vector<int> mic_samples[SAMPLES];

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

  pinMode(LED_BUILTIN, OUTPUT);

  if(!read_config() || drd.detectDoubleReset()){
    shouldConfig = true;
  }else{
    //deactivate led: no config mode
    pinMode(LED_BUILTIN, HIGH);
  }
  // The extra parameters to be configured
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
  WiFiManager wifiManager;

  //set config save notify and ap callback 
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
    wifiManager.startConfigPortal(("AutoConnectAP" + device_macaddress).c_str(), "password");
  }else{
    Serial.println("Trying to connect to network");
    if (!wifiManager.autoConnect(("AutoConnectAP" + device_macaddress).c_str(), "password")) {
        Serial.println("failed to connect and hit timeout");
        delay(3000);
        //reset and try again
        ESP.reset();
        delay(5000);
    }
  }

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

  //only for debugging purposes
  Serial.println("local ip");
  Serial.println(WiFi.localIP());

  //tell pubsubclient where to find the mqtt server
  client.setServer(mqtt_server, String(mqtt_port).toInt());
  client.setKeepAlive(65);


  //temperature via i2c
  if(has_temp){
    Wire.begin(pinSda, pinSdl);

    bmesensor.begin(Bme280TwoWireAddress::Secondary);
    bmesensor.setSettings(Bme280Settings::indoor());
  }

  //co2 via rx pin (serial)
  if(has_co2) {
    Serial.println("starting");
    Serial.flush();
    
    //can pass either a software serial or the hardware serial
    //usage of software serial will collide with hardware serial
    //->noise when printing to serial monitor
    //mySerial.begin(9600, SWSERIAL_8N1, RX, TX, false);
    myMHZ19.begin(Serial);                              

    myMHZ19.autoCalibration();                              // Turn auto calibration ON (takes ~ 20 minutes, will be done once every few months)
    
  }

  //mic via i2s, is a bit iffy
  if(has_mic){
    // start I2S at 16 kHz with 32-bits per sample
    if (!I2S.begin(I2S_PHILIPS_MODE, 16000, 32)) {
      Serial.println("Failed to initialize I2S!");
      while(1); //do nothing (shoud probably ask the user to reenter configs or something along those lines)
    }
  }

  //LED off
  digitalWrite(LED_BUILTIN, HIGH);
  //no double click to detect
  drd.stop();
}

//takes sensor type (co2/pressure/temp/...) 
//and measurement value to publish to the mqtt broker
void upload(String sensor_type, String data){
    DynamicJsonDocument json(1024);
    json["sensor_type"] = sensor_type;
    json["measure_value"] = data;
    json["controller_id"] = device_macaddress;

    String json_string;
    serializeJson(json, json_string);
    serializeJson(json, Serial);

    client.publish("/sensors/measurements", json_string.c_str());
}

void loop() {
    if (!client.connected()) {
        while (!client.connected()) {
            client.connect(device_macaddress.c_str() , mqtt_user, mqtt_pass);
            delay(100);
        }
        send_sensor_config();

    }

    if(millis() - lastMillis >= UPDATE_TIME){
      lastMillis = millis();
      if(has_temp){
        auto temperature = String(bmesensor.getTemperature());
        upload("temp", temperature);
        auto pressure = String(bmesensor.getPressure() / 100.0);
        upload("pressure", pressure);
        auto humidity = String(bmesensor.getHumidity());
        upload("humidity", humidity);
      }
      if(has_mic){}
      if(has_co2){
        int CO2; 

        CO2 = myMHZ19.getCO2();              
        
        upload("co2", String(CO2));
      }

    }
    client.loop();
    if(has_mic){
      int sample = 0; 
      while ((sample == 0) || (sample == -1) ) {
        sample = I2S.read();
      }
      // convert to 18 bit signed
      sample >>= 14; 
      mic_samples->push_back(sample);
    }
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
        //allocate a buffer to store contents of the file
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

//TODO: catch serialitations errors
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

  //called once during microcontroller startup
  //tells the server the new config
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
    serializeJson(json, Serial);

    client.publish("/sensors/config", json_string.c_str());
  }