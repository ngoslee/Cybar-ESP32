Cybwetruck Lightbar bridge/ simulator

Uses two LIN transceivers and an ESP32 to pretend to be a Lightbar to the truck and control a lightbar if one is connected.
Can also control gpio for load switching things like 3rd party light bars.

UI control:
Turn on lightbar
Set brightness to 42%
Within two seconds, set brightness to 69% WITHOUT PASSING THROUGH THE 50-59% RANGE
So get 42%, then tap near 69 and swipe to it

Modes:
Flood: scanner
Ditch: wigwag, cycle the ends back and forth, 100% to exit special modes
Both: brightness slider changes active segments, 0% is left side, 100% is right side

To exit, set Flood 100%

The is BLE UART server exposed for control as well as the debug serial port:
xx xx xx xx xx xx xx : xx: 0-100, set the six lightbar segments to the given values
kitt: scanner
off: turn off
load xx: xx = 0: off, 1: load 1, 2: load 2, 3: both loads

Pinouts:
Truck side LIN: Tx 22, Rx 23 
Bar side LIN: Tx 32, Rx 33
Loads: 16, 17

LIN Protocol:
19200 Baud, Enhanced checksum, each message is sent every 20 ms (so a message every 10 ms)
Truck to Bar: 0x0A 8 bytes, each value is a 10 bit value packed together
Bar to Truck: 0x0B 5 bytes, contains temperature and voltage

Hardare:
Sourced from Amazon
ESP32-DEVKITV1
TTL UART to LIN can bus converter NOTE! remove resistors R4 and R6
AITRIP 3PCS DC-DC step down power supply module 60V 3A, One set to 4V for ESP32 devkit, One 12V for LIN
2 Pcs High-Power 600W MOS Tube Trger Switch Drive Modules PWN Control DC 4V-60V

Plus protoboard and jumpers
