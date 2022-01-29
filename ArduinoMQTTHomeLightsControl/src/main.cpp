/*

ArduinoHomeLightsControl
Version 0.6
Date: 2022-JAN-06

This is the program that can be used to control home lights using Arduino Mega + Ethernet shield + PCF8574 pin expanders.
In addition it uses MQTT communication to send swtich press events to topics and turn on lights by incoming MQTT messeges.
In my project I ise Arduino Mega pins + 2 x PC8574 epanders defined as input pins which gives me 71 INPUT PINS 
(55 in Arduino and 16 in expanders, called "buttons" in the sketch) and 6 x PCF8574 epanders to achieve 48 OUTPUT PINS 
( called "leds" in the sketch). The PINS can be reconfigured according to the need.

I'm using io-abstraction library to get all pins together.
https://www.thecoderscorner.com/products/arduino-libraries/io-abstraction/

Check MultiIoAbstraction for more details.
https://www.thecoderscorner.com/products/arduino-libraries/io-abstraction/arduino-pins-and-io-expanders-same-time/

State of leds is stored in EEPROM, so after the controler reset - the light are back. EEPROM overrides initial states of light.
You can erase EEPROM by pressing swich 2 on Arduino.

Configuration:
1. Set up IP address of arduino, DNS, MQTT broker IP
2. Set up table of lights (called "leds") by entering there every light with it initial state (ON/OFF)
3. Set up table of buttons and connection between buttons and leds by filling in table button2leds.
   Each row is on button, posision 0 defines button PIN number, positions 1-10 define leds PIN numbers that should be 
   switched by the button
4. REMARK: Do not use pins: 
    - 0,1, 4,5, 10, 13, 50,51,52,53 if using Ethernet shield 
    - analog IN 20,21 if using I2C expanders (for instance PCF8574)
    - PIN 2 and 3 are set up as special pins: 2 clears EEPROM, 3 turns off all leds. 
    This makes still 52 available PINs of Arduino Mega.
5. I discovered strange behavior of the set up: if you want to use all the PINS of the PCF8574 Expander as outputs - you 
   have to define one of PIN of each expander both as input and output. It still works then OK as Output. 
   If you know the reason - please let me know.

VERSION NOTES:


0.4 - MQTT now publishes state to mqttStateTopic when button pressed in a format key-held-ledState (54-0-1).
    - Added clering of retain buffer in setup. Without it - retained messeges have pressed last used button after restart
    - special button "3" now turns off all leds

0.5 - redesigned MQTT protocol instead of all switched using single topis and number as a payload - each button and led 
      has its own topic ending with "/nubmer| (".../54");
      Frot testing - button 99 (MQTT) turns every led for 1 sec in sequence.

0.6 - added MQTT control for single leds

0.6.2 - MQTT improvements:
          - bugfixes and code sanity
          - button state sent in payload can be "pressed" or "hold down"
          - light state boradcasted in format: "on,255,255-255-255" (on/off + brightness + RGB brightnesses)
          - tested with HA light MQTT component (template) - payload sent as string
          - 
0,6,3 - MQTT improvements:
          - payload as JSON

0.6.4 - MQTT improvements
          - autodiscovery of leds

0.6.5 - MQTT bugfixes
          - problem with retaining messages

0.6.6 - MQTT improvemets
          - buttons auto discovery          
*/


#include <IoAbstraction.h>
#include <IoAbstractionWire.h>
#include <TaskManagerIO.h>
#include <Wire.h>
#include <Ethernet.h>
#include <PubSubClient.h>
#include <EEPROM.h>
#include <string.h>
#include <ArduinoJson.h>


// Serial prints only in this mode (under implementation)
#define debugOn 0

#define buttonSetTopic "arduino01/button/set"
#define buttonStateTopic "arduino01/button/state"
#define ledSetTopic "arduino01/led/set"
#define ledStateTopic "arduino01/led/state"

//define ON/OFF for low triggered output PINs
#define ON 0
#define OFF 1
#define ledsAutoDiscovery 1
#define buttonsAutoDiscovery 1

//Setting up Ethernet shield
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xAD };
IPAddress ip(192, 168, 1, 203); // Arduino IP address
IPAddress myDns(192, 168, 1, 1); //DNS ip
IPAddress mqttBrokerIp(192, 168, 1, 11); // MQTT broker IP adress
#define mqttUser "homeassistant"
#define mqttPasswd "aih1xo6oqueazeSa5oojootebo6Baj0aochizeThaighieghahdieBeco7phei7s"

// no of PINS reserved for Arduino; first expander's PIN will start from EXPANDER1 value
#define ArduinoPins 160
#define startLedNo 160

boolean mqttConnected = 0;

// create a multi Io that allocates the first "#define ArduinoPins" pins to arduino pins
MultiIoAbstractionRef multiIo = multiIoExpander(ArduinoPins);

EthernetClient ethClient;
PubSubClient mqttClient(mqttBrokerIp, 1883, ethClient);

struct led
{ 
  uint8_t ledNo;
  boolean ledState;
  boolean ledAutoDiscovery;
  char ledName[10];
};

//initiate table of leds (Expander PINS) - output. Max = 8x8=64 on PCF8574's
//define initial state. Will be used if no EEPROM value found.
//By default ledAutoDiscovery is set to 0. Change to 1 for leds that shoudl be visible in HomeAssistant.
led leds[] = 
  {  {startLedNo,OFF,1,"Led 100"},{startLedNo+1,OFF,1,"Led 101"},{startLedNo+2,OFF,1,"Led 102"},{startLedNo+3,OFF,0,"Led 103"},{startLedNo+4,OFF,0,"Led 104"},{startLedNo+5,OFF,0,"Led 105"},{startLedNo+6,OFF,0,"Led 106"},{startLedNo+7,OFF,0,"Led 107"}
    ,{startLedNo+10,OFF,0,"Led 110"},{startLedNo+11,OFF,0,"Led 111"},{startLedNo+12,OFF,0,"Led 112"},{startLedNo+13,OFF,0,"Led 113"},{startLedNo+14,OFF,0,"Led 114"},{startLedNo+15,OFF,0,"Led 115"},{startLedNo+16,OFF,0,"Led 116"},{startLedNo+17,OFF,0,"Led 117"}
    ,{startLedNo+20,OFF,0,"Led 120"},{startLedNo+21,OFF,0,"Led 121"},{startLedNo+22,OFF,0,"Led 122"},{startLedNo+23,OFF,0,"Led 123"},{startLedNo+24,OFF,0,"Led 124"},{startLedNo+25,OFF,0,"Led 125"},{startLedNo+26,OFF,0,"Led 126"},{startLedNo+27,OFF,0,"Led 127"}
    ,{startLedNo+30,OFF,0,"Led 130"},{startLedNo+31,OFF,0,"Led 131"},{startLedNo+32,OFF,0,"Led 132"},{startLedNo+33,OFF,0,"Led 133"},{startLedNo+34,OFF,0,"Led 134"},{startLedNo+35,OFF,0,"Led 135"},{startLedNo+36,OFF,0,"Led 136"},{startLedNo+37,OFF,0,"Led 137"}
    ,{startLedNo+40,OFF,0,"Led 140"},{startLedNo+41,OFF,0,"Led 141"},{startLedNo+42,OFF,0,"Led 142"},{startLedNo+43,OFF,0,"Led 143"},{startLedNo+44,OFF,0,"Led 144"},{startLedNo+45,OFF,0,"Led 145"},{startLedNo+46,OFF,0,"Led 146"},{startLedNo+47,OFF,0,"Led 147"}
    ,{startLedNo+50,OFF,0,"Led 150"},{startLedNo+51,OFF,0,"Led 151"},{startLedNo+52,OFF,0,"Led 152"},{startLedNo+53,OFF,0,"Led 153"},{startLedNo+54,OFF,0,"Led 154"},{startLedNo+55,OFF,0,"Led 155"},{startLedNo+56,OFF,0,"Led 156"},{startLedNo+57,OFF,0,"Led 157"}
    ,{startLedNo+60,OFF,0,"Led 160"},{startLedNo+61,OFF,0,"Led 161"},{startLedNo+62,OFF,0,"Led 162"},{startLedNo+63,OFF,0,"Led 163"},{startLedNo+64,OFF,0,"Led 164"},{startLedNo+65,OFF,0,"Led 165"},{startLedNo+66,OFF,0,"Led 166"},{startLedNo+67,OFF,0,"Led 167"}
    ,{startLedNo+70,OFF,0,"Led 170"},{startLedNo+71,OFF,0,"Led 171"},{startLedNo+72,OFF,0,"Led 172"},{startLedNo+73,OFF,0,"Led 173"},{startLedNo+74,OFF,0,"Led 174"},{startLedNo+75,OFF,0,"Led 175"},{startLedNo+76,OFF,0,"Led 176"},{startLedNo+77,OFF,0,"Led 177"}
  };

//Define number of buttons (outputs/LEDs)
size_t noOfLeds = sizeof(leds) / sizeof(leds[0]);

uint8_t currentEEPROMValue=250;

//this is the number of leds that can be mananaged by single button
#define maxNoOfLedsPerButton 10

/* Define which buttons control which leds. First number in a row is a button PIN number, then come leds PIN numbers.
Please make sure to add line for EACH ADDED eapander:
  {100,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
where 100 should be replaced by one of expander's pins.
independently you can define this PIN as output.
Without this trick Outputs on the expander don't work ;)
*/

int16_t  button2leds[][maxNoOfLedsPerButton+1] = 
  { {2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}, //this one clears EEPROM
    {3,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}, //this one to turn all off
    {6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {8,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {9,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {12,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {14,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {15,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {16,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {17,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {18,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {19,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {22,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {23,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {24,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {25,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {26,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {27,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {28,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {29,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {30,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {31,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {32,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {33,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {34,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {35,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {36,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {37,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {38,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {39,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {40,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {41,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {42,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {43,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {44,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {45,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {46,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {47,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {48,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {49,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {54,0,1,2,-1,-1,-1,-1,-1,-1,-1}, //A0
    {55,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}, //A1
    {56,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}, //A2
    {57,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}, //A3
    {58,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}, //A4
    {59,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}, //A5
    {60,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}, //A6
    {61,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}, //A7
    {61,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}, //A8
    {63,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}, //A9
    {64,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}, //A10
    {65,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}, //A11
    {66,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}, //A12
    {67,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}, //A13
    {68,13,-1,-1,-1,-1,-1,-1,-1,-1,-1}, //A14
    //{90,113,-1,-1,-1,-1,-1,-1,-1,-1,-1}, //MQTT virtual button test - always get pressed and hod down on startup (led blinks once)
    //{99,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}, //MQTT test - all leds sequence
    {160,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}, //to make outputs on expander 0x20 work
    {170,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},  //to make outputs on expander 0x21 work
    {180,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}, //to make outputs on expander 0x22 work
    {190,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},  //to make outputs on expander 0x23 work
    {200,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}, //to make outputs on expander 0x24 work
    {210,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}  //to make outputs on expander 0x25 work
  };

//count the number of buttons
size_t noOfButtons = sizeof(button2leds) / sizeof(button2leds[0]);

struct button
{ 
  int16_t buttonNo;
  boolean buttonAutoDiscovery;
  char buttonName[20];
};


// Should a button be auto discovered via mqtt - add a row with the number, then "1" and the name
// change 1 to 0 if you want to remove from auto discovery
button buttons[] = 
{
  {2,1,"CLR EEPROM"},
  {3,1,"All Leds OFF"},
  {68,1,"Pokój dizenny"}
};

size_t noOfButtons2 = sizeof(buttons) / sizeof(buttons[0]);

void clearEeprom()
{
  for (size_t i = 0; i < noOfLeds; ++i) 
    {
      EEPROM.write(i, -1);
    }
    Serial.println ("EEPROM Cleared");
}

void serialPrintEeprom()
{
   for (size_t i = 0; i < noOfLeds; ++i) 
    {
      Serial.print(leds[i].ledNo);
      Serial.print(" = ");
      Serial.println(EEPROM.read(i));
    }
}

void saveLedStatesToEeprom(uint8_t switchedLed, uint8_t ledState)
{
  for (size_t i=0; i<noOfLeds; i++)
  {
    if (leds[i].ledNo == switchedLed)
    {
      leds[i].ledState = ledState;
    } 
    EEPROM.update(i,leds[i].ledState);
  }
  
}

void onSwitchPressed(uint8_t key, bool held); //just the declaration here

//Connect to MQTT broker
boolean mqttConnect() 
{
  uint8_t count=0;
  int waitTime=2000;
  while (!mqttClient.connected() && count<5) 
  {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect (5 times - each time wait 2 seconds longer than before)
    if (mqttClient.connect("arduinoClient", mqttUser, mqttPasswd)) 
    {
      Serial.println("Connected");
    } else 
      {
        Serial.print("failed, rc=");
        Serial.print(mqttClient.state());
        Serial.println(" try again in 2 seconds");
        count++;
        // wait 2 seconds to repeat
        delay(count*waitTime);
      }
  }
  if (count < 5) 
  {
    return 1;
  }
  else
  { 
    Serial.println("MQTT Connection failed. Arduino only mode.");
    return 0;
  }
    
}

//subsribe to topic constructed of topic/key
int8_t mqttSubscribeToTopic(String topic, uint16_t key)
{
  String keyStr = String(key).c_str();
  char topicChar[50] = {"\0"};
  String topicStr = topic;
  topicStr = topicStr + "/" + keyStr;
  topicStr.toCharArray(topicChar,topicStr.length()+1);
  int8_t subscribeResult = mqttClient.subscribe(topicChar);
/*  if (debugOn)
  {
    Serial.print("Subscribed to topic: ");
    Serial.print(topicChar);
    Serial.print(" with the result: ");
    Serial.println(subscribeResult);
  }
*/
  return  subscribeResult;
}

// Publish keyState as payload to MQTT topic named topic/key
void mqttPublishState(String topic, uint16_t key, uint8_t keyState)
{ 
  //DynamicJsonDocument doc(1024);
  StaticJsonDocument<512> doc;
  String keyStr = String(key).c_str();
  //Serial.print("Key in mqqPublish = ");
  //Serial.println(key);
  if (key>=startLedNo)
  {   //Serial.println("I'm in if");
      doc["state"] = keyState ? "off" : "on";
      //Serial.print("keyState = ");
      //Serial.println(keyState);    
      //doc1["brightness"] = 255;
  }
  else
  {
      doc["state"] = keyState ? "held_down" : "pressed";
  }
  //Serial.print("Before serialize ");
  //Serial.print("doc[state] = ");
  //Serial.println((char*)doc["state"]);
  char topicChar[50] = {"\0"};
  String topicStr = String(topic).c_str();  
  topicStr = topicStr + "/" + keyStr;
  topicStr.toCharArray(topicChar,topicStr.length()+1);
  char payloadChar[512] = {"\0"};
  //int b = 
  serializeJson(doc, payloadChar);
  //Serial.print("bytes = ");
  //Serial.println(b,DEC);
  //Serial.print("After serialize ");
  //Serial.print("doc[state] = ");
  //Serial.println((char*)doc["state"]);
  boolean publishResult = mqttClient.publish(topicChar, payloadChar, true);  
  //Serial.print("payloadChar = ");
  //Serial.println(payloadChar);
  
  if (debugOn)
  {
    Serial.print("Published message: ");
    Serial.println(payloadChar);

    Serial.print(" to topic: ");
    Serial.print(topicStr);
    Serial.print(" with the result: ");
    Serial.println(publishResult);
  }
}


void mqttSendAutoDiscovery(int16_t key, boolean turnON)
{   
  //DynamicJsonDocument doc(1024);
  StaticJsonDocument<512> doc;
  String keyStr = String(key).c_str();
  String topicStr;
  String slash = "/";
  if (key>=startLedNo)
  {
    doc["platform"] = "mqtt";
    doc["schema"] = "template";
    doc["uniq_id"] = "led_"+ keyStr;
    for (uint8_t i=0; i<noOfLeds; i++)
    {
      if (leds[i].ledNo == key) doc["name"] = leds[i].ledName;  
    }
    doc["cmd_t"] = ledSetTopic + slash + keyStr;
    doc["stat_t"] = ledStateTopic + slash + keyStr;
    doc["cmd_on_tpl"] = "{\"state\":\"on\"}";
    doc["cmd_off_tpl"] = "{\"state\":\"off\"}";
    doc["stat_tpl"] = "{{ value_json.state }}";
    doc["qos"] = "1";
    doc["retain"] = "FALSE";
    topicStr = "homeassistant/light/light_" + keyStr +"/config";
  }
  else
  {
    doc["platform"] = "mqtt";
    doc["uniq_id"] = "button_"+keyStr;
    doc["name"] = "Button Name";
    for (uint8_t i=0; i<noOfButtons2; i++)
    {
      if (buttons[i].buttonNo == key) doc["name"] = buttons[i].buttonName;  
    }
    doc["cmd_t"] = buttonSetTopic + slash + keyStr;
    doc["payload_press"] = "{\"state\":\"pressed\"}";
    doc["qos"] = "1";
    doc["retain"] = "FALSE";
    topicStr = "homeassistant/button/button_" + keyStr +"/config";
  }

  char topicChar[50] = {"\0"};
  topicStr.toCharArray(topicChar,topicStr.length()+1);
  char payloadChar[512] = {"\0"};
  int b = 0;
  if (turnON) 
  {
    b = serializeJson(doc, payloadChar);
  }
  //Serial.print("bytes = ");
  //Serial.println(b,DEC);
  boolean publishResult = mqttClient.publish(topicChar, payloadChar, true);
  if (debugOn)
  {
    Serial.print("Published message: ");
    Serial.println(payloadChar);
    Serial.print(" to topic: ");
    Serial.print(topicStr);
    Serial.print(" with the result: ");
    Serial.println(publishResult);
  }
}




void callback(char* topic, byte* payload, unsigned int length) 
{
  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(". Message: ");
  String payloadTemp;
  for (unsigned int i = 0; i < length; i++) 
  {
    Serial.print((char)payload[i]);
    payloadTemp += (char)payload[i];
  }
  Serial.println(".");
  //payloadTemp.toUpperCase();
  //DynamicJsonDocument doc(1024);
  StaticJsonDocument<512> doc;
  deserializeJson(doc, payload);

  String topicStr = String(topic);
  String topicPrefixStr = topicStr.substring(0,topicStr.lastIndexOf('/'));
  String topicSuffixStr = topicStr.substring(topicStr.lastIndexOf('/')+1);
  
  Serial.println(topicPrefixStr);
  Serial.println(topicSuffixStr);

  uint8_t mqttKey=atoi(String(topicSuffixStr).c_str());
  uint8_t payloadInt = 2;
  String payloadState = doc["state"];
  //Serial.println(mqttKey);
  //Serial.println(payloadState);

  if (topicPrefixStr.equals(buttonSetTopic))
  { if (payloadState.equals("0")||payloadState.equals("pressed"))
    {
      onSwitchPressed(mqttKey, false);
      Serial.println("Button pressed by MQTT message");  
    }
    else if (payloadState.equals("1")||payloadState.equals("hold_down"))
    {
      onSwitchPressed(mqttKey, true);
      Serial.println("Button hold down by MQTT message");  
    }
  }
  else if (topicPrefixStr.equals(ledSetTopic)) 
  {
    if (payloadState.equals("1")||payloadState.equals("ON")||payloadState.equals("on"))
    {
      payloadInt = ON;
      ioDeviceDigitalWrite(multiIo, mqttKey, payloadInt);
      saveLedStatesToEeprom(mqttKey,payloadInt); 
      mqttPublishState(ledStateTopic, mqttKey, payloadInt);
      Serial.println("Led turned on by MQTT message");
    }
    else if (payloadState.equals("0")||payloadState.equals("OFF")||payloadState.equals("off"))
    {
      payloadInt = OFF;
      ioDeviceDigitalWrite(multiIo, mqttKey, payloadInt);
      saveLedStatesToEeprom(mqttKey,payloadInt);
      mqttPublishState(ledStateTopic, mqttKey, payloadInt);
      Serial.println("Led turned off by MQTT message");
    }
  }
}


// When the button is pressed then this function will be called (both hardware and MQTT button works).
void onSwitchPressed(uint8_t key, bool held)
{ if (key<startLedNo)
  {
  uint8_t ledState = 2;
  
  
  if (key == 2) //EEPROM clear
  {
    clearEeprom();
  } else if (key == 3)   //Turn off all leds
    {
       for (size_t i=0; i<noOfLeds; i++)
            { 
              ioDeviceDigitalWrite(multiIo, leds[i].ledNo, OFF);
              saveLedStatesToEeprom(leds[i].ledNo,OFF);
              if (mqttConnected) mqttPublishState(ledStateTopic, leds[i].ledNo, OFF);
              Serial.print("Led no: ");
              Serial.print(leds[i].ledNo);
              Serial.println(" OFF");
            }
        ioDeviceSync(multiIo); // force another sync    
    } 
    else 
    {
      for(size_t i=0; i<noOfButtons; i++)
      { if(button2leds[i][0]==key)
        { for(int j=1;j<maxNoOfLedsPerButton+1;j++) 
          if (button2leds[i][j] != -1)
          { 
            ledState = ioDeviceDigitalReadS(multiIo, button2leds[i][j]+startLedNo);
            ioDeviceDigitalWrite(multiIo, button2leds[i][j]+startLedNo, !ledState);
            if (debugOn)
            { 
              Serial.print("LedState of leds ");
              Serial.print(button2leds[i][j]+startLedNo);
              Serial.print(" = ");
              Serial.println(!ledState);
            }
            saveLedStatesToEeprom(button2leds[i][j]+startLedNo,!ledState);
            if (mqttConnected) mqttPublishState(ledStateTopic, button2leds[i][j]+startLedNo, !ledState);
            
          }
        }
      }
      ioDeviceSync(multiIo); // force another sync
      //Serial.print("Button "); 
      //Serial.print(key);
      //Serial.println(held ? " Held down" : " Pressed");
      //serialPrintEeprom();
      //mqttPublishState(buttonStateTopic, key, held);
      if (mqttConnected) mqttPublishState(buttonStateTopic, key, held);
    }
  }
}



// traditional arduino setup function
void setup() {
  Wire.begin();
  Serial.begin(9600);
  // Setup MQTT
  mqttClient.setCallback(callback);
  mqttClient.setBufferSize(512);
  //Ethernet.init(53);
  Ethernet.begin(mac, ip, myDns);
  Serial.println(Ethernet.localIP()); //Print Arduino IP adddress
  // Connnect to MQTT broker: 5 times every (2 * no of the try) seconds, then Arduino only mode
  mqttConnected = mqttConnect();
  // END Setup MQTT

  // Add an 8574 chip that allocates 10 more pins, therefore it goes from 100..109
  multiIoAddExpander(multiIo, ioFrom8574(0x20), 10);
  Serial.println("added an expander at pin 100 to 109");

  // Add an 8574 chip that allocates 10 more pins, therefore it goes from 110..119
  multiIoAddExpander(multiIo, ioFrom8574(0x21), 10);
  Serial.println("added an expander at pin 110 to 119");

  // Add an 8574 chip that allocates 10 more pins, therefore it goes from 120..129
  multiIoAddExpander(multiIo, ioFrom8574(0x22), 10);
  Serial.println("added an expander at pin 120 to 129");

  // Add an 8574 chip that allocates 10 more pins, therefore it goes from 130..139
  multiIoAddExpander(multiIo, ioFrom8574(0x23), 10);
  Serial.println("added an expander at pin 130 to 139");

  // Add an 8574 chip that allocates 10 more pins, therefore it goes from 140..149
  multiIoAddExpander(multiIo, ioFrom8574(0x24), 10);
  Serial.println("added an expander at pin 140 to 149");

  // Add an 8574 chip that allocates 10 more pins, therefore it goes from 150..159
  multiIoAddExpander(multiIo, ioFrom8574(0x25), 10);
  Serial.println("added an expander at pin 150 to 159");

  // Add more expanders here..

  Serial.print("Number of leds defined:");
  Serial.println(noOfLeds);
 
  // Define Arduino PINs as INPUT. Initialise pullup buttons
  switches.initialise(multiIo, true);
  for (size_t i=0; i<noOfButtons; i++)
  {
    switches.addSwitch(button2leds[i][0], onSwitchPressed); 
    ioDevicePinMode(multiIo, button2leds[i][0], INPUT_PULLUP);
    if (mqttConnected && button2leds[i][0]<startLedNo)                 //skip expander's fake input switches
        {
          mqttSubscribeToTopic(buttonSetTopic, button2leds[i][0]); 
        }
  }
  // Initialize mqtt auto discovery
  if (mqttConnected) 
    for (size_t i=0; i<noOfButtons2; i++)
    {
      mqttSendAutoDiscovery(buttons[i].buttonNo, buttons[i].buttonAutoDiscovery);
      //mqttSendAutoDiscovery(buttons[i].buttonNo, 0);
    }


  // Define Expanders PINs as OUTPUT
  for (size_t i=0; i<noOfLeds; i++) 
  {
    ioDevicePinMode(multiIo, leds[i].ledNo, OUTPUT); // Set mode of the PIN number which is stored in table "leds" under address "i" as output
    EEPROM.get(i,currentEEPROMValue); // Read EEPROM value stored under the address "i"; value LOW = -256, HIGH = -255, no value before = -1.
    if (currentEEPROMValue == 0 || currentEEPROMValue == 1 )       // If there is either LOW or HIGH stored - set pin state to previously stored value
    { 
      leds[i].ledState = currentEEPROMValue;
    }                             
    ioDeviceDigitalWrite(multiIo, leds[i].ledNo, leds[i].ledState);
    if (mqttConnected)
    {
      mqttSubscribeToTopic(ledSetTopic, leds[i].ledNo);
      mqttSendAutoDiscovery(leds[i].ledNo, leds[i].ledAutoDiscovery);
    }
    //Serial.print("PIN set as output: ");  
    //Serial.println(leds[i].ledNo);
  }
  ioDeviceSync(multiIo); // force another sync
  Serial.println("Setup is done!");
}

void loop() 
{
  taskManager.runLoop();
  mqttClient.loop();
}