# Arduino (&MQTT) Home Lights Control

Project in progress for my house lights automation
WARNING: this is a working prototype stage. Use it on your own risk.
I will update after deploying in my house. Also with used hardware description.

This is the software that can be used to control home lights using Arduino Mega + Ethernet shield + up to 16 x PCF8574 pin expanders.
In addition it uses MQTT communication to send button press events to topics and turn on lights by incoming MQTT messeges.
MQTT can be used to enable autodiscovery of lights and buttons. I used HomeAssistant syntax.
If network or MQTT broker is not available - Arduino works alone without this functionality, because the buttons to lights translation table is stored in the flash memory.

In my project I ise Arduino Mega pins + DIY boars containig 8 x PC8574A epanders, defined as input pins, which gives me 118 available INPUT PINS (54 in Arduino and 64 in expanders, called "buttons" in the sketch) 
Additionally I use 8 x PCF8574 epanders to achieve 64 OUTPUT PINS (called "leds" in the sketch). This is the maximum number of PCF8574(A) expanders that can be used.
PINS can be reconfigured according to the need. 
Outpus PINS are connected to SSR relays to allow switching 230V lights.

I'm using io-abstraction library to get all pins together.
https://www.thecoderscorner.com/products/arduino-libraries/io-abstraction/
Check MultiIoAbstraction for more details.
https://www.thecoderscorner.com/products/arduino-libraries/io-abstraction/arduino-pins-and-io-expanders-same-time/
Great library - many thanks to TheCodersCorner / Dave Cherry!
