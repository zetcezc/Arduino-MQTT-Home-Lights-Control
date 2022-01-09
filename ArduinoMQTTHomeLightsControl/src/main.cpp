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
3. Set up table of buttons and connection of buttons to leds by filling in table buttons2led.
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

0.5 - redesigned MQTT protocol instead of all switched using single topis and number as a payload - each switch and led 
      has its own topic ending with "/nubmer| (".../54");
      Frot testing - switch 99 (MQTT) turns every led for 1 sec in sequence.

0.6 - added MQTT control for single leds

*/


#include <IoAbstraction.h>
#include <IoAbstractionWire.h>
#include <TaskManagerIO.h>
#include <Wire.h>
#include <Ethernet.h>
#include <PubSubClient.h>
#include <EEPROM.h>
#include <string.h>

// Serial prints only in this mode (under implementation)
#define debugOn 1

#define switchSetTopic "arduino01/switch/set"
#define switchStateTopic "arduino01/switch/state"
#define ledSetTopic "arduino01/led/set"
#define ledStateTopic "arduino01/led/state"

//define ON/OFF for low triggered output PINs
#define ON 0
#define OFF 1

//Setting up Ethernet shield
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xAD };
IPAddress ip(192, 168, 1, 203); // Arduino IP address
IPAddress myDns(192, 168, 1, 1); //DNS ip
IPAddress mqttBrokerIp(192, 168, 1, 11); // MQTT broker IP adress
#define mqttUser "homeassistant"
#define mqttPasswd "aih1xo6oqueazeSa5oojootebo6Baj0aochizeThaighieghahdieBeco7phei7s"
char message_buff[100];

// to make life easier, probably define each expander
#define EXPANDER1 100

// create a multi Io that allocates the first 100 pins to arduino pins
MultiIoAbstractionRef multiIo = multiIoExpander(EXPANDER1);

EthernetClient ethClient;
PubSubClient mqttClient(mqttBrokerIp, 1883, ethClient);

//initiate table of leds (Expander PINS) - output. Max = 8x8=64 on PCF8574's
//define initial state. Will be used if no EEPROM value found.
uint8_t leds[][2] = 
  {  {100,OFF},{101,OFF},{102,OFF},{103,OFF},{104,OFF},{105,OFF},{106,OFF},{107,OFF}
    ,{110,OFF},{111,OFF},{112,OFF},{113,OFF},{114,OFF},{115,OFF},{116,OFF},{117,OFF}
    ,{120,OFF},{121,OFF},{122,OFF},{123,OFF},{124,OFF},{125,OFF},{126,OFF},{127,OFF}
    ,{130,OFF},{131,OFF},{132,OFF},{133,OFF},{134,OFF},{135,OFF},{136,OFF},{137,OFF}
    ,{140,OFF},{141,OFF},{142,OFF},{143,OFF},{144,OFF},{145,OFF},{146,OFF},{147,OFF}
    ,{150,OFF},{151,OFF},{152,OFF},{153,OFF},{154,OFF},{155,OFF},{156,OFF},{157,OFF}
    //,{160,OFF},{161,OFF},{162,OFF},{163,OFF},{164,OFF},{165,OFF},{166,OFF},{167,OFF}
    //,{170,OFF},{171,OFF},{172,OFF},{173,OFF},{174,OFF},{175,OFF},{176,OFF},{177,OFF}
  };


//Define number of switches (outputs/LEDs)
size_t noOfLeds = sizeof(leds) / sizeof(leds[0]);

uint8_t currentEEPROMValue=250;

//this is the number of leds that can be mananaged by single switch
#define maxNoOfLedsPerButton 10

/*
Define which switches control which leds. First number in a row is a switch PIN number, then come leds PIN numbers.
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
    {54,101,112,123,134,145,156,-1,-1,-1,-1}, //A0
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
    {68,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}, //A14
    {90,113,-1,-1,-1,-1,-1,-1,-1,-1,-1}, //MQTT virtual switch test
    //{99,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}, //MQTT test - all leds sequence
    {100,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}, //to make outputs on expander 0x20 work
    {110,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},  //to make outputs on expander 0x21 work
    {120,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}, //to make outputs on expander 0x22 work
    {130,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},  //to make outputs on expander 0x23 work
    {140,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}, //to make outputs on expander 0x24 work
    {150,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}  //to make outputs on expander 0x25 work
  };

//count the number of buttons
size_t noOfButtons = sizeof(button2leds) / sizeof(button2leds[0]);

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
      Serial.print(leds[i][0]);
      Serial.print(" = ");
      Serial.println(EEPROM.read(i));
    }
}

void saveLedStatesToEeprom(uint8_t switchedLed, uint8_t ledState)
{
  for (size_t i=0; i<noOfLeds; i++)
  {
    if (leds[i][0] == switchedLed)
    {
      leds[i][1] = ledState;
    } 
    EEPROM.update(i,leds[i][1]);
  }
  
}

void onSwitchPressed(uint8_t key, bool held); //just the declaration here


// Publish ledState as payload to MQTT topic named topic/key
void mqttPublishState(String topic, uint8_t key, uint8_t ledState)
{   
  
  String keyStr = String(key).c_str();
  String ledStateStr= ledState ? "OFF" : "ON";
  char topicChar[50] = {"\0"};
  String topicStr = String(topic).c_str();  
  topicStr = topicStr + "/" + keyStr;
  topicStr.toCharArray(topicChar,topicStr.length()+1);
  String payloadStr = ledStateStr;
  char payloadChar[5] = {"\0"};
  payloadStr.toCharArray(payloadChar,payloadStr.length()+1);
  int8_t publishResult = mqttClient.publish(topicChar, payloadChar, true);
  if (debugOn)
  {
    Serial.print("Published message: ");
    Serial.print(payloadStr);
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
  payloadTemp.toUpperCase();
  String topicStr = String(topic);
  String topicPrefixStr = topicStr.substring(0,topicStr.lastIndexOf('/'));
  String topicSuffixStr = topicStr.substring(topicStr.lastIndexOf('/')+1);
  
  Serial.println(topicPrefixStr);
  Serial.println(topicSuffixStr);

  uint8_t mqttKey=atoi(String(topicSuffixStr).c_str());
  uint8_t payloadInt = 2;
  
  if (topicPrefixStr.equals(switchSetTopic))
  { onSwitchPressed(mqttKey, false);
    Serial.println("Switch pressed by MQTT message");
  }
  else if (topicPrefixStr.equals(ledSetTopic)) 
  {
    uint8_t ledState = ioDeviceDigitalReadS(multiIo, mqttKey);
    if (payloadTemp.equals("1")||payloadTemp.equals("ON"))
    {
      payloadInt = ON;
      ioDeviceDigitalWrite(multiIo, mqttKey, payloadInt);
      saveLedStatesToEeprom(mqttKey,payloadInt); 
      mqttPublishState(ledStateTopic, mqttKey, ledState);
      Serial.println("Led turned on/off by MQTT message");
    }
    else if (payloadTemp.equals("0")||payloadTemp.equals("OFF"))
    {
      payloadInt = OFF;
      ioDeviceDigitalWrite(multiIo, mqttKey, payloadInt);
      saveLedStatesToEeprom(mqttKey,payloadInt);
      mqttPublishState(ledStateTopic, mqttKey, ledState);
      Serial.println("Led turned on/off by MQTT message");
    }
  }
}

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

// when the switch is pressed then this function will be called.
void onSwitchPressed(uint8_t key, bool held)
{ uint8_t ledState = 2;
  if (key == 2)
  {
    clearEeprom();
  } else if (key == 3) 
    {
       for(size_t i=0; i<noOfButtons; i++)
          for(int j=1;j<maxNoOfLedsPerButton+1;j++) 
            { 
              ioDeviceDigitalWrite(multiIo, button2leds[i][j], OFF);
            }
        Serial.println("All leds off");
    } 
    /*  else if (key == 99) 
    {
       for (size_t i=0; i<noOfLeds; i++)
            { 
              ioDeviceDigitalWriteS(multiIo, leds[i][0], ON);
              Serial.print("Led no: ");
              Serial.print(leds[i][0]);
              Serial.println(" ON");
              delay(1000);
              ioDeviceDigitalWriteS(multiIo, leds[i][0], OFF);
              Serial.print("Led no: ");
              Serial.print(leds[i][0]);
              Serial.println(" OFF");
            }
        Serial.println("Leds sequence done.");
    } 
    */ 
    else
    {
      for(size_t i=0; i<noOfButtons; i++)
      { if(button2leds[i][0]==key)
        { for(int j=1;j<maxNoOfLedsPerButton+1;j++) 
          if (button2leds[i][j] != -1)
          { 
            ledState = ioDeviceDigitalReadS(multiIo, button2leds[i][j]);
            ioDeviceDigitalWrite(multiIo, button2leds[i][j], !ledState);
            if (debugOn)
            { 
              Serial.print("LedState of leds ");
              Serial.print(button2leds[i][j]);
              Serial.print(" = ");
              Serial.println(!ledState);
            }
            saveLedStatesToEeprom(button2leds[i][j],!ledState);
            mqttPublishState(ledStateTopic, button2leds[i][j], !ledState);
          }
        }
      }
            ioDeviceSync(multiIo); // force another sync
      Serial.print("Switch "); 
      Serial.print(key);
      Serial.println(held ? " Held down" : " Pressed");
      //serialPrintEeprom();
      mqttPublishState(switchStateTopic, key, !ledState);
    }
}

int8_t subscribeToTopic(String topic, uint16_t key)
{
  String keyStr = String(key).c_str();
  char topicChar[50] = {"\0"};
  String topicStr = topic;
  topicStr = topicStr + "/" + keyStr;
  topicStr.toCharArray(topicChar,topicStr.length()+1);
  int8_t subscribeResult = mqttClient.subscribe(topicChar);
  if (debugOn)
  {
    Serial.print("Subscribed to topic: ");
    Serial.print(topicChar);
    Serial.print(" with the result: ");
    Serial.println(subscribeResult);
  }
  return  subscribeResult;
}


// traditional arduino setup function
void setup() {
  Wire.begin();
  Serial.begin(9600);
  // Setup MQTT
    mqttClient.setCallback(callback);
  Ethernet.begin(mac, ip, myDns);
  Serial.println(Ethernet.localIP()); //Print Arduino IP adddress
  // Connnect to MQTT broker: 5 times every (2 * no of the try) seconds, then Arduino only mode
  boolean mqttConnected = mqttConnect();
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
 
  // Define Arduino PINs as INPUT. Initialise pullup switches
  switches.initialise(multiIo, true);
  for (size_t i=0; i<noOfButtons; i++)
  {
    switches.addSwitch(button2leds[i][0], onSwitchPressed); 
    ioDevicePinMode(multiIo, button2leds[i][0], INPUT_PULLUP);
    if (mqttConnected && button2leds[i][0]<100)                 //skip expander's fake input switches
        subscribeToTopic(switchSetTopic, button2leds[i][0]); 
  }

  // Define Expanders PINs as OUTPUT
  for (size_t i=0; i<noOfLeds; i++) 
  {
    ioDevicePinMode(multiIo, leds[i][0], OUTPUT); // Set mode of the PIN number which is stored in table "leds" under address "i" as output
    EEPROM.get(i,currentEEPROMValue); // Read EEPROM value stored under the address "i"; value LOW = -256, HIGH = -255, no value before = -1.
    //Serial.print("EEPROM=");
    //Serial.println(currentEEPROMValue);
    if (currentEEPROMValue == 0 || currentEEPROMValue == 1 )       // If there is either LOW or HIGH stored - set pin state to previously stored value
    { 
      leds[i][1] = currentEEPROMValue;
    }                             
    ioDeviceDigitalWrite(multiIo, leds[i][0], leds[i][1]);
    subscribeToTopic(ledSetTopic, leds[i][0]);
    /*
    Serial.print("PIN ");
    Serial.print(leds[i][0]);
    Serial.print(" set to Output with initial value ");
    Serial.println(leds[i][1]);
    */
  }
  ioDeviceSync(multiIo); // force another sync
  Serial.println("Setup is done!");
}

void loop() 
{
  taskManager.runLoop();
  mqttClient.loop();
}