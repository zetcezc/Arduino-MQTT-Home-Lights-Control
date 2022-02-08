# Arduino ( & MQTT ) Home Lights Control

Project in progress for my house lights automation

WARNING: The software is deployed in my house, but I cannot guarantee it's going work at yours. Use it on your own risk.

My idea was to design the system which would 
- work with wired system (reliable) - I already had cables set up in my house in the star topology
- boot up and work fast - my choice was Arduino as the controller
- be independent from network infrastructure but could benefit it
- easy to build using ready hardware modules (this I could not achieve completely - but maybe you can if you need less input/output connections

The prerequisite is that every light and every button in your house is wired with one central place. 
You also must use wall buttons - not wall switches - they have a spring inside and are closed only as long as you keep them pressed.

Arduino is the brain which listens to buttons connected to input pins and turns on/off output pins connected to relays, which control lights. In the current version it also stores in flash memory the table with the definition which buttons control which set of lamps. And that's it if we talk about basic functionality. 
To have more input/output pins - I use PCF8574 and PCF8574A expanders.

In my project I ise Arduino Mega pins defined as input pins (54) + DIY boars containing 4 x PC8574A expanders, defined as input pins (32) which makes 86 available "buttons" + 3 reserved (clear EEPROM, reset, switch all off).<br>
Additionally, I use 8 x PCF8574 expanders to achieve 64 OUTPUT PINS (I call them "leds"). They are available in the form of ready to use module and be connected to each other like train cars ;) <br>
If 54 pins of Arduino mega + 64 pins of expanders are enough for you - you can skip the DYI extension board.
In theory you could combine 8 x PCF8574 + 8 x PCF8574A expanders (limit of the addressing), but the maximum number of PCF8574(A) expanders that can be used in this project is 12 (limit of io-abstraction library used). PINS can be reconfigured according to the need. <br>
Output PINS are connected to SSR relays and standard relays to allow switching 230V lights.

And this works fine standalone, but here comes the more interesting part if you want to integrate it with home automation system - MQTT. <br>
If Arduino succeeds in connecting to MQTT broker - it uses MQTT communication to send button press events to topics and turns on lights by incoming MQTT messages. <br>
You can also control particular light via MQTT and see their state. <br>
MQTT can be used to enable auto discovery of lights and buttons. I used Home Assistant syntax.


I used io-abstraction library to get all pins together. <br>
https://www.thecoderscorner.com/products/arduino-libraries/io-abstraction/ <br>
Check MultiIoAbstraction for more details .<br>
https://www.thecoderscorner.com/products/arduino-libraries/io-abstraction/arduino-pins-and-io-expanders-same-time/<br>
Great library - many thanks to TheCodersCorner / Dave Cherry!
