/*

ArduinoMQTTHomeLightsControl
Version 0.7
Date: 2022-JAN-29

This is the program that can be used to control home lights using Arduino Mega + Ethernet shield + up to 16 x PCF8574 pin expanders.
In addition it uses MQTT communication to send button press events to topics and turn on lights by incoming MQTT messeges.
MQTT can be used to enable autodiscovery of lights and buttons. I used HomeAssintant syntax.

In my project I ise Arduino Mega pins + DIY boars containig 8 x PC8574A epanders, defined as input pins, which gives me 118 available INPUT PINS (54 in Arduino and 64 in expanders, called "buttons" in the sketch) 
Additionally I use 8 x PCF8574 epanders to achieve 64 OUTPUT PINS (called "leds" in the sketch). This is the maximum number of PCF8574(A) expanders that can be used.
PINS can be reconfigured according to the need. 

I'm using io-abstraction library to get all pins together.
https://www.thecoderscorner.com/products/arduino-libraries/io-abstraction/
Check MultiIoAbstraction for more details.
https://www.thecoderscorner.com/products/arduino-libraries/io-abstraction/arduino-pins-and-io-expanders-same-time/
Great library - many thanks to TheCodersCorner / Dave Cherry!

State of leds is stored in EEPROM, so after the controler reset - the lights are back. EEPROM overrides initial states of light defined in the code.
You can erase EEPROM by pressing button 2 on Arduino.

Configuration:
1. Set up IP address of arduino, DNS, MQTT broker IP
2. Set up table of lights (called "leds") by entering there every light with it initial state (ON/OFF)
3. Set up table of buttons and connection between buttons and leds by filling in table button2leds.
   Each row is on button, posision 0 defines button PIN number, next positions in a row define leds PIN numbers that should be 
   switched by the button
4. REMARK: Do not use pins: 
    - 0,1, 4,5, 10, 13, 50,51,52,53 if using Ethernet shield 
    - analog IN 20,21 if using I2C expanders (for instance PCF8574)
    - PIN 2,3 and 6 are set up as special pins: 2 clears EEPROM, 3 turns off all leds, 6 does reset (to do). 
    This makes still 54 available PINs of Arduino Mega.
5. I discovered strange behavior of the set up: if you want to use all the PINS of the PCF8574 Expander as outputs - you 
   have to define one of PIN of each expander both as input and output. It still works then OK as Output. 
   If you know the reason - please let me know.
6. in the ioAbstraction.h - change #define MAX_ALLOWABLE_DELEGATES from 8 to 16

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
0.6.7 - PCF8574A DIY board added - only 3 exapnders of 8.        
0.7.0 - PCF8574A DIY board added
      - button2leds array stored now in flash memory instead of RAM to enable bigger size 
0.7.1 - small code cleanup
0.7.2 - version working with 4 input expanders and 8 output expanders. Added Led names - still to test
1.0.0 - Bugfixes and minor code improvement
      - tested in production environment
1.0.1 - Bugfixes and minor code improvement
      - Light conf adjustment  
1.0.2 - Current production version
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



// Some areas of code shuld be compiled only in production - not in test mode
#define prodMode 1

//debug comments printed
#define debugOn 1

//debug MQTT comments printed
#define mqttDebugOn 0

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
#define ArduinoPins 80
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
  char ledName[14];
};

//initiate table of leds (Expander PINS) - output. Max = 8x8=64 on PCF8574's
//define initial state. Will be used if no EEPROM value found.
//By default ledAutoDiscovery is set to 0. Change to 1 for leds that shoudl be visible in HomeAssistant.
led leds[] = 
  {  {startLedNo,OFF,1,"Antresola"}
    ,{startLedNo+1,OFF,1,"Łaz. prysz."}
    ,{startLedNo+2,OFF,1,"Krysia str."}
    ,{startLedNo+3,OFF,1,"Krysia ref."}
    ,{startLedNo+4,OFF,1,"Susz. sufit"}
    ,{startLedNo+5,OFF,1,"Prac. sufit"}
    ,{startLedNo+6,OFF,1,"Łaz. led"}                 
    ,{startLedNo+7,OFF,1,"Susz. des."}
    
    ,{startLedNo+10,OFF,1,"Janek sam."}
    ,{startLedNo+11,OFF,1,"Syp. EZ ref."}
    ,{startLedNo+12,OFF,1,"Łaz. lustro"}
    ,{startLedNo+13,OFF,1,"Syp. EZ suf."}
    ,{startLedNo+14,OFF,1,"Krysia suf."}
    ,{startLedNo+15,OFF,1,"Prac. biurka"}
    ,{startLedNo+16,OFF,1,"Łaz. sufit"}
    ,{startLedNo+17,OFF,1,"Janek ref."}
    
    ,{startLedNo+20,OFF,1,"Led 20"}
    ,{startLedNo+21,OFF,1,"Led 21"}
    ,{startLedNo+22,OFF,1,"Gospodarcze"}
    ,{startLedNo+23,OFF,1,"Hall duże"}
    ,{startLedNo+24,OFF,1,"Jad. kwia.1"}
    ,{startLedNo+25,OFF,1,"Led 25"}
    ,{startLedNo+26,OFF,1,"Kuch. suf."}
    ,{startLedNo+27,OFF,1,"Led 27"}

    ,{startLedNo+30,OFF,1,"Led 30"}
    ,{startLedNo+31,OFF,1,"Wejście"}
    ,{startLedNo+32,OFF,1,"Hall wejście"}
    ,{startLedNo+33,OFF,1,"Schody"}
    ,{startLedNo+34,OFF,1,"Salon akw.L"}
    ,{startLedNo+35,OFF,1,"Kuch. stół"}
    ,{startLedNo+36,OFF,1,"Salon KL2"}
    ,{startLedNo+37,OFF,1,"WC prysz."}
    
    ,{startLedNo+40,OFF,1,"Salon suf."}
    ,{startLedNo+41,OFF,1,"WC sufit"}
    ,{startLedNo+42,OFF,1,"Garderoba"}
    ,{startLedNo+43,OFF,0,"ERROR"}    // zewnętrzne
    ,{startLedNo+44,OFF,1,"Led 44"}
    ,{startLedNo+45,OFF,1,"Taras bok"}
    ,{startLedNo+46,OFF,1,"Wej.gosp."}
    ,{startLedNo+47,OFF,1,"Taras las"}
    
    ,{startLedNo+50,OFF,1,"Salon KL1"}
    ,{startLedNo+51,OFF,1,"WC lustro"}
    ,{startLedNo+52,OFF,1,"TV kin.tył"}
    ,{startLedNo+53,OFF,1,"Salon KP2"}
    ,{startLedNo+54,OFF,1,"TV sufit"}
    ,{startLedNo+55,OFF,1,"Salon KP1"}
    ,{startLedNo+56,OFF,1,"Kuch. zlew"}
    ,{startLedNo+57,OFF,1,"Jad. kwia.2"}
    
    ,{startLedNo+60,OFF,0,"Kuch. blat"}
    ,{startLedNo+61,OFF,0,"Salon akw.P"}
    ,{startLedNo+62,OFF,0,"Salon buda"}
    ,{startLedNo+63,OFF,0,"Jad. stół"}
    ,{startLedNo+64,OFF,0,"TV kin.przód"}
    ,{startLedNo+65,OFF,0,"Led 65"}
    ,{startLedNo+66,OFF,0,"Led 66"}
    ,{startLedNo+67,OFF,0,"Led 67"}
    
    /*
    ,{startLedNo+70,OFF,0,"Led 70"}
    ,{startLedNo+71,OFF,0,"Led 71"}
    ,{startLedNo+72,OFF,0,"Led 72"}
    ,{startLedNo+73,OFF,0,"Led 73"}
    ,{startLedNo+74,OFF,0,"Led 74"}
    ,{startLedNo+75,OFF,0,"Led 75"}
    ,{startLedNo+76,OFF,0,"Led 76"}
    ,{startLedNo+77,OFF,0,"Led 77"}
    */
  };

//Define number of buttons (outputs/LEDs)
size_t noOfLeds = sizeof(leds) / sizeof(leds[0]);

uint8_t currentEEPROMValue=250;

//this is the number of leds that can be mananaged by single button
#define maxNoOfLedsPerButton 5

/* Define which buttons control which leds. First number in a row is a button PIN number, then come leds PIN numbers.
Please make sure to add line for EACH ADDED eapander:
  {100,vL,vL,vL,vL,vL},
where 100 should be replaced by one of expander's pins.
independently you can define this PIN as output.
Without this trick Outputs on the expander don't work ;)
*/

// vL - voidLed - number representing no led assigned
#define vL 255

const uint8_t  button2leds[][maxNoOfLedsPerButton+1] PROGMEM = 
  { {2,vL,vL,vL,vL,vL}, //this one clears EEPROM
    {3,vL,vL,vL,vL,vL}, //this one to turn all off
    {6,vL,vL,vL,vL,vL}, //this one is wired do reset
    {7,vL,vL,vL,vL,vL},  //P0 Kitchen 1
    {8,5,vL,vL,vL,vL},   //P1 Office room 1
    {9,54,vL,vL,vL,vL},  //P0 TV 4
    {11,54,vL,vL,vL,vL},  //P0 TV 3
    {12,15,vL,vL,vL,vL},  //P1 Office room 2
    {14,26,vL,vL,vL,vL},  //P0 Kitchen 3
    {15,35,vL,vL,vL,vL},  //P0 Kitchen 4
    
    {16,51,vL,vL,vL,vL},  //P0 WC2
    {17,40,vL,vL,vL,vL},  //P0 Salon 3
    {18,64,vL,vL,vL,vL},  //P0 TV 1
    {19,41,vL,vL,vL,vL},  //P0 WC 1
    {22,45,vL,vL,vL,vL},  //P0 Dining N1
    {23,52,vL,vL,vL,vL},  //P0 TV blinds 2
    {24,43,vL,vL,vL,vL},  //P0 Dining N2
    {25,64,vL,vL,vL,vL},  //P0 TV blinds 1
    {26,16,vL,vL,vL,vL},  //P1 Antresola bathroom 1
    {27,23,vL,vL,vL,vL},  //P0 Kitchen 2
    
    {28,13,vL,vL,vL,vL},  //P1 SypEZ 1
    {29,vL,vL,vL,vL,vL},  //P0 Hall 5                         
    {30,02,vL,vL,vL,vL},  //P1 Krysia 3 (dupl. strych)
    {31,54,vL,vL,vL,vL},  //P0 TV blinds 3
    {32,54,vL,vL,vL,vL},  //P0 TV blinds 4
    {33,33,vL,vL,vL,vL},  //P0 stairs 1
    {34,36,50,vL,vL,vL},  //P0 Salon 1
    {35,31,vL,vL,vL,vL},  //P0 Hall 1
    {36,vL,vL,vL,vL,vL},  //P0 Hall 2                        
    {37,34,61,62,vL,vL},  //P0 Salon 7
    
    {38,40,vL,vL,vL,vL},  //P0 Salon 4
    {39,vL,vL,vL,vL,vL},  //P0 gosp drzwi 1
    {40,51,vL,vL,vL,vL},  //P0 WC mirror 
    {41,34,61,62,vL,vL},  //P0 Dining N6
    {42,40,vL,vL,vL,vL},  //P0 Dining N5
    {43,42,vL,vL,vL,vL},  //P0 Wardrobe 2
    {44,vL,vL,vL,vL,vL},  //P0 Hall 3 
    {45,0,vL,vL,vL,vL},   //P1 Antresola SypEZ 2                        
    {46,63,vL,vL,vL,vL},  //P0 Dining N4
    {47,22,vL,vL,vL,vL},   //P1 gosp door 2
    
    {48,40,vL,vL,vL,vL},        //P1 Antresola Janek 4  
    {49,vL,vL,vL,vL,vL},        //P1 Krysia 4
    {54,53,55,vL,vL,vL}, //A0   //P0 Salon 2
    {55,vL,vL,vL,vL,vL}, //A1
    {56,37,vL,vL,vL,vL}, //A2   //P0 WC shower
    {57,vL,vL,vL,vL,vL}, //A3
    {58,vL,vL,vL,vL,vL}, //A4
    {59,vL,vL,vL,vL,vL}, //A5
    {60,vL,vL,vL,vL,vL}, //A6
    {61,vL,vL,vL,vL,vL}, //A7
    
    {62,vL,vL,vL,vL,vL}, //A8
    {63,vL,vL,vL,vL,vL}, //A9
    {64,vL,vL,vL,vL,vL}, //A10
    {65,vL,vL,vL,vL,vL}, //A11
    {66,vL,vL,vL,vL,vL}, //A12
    {67,vL,vL,vL,vL,vL}, //A13
    {68,vL,vL,vL,vL,vL}, //A14
    //Here starts the expander 0x38
    {80,0,vL,vL,vL,vL},  //P1 Antresola Janek 2      
    {81,vL,vL,vL,vL,vL},  //P0 Hall 6                  
    {82,24,57,vL,vL,vL},  //P0 Dining W3
    {83,60,vL,vL,vL,vL},  //P0 Kitchen 6
    {84,17,vL,vL,vL,vL},  //P1 Janek 2
    {85,63,vL,vL,vL,vL},  //P0 Dining W4
    {86,10,vL,vL,vL,vL},  //P1 Janek 1
    {87,0,vL,vL,vL,vL},  //P0 Stairs down 2
    //Here starts the expander 0x3A
    {100,43,vL,vL,vL,vL},  //P0 Dining W2   
    {101,11,vL,vL,vL,vL},  //P1 SypEZ 2
    {102,vL,vL,vL,vL,vL},  //NN
    {103,52,vL,vL,vL,vL},  //P0 TV 2
    {104,3,vL,vL,vL,vL},   //P1 Krysia 2
    {105,23,vL,vL,vL,vL},  //P0 Hall 4                                 
    {106,24,57,vL,vL,vL},  //P0 Dining N3
    {107,25,vL,vL,vL,vL},  //P0 Hall 7
    //Here starts the expander 0x3C
    {120,vL,vL,vL,vL,vL},  //P0 Hall 8 - all off   
    {121,vL,vL,vL,vL,vL},  //NN
    {122,33,vL,vL,vL,vL},  //P1 Antresola Janek 1
    {123,45,vL,vL,vL,vL},  //P0 Dining W1  
    {124,6,vL,vL,vL,vL},  //P1 Antresola bathroom 2   -  ledy nocne
    {125,22,vL,vL,vL,vL},  //P0 gosp 2                                 
    {126,4,vL,vL,vL,vL},  //P1 Washroom 2
    {127,vL,vL,vL,vL,vL},  //NC - no cable
    //Here starts the expander 0x3E
    {140,14,vL,vL,vL,vL},  //P1 Krysia 1   
    {141,42,vL,vL,vL,vL},  //P0 Wardrobe 1
    {142,56,vL,vL,vL,vL},  //P0 Kitchen 5
    {143,12,vL,vL,vL,vL},   //P1 bathroom 1
    {144,42,vL,vL,vL,vL},  //P0 gosp 1                                  
    {145,7,vL,vL,vL,vL},  //P1 Washroom 1
    {146,40,vL,vL,vL,vL},  //P1 Antresola Janek 3
    {147,1,vL,vL,vL,vL},  //P1 bathroom 2
    //Fake INPUT pins which are going to be redefined to OUTPUTS in the next step. 
    //Without this trick - ioAbstraction expanders don't work (at least at my place)
    {startLedNo,vL,vL,vL,vL,vL}, //to make outputs on expander 0x20 work
    {startLedNo+10,vL,vL,vL,vL,vL},  //to make outputs on expander 0x21 work
    {startLedNo+20,vL,vL,vL,vL,vL}, //to make outputs on expander 0x22 work
    {startLedNo+30,vL,vL,vL,vL,vL},  //to make outputs on expander 0x23 work
    {startLedNo+40,vL,vL,vL,vL,vL}, //to make outputs on expander 0x24 work
    {startLedNo+50,vL,vL,vL,vL,vL},  //to make outputs on expander 0x25 work
    {startLedNo+60,vL,vL,vL,vL,vL}, //to make outputs on expander 0x25 work
    {startLedNo+70,vL,vL,vL,vL,vL}  //to make outputs on expander 0x25 work
    
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

/*
  {35,1,"Hall 1"},  
  {36,1,"Hall 2"},  
  //Hall 3 not working
  {105,1,"Hall 4"},  
  {90,1,"Hall 5"},  
  {81,1,"Hall 6"},  
  {151,1,"Hall 7"},  
  {120,1,"Hall 8"},  

  {141,1,"Garder. 1"},
  {94,1,"Garder. 2"},

  {144,1,"Gosp. 1"},
  {125,1,"Gosp. 2"},

  {39,1,"Gosp.D-1"},
  {47,1,"Gosp.D-2"},

  {19,1,"WC 1"},
  {16,1,"WC 1"},

  {152,1,"WC Lust.1"},
  {95,1,"WC Lust.2"},

  {7,1,"Kuchnia 1"},
  {27,1,"Kuchnia 2"},
  {14,1,"Kuchnia 3"},
  {15,1,"Kuchnia 4"},
  //Kuchnia 5 nmie działa
  {83,1,"Kuchnia 6"},

  {34,1,"Salon 1"},
  //Salon 2 nie działa
  {17,1,"Salon 3"},
  {38,1,"Salon 4"},
  //Salon 5 nie działa
  {37,1,"Salon 6"},
  
  {112,1,"Jadal-1"},
  {114,1,"Jadal-2"},
  {82,1,"Jadal-3"},
  {85,1,"Jadal-4"},

  {22,1,"Jadal.T-1"},
  {24,1,"Jadal.T-2"},
  {106,1,"Jadal.T-3"},
  {46,1,"Jadal.T-4"},
  {42,1,"Jadal.T-5"},
  {115,1,"Jadal.T-6"},

  {18,1,"TV 1"},
  {103,1,"TV 2"},
  {11,1,"TV 3"},
  {9,1,"TV 4"},
  {25,1,"TV OKNO-1"},
  {23,1,"TV OKNO-2"},
  {31,1,"TV OKNO-3"},
  {32,1,"TV OKNO-4"},

  {45,1,"Schody dół-1"},
  {87,1,"Schody dół-2"},

  {122,1,"Antr.J 1"},
  {133,1,"Antr.J 2"},
  {146,1,"Antr.J 3"},
  {150,1,"Antr.J 4"},

  {86,1,"Janek 1"},
  {84,1,"Janek 2"},

  {134,1,"Antr.Ł. 1"},
  {124,1,"Antr.Ł. 2"},

  {143,1,"Łazienka 1"},
  {147,1,"Łazienka 2"},

  {135,1,"Pralnia 1"},
  {126,1,"Pralnia 2"},

  {8,1,"Pracownia 1"},
  {12,1,"Pracownia 2"},

  {44,1,"Antr-SEZ-1"},
  {91,1,"Antr-SEZ-2"},

  {113,1,"Syp EZ 1"},
  {101,1,"Syp EZ 2"},

  {140,1,"Krysia 1"},
  {104,1,"Krysia 2"},
  {156,1,"Krysia 3"},
  {154,1,"Krysia 4"},

*/
  
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
  #if mqttDebugOn
  {
    Serial.print("Subscribed to topic: ");
    Serial.print(topicChar);
    Serial.print(" with the result: ");
    Serial.println(subscribeResult);
  }
  #endif
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
  #if mqttDebug
  boolean publishResult = 
  #endif
  mqttClient.publish(topicChar, payloadChar, true);  
  //Serial.print("payloadChar = ");
  //Serial.println(payloadChar);
  
  #if mqttDebugOn
    Serial.print("Published message: ");
    Serial.println(payloadChar);

    Serial.print(" to topic: ");
    Serial.print(topicStr);
    Serial.print(" with the result: ");
    Serial.println(publishResult);
  #endif
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
  #if mqttDebugOn
  boolean publishResult = 
  #endif
  mqttClient.publish(topicChar, payloadChar, true);
  #if mqttDebugOn
    Serial.print("Published message: ");
    Serial.println(payloadChar);
    Serial.print(" to topic: ");
    Serial.print(topicStr);
    Serial.print(" with the result: ");
    Serial.println(publishResult);
  #endif
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
  } else if ((key == 3) || (key == 120))  //Turn off all leds
    {
       for (size_t i=0; i<noOfLeds; i++)
            { 
              ioDeviceDigitalWrite(multiIo, leds[i].ledNo, OFF);
              saveLedStatesToEeprom(leds[i].ledNo,OFF);
              if (mqttConnected) mqttPublishState(ledStateTopic, leds[i].ledNo, OFF);
              #if debugOn
                Serial.print("Led no: ");
                Serial.print(leds[i].ledNo);
                Serial.println(" OFF");
              #endif
            }
        ioDeviceSync(multiIo); // force another sync    
    } 
    else 
    {
      for(size_t i=0; i<noOfButtons; i++)
      { if(pgm_read_byte(&(button2leds[i][0]))==key)
        { for(int j=1;j<maxNoOfLedsPerButton+1;j++) 
          if (pgm_read_byte(&(button2leds[i][j])) != vL)
          { 
            ledState = ioDeviceDigitalReadS(multiIo, pgm_read_byte(&(button2leds[i][j]))+startLedNo);
            ioDeviceDigitalWrite(multiIo, pgm_read_byte(&(button2leds[i][j]))+startLedNo, !ledState);
            uint8_t newLedState = ioDeviceDigitalReadS(multiIo, pgm_read_byte(&(button2leds[i][j]))+startLedNo);
            Serial.print("Led state changed to: ");
            Serial.println(newLedState);
            #if debugOn
              Serial.print("LedState of led: ");
              Serial.print(pgm_read_byte(&(button2leds[i][j])));
              Serial.print(" = ");
              Serial.println(!ledState);
            #endif
            saveLedStatesToEeprom(pgm_read_byte(&(button2leds[i][j]))+startLedNo,!ledState);
            if (mqttConnected) mqttPublishState(ledStateTopic, pgm_read_byte(&(button2leds[i][j]))+startLedNo, !ledState);
            
          }
        }
      }
      ioDeviceSync(multiIo); // force another sync
      #if debugOn
        Serial.print("Button "); 
        Serial.print(key);
        Serial.println(held ? " Held down" : " Pressed");
        //serialPrintEeprom();
      #endif
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
  //Serial.println(sizeof(button2leds));
 
  // Add an 8574A chip that allocates 10 more pins, therefore it goes from startLedNo..startLedNo+9
  multiIoAddExpander(multiIo, ioFrom8574(0x38), 20);
  if (debugOn) Serial.println("added an expander at pin 80 to 89");

  // Add an 8574A chip that allocates 10 more pins, therefore it goes from startLedNo..startLedNo+9
  //multiIoAddExpander(multiIo, ioFrom8574(0x39), 10);
  //if (debugOn) Serial.println("added an expander at pin 90 to 99");

  // Add an 8574A chip that allocates 10 more pins, therefore it goes from startLedNo..startLedNo+9
  multiIoAddExpander(multiIo, ioFrom8574(0x3A), 20);
  if (debugOn) Serial.println("added an expander at pin 100 to 109");

  // Add an 8574A chip that allocates 10 more pins, therefore it goes from startLedNo..startLedNo+9
  //multiIoAddExpander(multiIo, ioFrom8574(0x3B), 10);
  //if (debugOn) Serial.println("added an expander at pin 110 to 119");

  // Add an 8574A chip that allocates 10 more pins, therefore it goes from startLedNo..startLedNo+9
  multiIoAddExpander(multiIo, ioFrom8574(0x3C), 20);
  if (debugOn) Serial.println("added an expander at pin 120 to 129");

  // Add an 8574A chip that allocates 10 more pins, therefore it goes from startLedNo..startLedNo+9
  //multiIoAddExpander(multiIo, ioFrom8574(0x3D), 10);
  //if (debugOn) Serial.println("added an expander at pin 130 to 139");

  // Add an 8574A chip that allocates 10 more pins, therefore it goes from startLedNo..startLedNo+9
  multiIoAddExpander(multiIo, ioFrom8574(0x3E), 20);
  if (debugOn) Serial.println("added an expander at pin 140 to 159");

  // Add an 8574A chip that allocates 10 more pins, therefore it goes from startLedNo..startLedNo+9
  //multiIoAddExpander(multiIo, ioFrom8574(0x3F), 10);
  //if (debugOn) Serial.println("added an expander at pin 150 to 159");


  // Add an 8574 chip that allocates 10 more pins, therefore it goes from startLedNo..startLedNo+9
  multiIoAddExpander(multiIo, ioFrom8574(0x20), 10);
  if (debugOn) Serial.println("added an expander at pin 160 to 169");

  // Add an 8574 chip that allocates 10 more pins, therefore it goes from startLedNo+10..startLedNo+19
  multiIoAddExpander(multiIo, ioFrom8574(0x21), 10);
  if (debugOn) Serial.println("added an expander at pin 170 to 179");

  // Add an 8574 chip that allocates 10 more pins, therefore it goes from startLedNo+20..startLedNo+29
  multiIoAddExpander(multiIo, ioFrom8574(0x22), 10);
  if (debugOn) Serial.println("added an expander at pin 180 to 189");

  // Add an 8574 chip that allocates 10 more pins, therefore it goes from startLedNo+30..startLedNo+39
  multiIoAddExpander(multiIo, ioFrom8574(0x23), 10);
  if (debugOn) Serial.println("added an expander at pin 190 to 199");

  // Add an 8574 chip that allocates 10 more pins, therefore it goes from startLedNo+40..startLedNo+49
  multiIoAddExpander(multiIo, ioFrom8574(0x24), 10);
  if (debugOn) Serial.println("added an expander at pin 200 to 209");

  // Add an 8574 chip that allocates 10 more pins, therefore it goes from startLedNo+50..startLedNo+59
  multiIoAddExpander(multiIo, ioFrom8574(0x25), 10);
  if (debugOn) Serial.println("added an expander at pin 210 to 219");

  // Add an 8574 chip that allocates 10 more pins, therefore it goes from startLedNo+50..startLedNo+59
  multiIoAddExpander(multiIo, ioFrom8574(0x26), 10);
  if (debugOn) Serial.println("added an expander at pin 220 to 229");

  // Add an 8574 chip that allocates 10 more pins, therefore it goes from startLedNo+50..startLedNo+59
  //multiIoAddExpander(multiIo, ioFrom8574(0x27), 10);
  //if (debugOn) Serial.println("added an expander at pin 230 to 239");


  Serial.print("Number of leds defined:");
  Serial.println(noOfLeds);
 
  Serial.print("Number of buttons defined:");
  Serial.println(noOfButtons);
 
  // Define Arduino PINs as INPUT. Initialise pullup buttons
  switches.initialise(multiIo, true);
  for (size_t i=0; i<noOfButtons; i++)
  {
    switches.addSwitch(pgm_read_byte(&(button2leds[i][0])), onSwitchPressed); 
    ioDevicePinMode(multiIo, pgm_read_byte(&(button2leds[i][0])), INPUT_PULLUP);
    if (mqttConnected && pgm_read_byte(&(button2leds[i][0]))<startLedNo)                 //skip expander's fake input switches
        {
          mqttSubscribeToTopic(buttonSetTopic, pgm_read_byte(&(button2leds[i][0]))); 
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
