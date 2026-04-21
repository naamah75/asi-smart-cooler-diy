 Home Assistant (HA) Dashboard for LilyGo T5 4.7 Inch E-Paper Screen
=======================================

This project works with LilyGO T5 4.7 inch e-paper EPD display as available from [AliExpress](https://aliexpress.com/item/1005002272417292.html) and [Home Assistant (HA)](https://www.home-assistant.io/) as ESP32 based dashboard for your smart home.

You can simply add and display your HA entities (switches, binary sensors, temperature...) to get a neat overview about what's up.

![](assets/epaper_home_assistant_dashboard.jpg)

LilyGo T5 4.7 Inch E-Paper Screen is very cheap all in one board that you can use to display various content. 
This would make a cool dashboard when integrated with HA to monitor the status of the devices intergated to HA. 

## Compiling and flashing
Edit configurations.h and enter WiFi settings and Home Assistant auth token. The configurations are pretty straight forward, just follow inline comments. 

To compile you will need following library  
- https://github.com/Xinyuan-LilyGO/LilyGo-EPD47  

In board manager choose ESP32 Dev Module with PSRAM Enabled.   

The project is configured as PlatformIO Project (Visual Studio Code AddIn) - to compile with arduino IDE rename ``main.cpp`` to ``main.ino`` and rename the src folder to ``main``.

## Icons and new Entities

Icons are taken from https://www.flaticon.com/ and resized to match the tile sizes. You can download and add own icons, see [Scripts](scripts/README.md) on how to convert and add them.

Adding new entities is quite straight forward, just have a look at the code.


## Remarks 
The project is forked from [hacksics/lilygo-t5-47-ha](https://github.com/hacksics/lilygo-t5-47-ha) - who did a great basic work.

Primary modifications are:  
- **Deep Sleep** to avoid battery drainage
- Using original LilyGo-EPD47 lib to work better with T5 board
- Support for **soil moisture sensors** (plant watering sensors)
- Some refactoring, bugfixing and code cleanup
- Some minior UI changes, where I liked it different