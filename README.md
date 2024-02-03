# RV-Shade: HomeKit RV Shade controller

1. [Features](#features)
2. [Background](#background)
3. [Hardware](#hardware)
4. [Schematic](#schematic)
5. [Board Layout](#board-layout)
6. [Wiring](#wiring)
7. [Firmware Setup](#firmware)
8. [3D Printing](#3dprint)

## <a name="features"></a>Features

* Arduino based circuit which intercepts the signals from the dash switch on an RV front shade to give HomeKit control.
* Built with an Adafruit QT Py ESP32-S3.
* Fits inside the front dash.
* STL files for a 3D printed case are included.

## <a name="background"></a>Background

![Penguin Express](/images/Penguin_Express.jpg)

After purchasing a class-A motorhome in 2020 (***The Penguin Express***) I worked on several 3D and arduino projects for it. The first was a HomeKit to RV-C interface [RV-Bridge](https://github.com/rubillos/RV-Bridge). Since then I've done a couple of LED lighting controllers.

This project adds another controller to handle opening and closing the front shade via HomeKit.

## <a name="hardware"></a>Hardware

![Finished Circuit](/images/Circuit.jpeg)

The circuit is just an [Adafruit QT Py ESP32-S3 No PSRAM](https://www.adafruit.com/product/5426) with two 3V DPDT relays. One relay isolates the shade motor from the switch, the second drives the motor in either direction. There are some NPN MosFETs with flyback diodes for driving the relays and a pair of voltage dividers connected to analog inputs to read user actions on the switch. Some LEDs are wired to the switch and motor connectors as visual indicators.

Note: my QT Py had a non-functional NeoPixel so I hardwired an external one in it's place. You shouldn't need that, adjust the ifdef in the code accordingly.

## <a name="schematic"></a>Schematic

![Schematic](/images/Schematic.png)

## <a name="layout"></a>Board Layout

I use Fritzing for arranging board layout. I find that it's really good for figuring out where to place components for breadboard wiring.

![Layout](/images/Layout.png)

## <a name="wiring"></a>Wiring

The front shade on our RV is made by Irvine Shade. It has two control wires that connect to a switch on the dash. The switch is normally in the middle, you press and hold the top half to put the shade up, the bottom half moves the shade down.

After locating the two wires leading from the dash switch to the motor I cut them and used some [Inline Wire Splice Connectors](https://www.amazon.com/gp/product/B0BS6HZ941) to extend them up to the top of the dash, keeping track of which pair went to the switch and which went to the motor. I made sure to color code the extensions to the two wires.

I screwed the 3D printed enclosure to an empty spot under the dash cover, then spliced into 12V power that's active when the "house" power is on. (Make sure you don't tap into "chassis" power; I did that initially and spent some time figuring out why I wasn't seeing power).

|  |  |
| :---: | :---: |
| <br>![Dash](/images/Dash.jpeg) |![Installed](/images/Installed.jpeg) |

## <a name="firmware"></a>Firmware Setup

- The project is set up for compilation with PlatformIO.
    * I use it via Microsoft's Visual Studio Code.
- Flashing
    * The QT Py ESP32-S3 does not auto-reset after flashing, you need to do it manually.
- Startup
    * Connect to the ESP32 via the Serial Monitor.
    * You should see a bunch of startup logging.
    * Then a message about being connected to Wifi and not being paired.
    * HomeSpan provides a command line interface that you can access through the Serial Monitor. Type '?' to see the available comands.
- Pairing
    * Be on the same wifi network and in close proximity to the ESP32.
    * In the Home app choose "Add Accessory".
    * Point the camera at this image:
    <br><br>
    ![Pairing Code](/images/defaultSetupCode.png)
    <br><br>
    * Tap on `RV-Shade`.
    * Accept that this is an "unsupported" device.
    * Add the bridge and the shade.
    * Done!

## <a name="3dprint"></a>3D Printing

- A case will keep the microcontroller isolated from any exposed contacts inside the dash.
- STL Files are in the `3D` folder:
    * [RV-Shade_Box_Bottom.stl](/3D/RV-Shade_Box_Bottom.stl)
    * [RV-Shade_Box_Top.stl](/3D/RV-Shade_Box_Top.stl)
    * [Text](/3D/Text/) - There is a folder with STLs for each of the words. I have a Prusa XL and used these to have the text in different colors. You ca skip these and just have recessed words.
- Build Plate
    * A textured build plate give a nice surface finish for the top and bottom of the box.
- Filament
    * PETG - handles heat better than PLA and sticks to a textured build plate much better than PLA.

---
Copyright © 2024 [Randy Ubillos](http://rickandrandy.com)
