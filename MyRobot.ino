//#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
//#include <DNSServer.h>
//#include <WiFiClient.h>
//#include <WebSocketsClient.h>
//#include <WebSockets.h>
//#include <ESP8266HTTPClient.h>

#include "WiFiManager.h"        //https://github.com/tzapu/WiFiManager
#include <WebSocketsServer.h>
#include <ESP8266mDNS.h>
#include <Servo.h>
#include <NewPingESP8266.h>
#include <ESP8266httpUpdate.h>
#include <PubSubClient.h>
#define DEBUG 1

WiFiClient espClient;
PubSubClient client(espClient);
Servo myservo[17];
char pin_set[17]="U";//'U':unknown, 'O':output, 'I':input
int command_counter =0;
const byte DNS_PORT2 = 53;
IPAddress apIP2(192, 168, 4, 1);
DNSServer dnsServer2;
WebSocketsServer webSocket = WebSocketsServer(81);
bool useAP = false;
bool useMQTT = false;
int currVersion = 1000;
String macID;
char esp_id[15];

void setup() {
    // put your setup code here, to run once:
#ifdef DEBUG
    Serial.begin(115200);
    Serial.printf("Current Version: %u\n",currVersion);
#endif

    // Do a little work to get a unique-ish name. Append the
    // last two bytes of the MAC (HEX'd) to "Thing-":
    uint8_t mac[WL_MAC_ADDR_LENGTH];
    WiFi.softAPmacAddress(mac);
    macID = String(mac[WL_MAC_ADDR_LENGTH - 2], HEX) +
                   String(mac[WL_MAC_ADDR_LENGTH - 1], HEX);
    macID.toUpperCase();
    memset(esp_id,0,15);
    esp_id[0]='e';esp_id[1]='s';esp_id[2]='p';
    for (int i=0; i<macID.length(); i++){
      esp_id[i+3] = macID.charAt(i);
    }

    
    String AP_NameString = "WIFI SELECT MODE " + macID;  

    //WiFiManager
    //Local intialization. Once its business is done, there is no need to keep it around
    WiFiManager wifiManager;

      if(wifiManager.startConfigPortal(AP_NameString.c_str())){
          if(wifiManager.useAP){
              useAP = true;
              setupWiFi();
          }else{//use lan
          }
      }

    // start webSocket server
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
    if(useAP){
       if(MDNS.begin("esp8266")) {
#ifdef DEBUG
              Serial.println("MDNS responder started");
#endif
            }
       MDNS.addService("http", "tcp", 80);
       MDNS.addService("ws", "tcp", 81); 
    }
#ifdef DEBUG
    Serial.println(WiFi.localIP());
    Serial.println("DONE!");
#endif
}
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {

    switch(type) {
        case WStype_DISCONNECTED:
#ifdef DEBUG
            Serial.printf("[%u] Disconnected!\n", num);
#endif
            if(client.connected()){
              client.disconnect();
              useMQTT = false;
            }
            break;
        case WStype_CONNECTED: {
            IPAddress ip = webSocket.remoteIP(num);
#ifdef DEBUG
            Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
#endif
            // send message to client
            webSocket.sendTXT(num, "Connected");
        }
            break;
        case WStype_TEXT:
            command_counter=command_counter+1;
#ifdef DEBUG
            Serial.print("command_counter: ");
            Serial.println(command_counter);
            Serial.printf("[%u] get Text: %s at time %u\n", num, payload,millis());
#endif
            if(payload[0] == '#') {
                //array to string  
                String payloadtext = (char*) payload;
                
                //deal with commands
                process_commands(payloadtext);

            }
            break;
    }
}
void loop() {
    // put your main code here, to run repeatedly:
    webSocket.loop();
    if(useAP){
      dnsServer2.processNextRequest();
    }
    if(useMQTT){
        if (!client.connected()) {
          mqtt_reconnect();
         }
        client.loop();
    }

    //yield();
}
void setupWiFi()
{
  WiFi.softAPdisconnect();
  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
  delay(100);
  // Do a little work to get a unique-ish name. Append the
  // last two bytes of the MAC (HEX'd) to "Thing-":
  uint8_t mac[WL_MAC_ADDR_LENGTH];
  WiFi.softAPmacAddress(mac);
  String macID = String(mac[WL_MAC_ADDR_LENGTH - 2], HEX) +
                 String(mac[WL_MAC_ADDR_LENGTH - 1], HEX);
  macID.toUpperCase();
  String AP_NameString = "ROBOT WIFI " + macID;  
  char AP_NameChar[AP_NameString.length() + 1];
  memset(AP_NameChar, 0, AP_NameString.length() + 1);
  for (int i=0; i<AP_NameString.length(); i++){
      AP_NameChar[i] = AP_NameString.charAt(i);
  }

  WiFi.softAPConfig(apIP2, apIP2, IPAddress(255, 255, 255, 0));

  // if DNSServer is started with "*" for domain name, it will reply with
  // provided IP to all DNS request
  dnsServer2.start(DNS_PORT2, "*", apIP2);
  
  WiFi.softAP(AP_NameChar);
}
//-----------------mqtt--------------------
void callback(char* topic, byte* payload, unsigned int length){
     String payloadtext="#";
     for (int i = 0; i < length; i++) {
       payloadtext+=(char)payload[i];
     }
#ifdef DEBUG
     Serial.print("Message arrived [");
     Serial.print(topic);
     Serial.print("] ");
     Serial.println(payloadtext);
#endif
     process_commands(payloadtext);
}

void mqtt_reconnect(){
  while(!client.connected()){
#ifdef DEBUG
     Serial.print("Attempting MQTT connection...");
#endif
    // Attempt to connect
    if (client.connect(esp_id)) {
#ifdef DEBUG
      Serial.println("connected");
#endif
      webSocket.sendTXT(0,esp_id);
      client.subscribe(esp_id);
    } else {
#ifdef DEBUG
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
#endif
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}
//--------------------OTA-----------------------
bool checkForUpdate(String url){
  url += macID;
  HTTPClient httpClient;
  String url_version = url+"/version";
  httpClient.begin( url_version );
  int httpCode = httpClient.GET();
  if( httpCode == 200 ) {
    String newFWVersion = httpClient.getString();
    int newVersion = newFWVersion.toInt();
    if(newVersion>currVersion){
        webSocket.sendTXT(0,"!UPDATING");
        String url_file = url+"/AutoConnect.ino.nodemcu.bin";
        t_httpUpdate_return ret = ESPhttpUpdate.update(url_file);
        switch(ret) {
          case HTTP_UPDATE_FAILED:  
            Serial.printf("HTTP_UPDATE_FAILED Error (%d): %s",  ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());       
            String txt = "!UPDATE_FAILURE, ";
            txt+=ESPhttpUpdate.getLastErrorString();
            webSocket.sendTXT(0,txt);
            break;
        }
    }else{
        webSocket.sendTXT(0,"!NO_UPDATE");
    }
  }else{
     webSocket.sendTXT(0,"!NO_INTERNET");
  }
  httpClient.end();
}
//--------------------helper----------------------
void process_commands(String payloadtext){
   //1: cmd, 2: pin, 3: value
    String cmd="",ip="";
    int pin=-1;
    int value=-1,value1=-1;
    
    int index=1,endCommand=1,endCommandNext=0;
    while((endCommandNext = payloadtext.indexOf(',',endCommandNext+1))!=-1){//Process commands
        if(index==1){
          cmd = payloadtext.substring(endCommand,endCommandNext);
        }else if(index==2){
          if(!strcmp(&cmd[0],"MQTT")){
            ip = payloadtext.substring(endCommand,endCommandNext);
          }else{
            pin = payloadtext.substring(endCommand,endCommandNext).toInt();
          }
        }else if(index==3){
          value = (payloadtext.substring(endCommand,endCommandNext)).toInt();
        }else if(index==4){
          value1 = (payloadtext.substring(endCommand,endCommandNext)).toInt();
        }
        //update
        index++;
        endCommand = endCommandNext+1;
    }
#ifdef DEBUG
    Serial.print("Command received: ");
    Serial.println(cmd);
    Serial.printf("Pin: %d\n",pin);
    if(value!=-1){
        Serial.printf("Value to be set: %d\n",value);
    }
#endif
    if(!strcmp(&cmd[0],"DR")){
        if(pin_set[pin]!='I'){
            pinMode(pin,INPUT);
            pin_set[pin] = 'I';
        }
        int val = digitalRead(pin);
        char str[3];
        str[0] = '#';
        str[1] = '0'+val;
        str[2] = 0;
        webSocket.sendTXT(0,str);
    }else if(!strcmp(&cmd[0],"AR")){
        if(pin_set[pin]!='I'){
            pinMode(pin,INPUT);
            pin_set[pin] = 'I';
        }
        int val = analogRead(pin);
        char str[12];
        str[0] = '#';
        itoa(val,(char*)str+1,10);
        webSocket.sendTXT(0,str);
    }else if(!strcmp(&cmd[0],"DW")){
        if(pin_set[pin]!='O'){
            pinMode(pin,OUTPUT);
            pin_set[pin] = 'O';
        }else{
            noTone(pin);
        }
        digitalWrite(pin,abs(value));
    }else if(!strcmp(&cmd[0],"AW")){
        if(pin_set[pin]!='O'){
            pinMode(pin,OUTPUT);
            pin_set[pin] = 'O';
        }else{
            noTone(pin);
        }
        analogWrite(pin,abs(value));
    }else if(!strcmp(&cmd[0],"Servo")){
        if(pin_set[pin]!='O'){
            pinMode(pin,OUTPUT);
            pin_set[pin] = 'O';
        }
        if(!myservo[pin].attached()){
            myservo[pin].attach(pin);
        }
        noTone(pin);
        myservo[pin].write(value);
    }else if(!strcmp(&cmd[0],"DETACHALL")){
        for(int i=0;i<17;i++){
            if(myservo[i].attached()){
                myservo[i].detach();
            }
        }
    }else if(!strcmp(&cmd[0],"STARTSONAR")){
        NewPingESP8266 sonar(pin, value, value1);
        char str[5];
        memset(str,0,5);
        str[0]='#';
        int dist = sonar.ping_cm(); // Send ping, get distance in cm and print result (0 = outside set distance range)
        itoa(dist,(char*)str+1,10);
        webSocket.sendTXT(0,str);
    }else if(!strcmp(&cmd[0],"Tone")){
        if(pin_set[pin]!='O'){
            pinMode(pin,OUTPUT);
            pin_set[pin] = 'O';
        }
        tone(pin,value,value1);
//        delay(value1);
//        noTone(pin);
    }else if(!strcmp(&cmd[0],"UPDATE")){
        checkForUpdate("http://172.16.1.92:8888/");
    }else if(!strcmp(&cmd[0],"MQTT")){
        client.setServer(&ip[0], 1883);
        client.setCallback(callback);
        useMQTT = true;
    }else if(!strcmp(&cmd[0],"MQTTDISCONNECT")){
        if(!client.connected()){
            client.disconnect();
            useMQTT = false;
        }
    }
}

