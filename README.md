# Dual Processor Lamp
Uses two [Adafruit Metro Mini](https://www.adafruit.com/products/2590) boards to control output to analog LEDs. Originally written for RGBW (red/green/blue/warm white) [LED strips](https://www.adafruit.com/products/2439 "Adafruit RGBW LED Strip") mounted into a lamp shade.

Primary features:
 * On/off button with indicator light (https://www.adafruit.com/products/558)
 * Ambient light sensor to adjust LED and button indicator light brightness
 * On/off button with indicator light for the ambient light sensor
 * Distance sensor to turn LEDs on/off when a user is near by or walks away
 * Calibrated for warm/pink light output in the strip originally used

Requires SharpIR library found here: http://playground.arduino.cc/Main/SharpIR
