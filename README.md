# PV Excess
This software will enable you to turn on your appliances automatically if your pv system is supplying enough excess power.
The device will receive the current power from your meter by MQTT. Positive values mean that you are receiving data from the grid. 
Negative values mean that you are feeding power into the grid.
If the excess power will rise above an adjustable level and will stay above that level for an adjustable ammount of time, 
an output of the ESP will turn on.  
Two outputs are avilable. The first will stay on permanently after the trigger threshold will be exceeded. E.g. a relay can be connected 
to this output to power your appliances. The second output will just stay on for 500 ms. E.g. this can "push" the start button of a device by bridging it´s start button with a relay.

![Welcome screen](https://github.com/otti/PvExcess/blob/main/pics/display_scrren1.jpg)
![Waiting for excess power screen](https://github.com/otti/PvExcess/blob/main/pics/display_scrren2.jpg)

# How to install
* Checkout this repo
* Rename and adapt [Config.h.example](https://github.com/otti/PvExcess/blob/master/SRC/PvExcess/Config.h.example) to Config.h with your compile time settings
* Flash to TTGO
* Connect to the setup wifi called PvExcess (PW: PvExcess) and configure the firmware via the webinterface at http://192.168.4.1
   * Select your WiFi and enter your password
   * Set MQTT server, port, username, password, topic and the JSON entry within the MQTT data
* If you need to reconfigure the stick later on you have to press the ap button for a few seconds (next beside the reset button)

## Features
Implemented Features:
* Built-in simple Webserver for debugging and firmware update
* It supports convenient OTA firmware update (`http://<ip>/firmware`)
* Wifi manager with own access point for initial configuration of Wifi and MQTT server (IP: 192.168.4.1, SSID: PvExcess, Pass: PvExcess)

## Supported devices
* LILYO TTGO T-Display ESP32 1.14 Inch (240x135 pixel)


## JSON Format Data

The value of ´ElectricalPower´ has to be a signed integer

example:
`{  
  "ElectricalPower": -500  
}`  

Some keywords:
ESP32, Arduino, MQTT, JSON,  photovoltaics, solar, excess power, excessive power, surplus power, overflow





