#include <DHT.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <WebSocketsClient.h>
#include <Hash.h>
#include "arduino_secrets.h"

// You only need to modify wi-fi and domain info
const char* ssid     = SECRET_SSID;   // enter your ssid/ wi-fi(case sensitive) router name - 2.4 Ghz only
const char* password = SECRET_PASS;   // enter ssid password (case sensitive)
char host[] = SECRET_LINK;            // better your Heroku domain name like  "iottempswitch.herokuapp.com"
int port = 80;
char path[] = "/ws";

#define DHTTYPE DHT22     // DHT 11
#define DHTPIN D5          // GPIO 5 (D1) OR for ESP change it to GPIO0
DHT dht(DHTPIN, DHTTYPE);

const int relayPin = D4;    // GPIO 16 (D0) OR change it to GPIO2


ESP8266WiFiMulti WiFiMulti;
WebSocketsClient webSocket;
DynamicJsonBuffer jsonBuffer;
String currState;
int pingCount = 0;
String triggerName = "";
String triggerVal = "";
int triggerEnabled = 0;

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) { //uint8_t *
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.println("Disconnected! ");
      Serial.println("Connecting...");
      webSocket.begin(host, port, path);
      webSocket.onEvent(webSocketEvent);
      break;
    case WStype_CONNECTED:
      Serial.println("Connected! ");
      // send message to server when Connected
      webSocket.sendTXT("Connected");
      break;
    case WStype_TEXT:
      Serial.println("Got data");
      //data = (char*)payload;
      processWebScoketRequest((char*)payload);
      break;
    case WStype_BIN:
      hexdump(payload, length);
      Serial.print("Got bin");
      // send data to server
      webSocket.sendBIN(payload, length);
      break;
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  dht.begin();

  pinMode(relayPin, OUTPUT);

  for (uint8_t t = 4; t > 0; t--) {
    delay(1000);
  }
  Serial.println();
  Serial.println();
  Serial.println("Connecting to ");

  //Serial.println(ssid);
  WiFiMulti.addAP(ssid, password);

  //WiFi.disconnect();
  while (WiFiMulti.run() != WL_CONNECTED) {
    Serial.print(".");
    delay(100);
  }
  Serial.println("Connected to wi-fi");
  webSocket.begin(host, port, path);
  webSocket.onEvent(webSocketEvent);

}

void loop() {
  webSocket.loop();    
  getTemp();
  if (triggerEnabled == 1) {
    setTrigger(triggerName, triggerVal);
  }
  //@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
  //If you make change to delay make sure adjust the ping
  delay(1000);
  // make sure after every 40 seconds send a ping to Heroku
  //so it does not terminate the websocket connection
  //This is to keep the conncetion alive between ESP and Heroku
  if (pingCount > 20) {
    pingCount = 0;
    webSocket.sendTXT("\"heartbeat\":\"keepalive\"");

  } else {
    pingCount += 1;
  }
  //@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
}

void processWebScoketRequest(String data) {
  String jsonResponse = "{\"version\": \"1.0\",\"sessionAttributes\": {},\"response\": {\"outputSpeech\": {\"type\": \"PlainText\",\"text\": \"<text>\"},\"shouldEndSession\": true}}";
  JsonObject& root = jsonBuffer.parseObject(data);
  String query = root["query"];
  String message = "";
  Serial.println(data);
  if (query == "cmd") { //if query check state
    String value = root["value"];
    Serial.println("Received command!");
    if (value == "on") {
      digitalWrite(relayPin, HIGH);
      message = "{\"state\":\"ON\"}";
      currState = "ON";
    } else if (value == "off") {
      digitalWrite(relayPin, LOW);
      message = "{\"state\":\"OFF\"}";
      currState = "OFF";
    } else if (value == "deactivate") {
      //deactivate trigger
      triggerEnabled = 0;
    } else {
      String object = root["object"];
      //set trigger for temp and humidity
      triggerName = object;
      triggerVal = value;
      triggerEnabled = 1;
    }
    jsonResponse.replace("<text>", "It is done");
  } else if (query == "?") { //if command then execute
    Serial.println("Received query!");
    int state = digitalRead(relayPin);
    String value = root["value"];
    //Serial.print("Value-->");
    //Serial.print(value);
    if (value == "switch") {
      if (currState == "ON") {
        message = "{\"state\":\"ON\"}";
      } else {
        message = "{\"state\":\"OFF\"}";
      }
    } else if (value == "umidita") {
      //response with current humidity DHT.humidity
      Serial.println("Humidity response...");
      jsonResponse.replace("<text>", "current humidity is " + String(dht.readHumidity()) + " percent");

    } else if (value == "temperatura") {
      //response with current temperature DHT.temperature /Celsius2Fahrenheit(DHT.temperature)
      Serial.println("Temp response...");
      jsonResponse.replace("<text>", "current temperature is " + String(Celsius2Fahrenheit(dht.readTemperature())) + " fahrenheit");
    }
  } else { //can not recognized the command
    Serial.println("Command is not recognized!");
  }
  //jsonResponse.replace("<text>", "Garage door " + instance + " is " + message );
  Serial.print("Sending response back");
  Serial.println(jsonResponse);
  // send message to server
  webSocket.sendTXT(jsonResponse);
  if (query == "cmd" || query == "?") {
    webSocket.sendTXT(jsonResponse);
  }
}

void setTrigger(String obj, String val) {
  Serial.print("Trigger is set for ");
  Serial.print(val.toFloat());
  Serial.print(" ");
  Serial.print(triggerName);
  Serial.println("");

  if (String("fahrenheit") == obj) {
    if (Celsius2Fahrenheit(dht.readTemperature()) >= val.toFloat()) {
      Serial.println("Fahrenheit trigger on!");
      digitalWrite(relayPin, HIGH);
    } else {
      digitalWrite(relayPin, LOW);
    }
  } else if (String("celsius") == obj) {
    //Celsius2Fahrenheit(DHT.temperature)
    if (dht.readTemperature() >= val.toFloat()) {
      Serial.println("Celsius trigger on!");
      digitalWrite(relayPin, HIGH);
    } else {
      digitalWrite(relayPin, LOW);
    }
  } else {
    //DHT.humidity
    if (dht.readHumidity() >= val.toFloat()) {
      Serial.println("Humidity trigger on!");
      digitalWrite(relayPin, HIGH);
    } else {
      digitalWrite(relayPin, LOW);
    }
  }
}


void getTemp() {
  //  DHT.read11(dht_dpin);
  Serial.print("Current humidity = ");
  Serial.print(dht.readHumidity());
  Serial.print("%  ");
  Serial.print("temperature = ");
  Serial.print(dht.readTemperature());
  Serial.print("C  ");
  Serial.print(Celsius2Fahrenheit(dht.readTemperature()));
  Serial.println("F  ");
  delay(1000);//Don't try to access too frequently... in theory
  //should be once per two seconds, fastest,
  //but seems to work after 0.8 second.
}

double Celsius2Fahrenheit(double celsius) {
  return celsius * 9 / 5 + 32;
}
