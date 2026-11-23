---
title: OmniConnect Assembly and Connection Guide
subtitle: Any throttle. Any command station. Total freedom.
short: Assembly and Connection Guide
toc: yes
---

# 1. About this guide

This guide is for the person **building** an OmniConnect unit and connecting it to a layout. It covers the circuit board, the parts, the build order, the connectors and a step-by-step power-up check. Day-to-day use is covered in the **Quick Start Guide** and the **User Manual**.

It describes the OmniConnect motherboard **version 2.1** (83.5 x 75.5 mm), designed in KiCad. The design files are in the project's `hardware` folder: the KiCad project, the parts list (`Wemos Protocol Bridge_BOM.csv`), the PCB manufacturing files for PCBWay and the 3D-printable enclosure lid.

> **Warning** The unit works from a DC supply of up to 24 V and produces 12 V for the throttle sockets. Check every polarity and every voltage in chapter 6 before you plug in the modules or a throttle. Never connect a throttle to a unit that has not passed the power-up check.

## 1.1 How it is built

OmniConnect is a motherboard that carries four plug-in modules and a handful of simple parts:

| Module / part | Job |
|---|---|
| **Wemos D1 Mini** (ESP8266) | The processor: Wi-Fi, all protocols, display |
| **MAX485 module** | Talks to the XpressNet cable (RS485) |
| **Wemos DC power shield** | The power socket; makes the 5 V and 3.3 V supplies |
| **Mean Well N7812** regulator | A switching regulator that makes the 12 V for XpressNet and LocoNet throttles |
| **LM311** comparator | Reads the LocoNet signal safely |
| **BC547 transistors** (3) | Drive the LocoNet line and its power |
| **OLED 128x64, I2C** | The status display |
| Resistors, test points, pushbutton, RJ12 sockets | Everything else |

# 2. Before you start

## 2.1 Tools and skills

- soldering iron, solder, side cutters, a small screwdriver
- a digital multimeter (needed for the power-up check)
- a USB cable and a computer, for loading the firmware
- through-hole soldering skills; no surface-mount parts are used on the motherboard

## 2.2 Parts list

The list below follows the design's parts list. Part values are those in the design files.

| Reference | Qty | Part | Notes |
|---|---|---|---|
| U1 | 1 | Wemos D1 Mini (ESP8266) | Plugs into a socket |
| U3 | 1 | Wemos DC power shield | Plugs into a socket. Gives the power input socket |
| U6 | 1 | MAX485 module, 5 V logic, TTL to RS485 | Plugs into a socket |
| U4 | 1 | LM311, DIP-8 | In an 8-pin DIP socket |
| U2 | 1 | Mean Well N7812 switching regulator (pin-compatible with the LM7812) | Soldered to the board |
| U5 | 1 | OLED 128x64 I2C (SSD1306), 4-pin; the common yellow/blue type, or plain white or blue | Connects to J3 |
| Q1, Q2, Q3 | 3 | BC547 | TO-92 |
| R1, R2 | 2 | 2.2 k | RS485 bias, long-lead type |
| R4 | 1 | 47 k | |
| R5 | 1 | 22 k | |
| R6 | 1 | 4.7 k | |
| R7, R10 | 2 | 10 k | |
| R8 | 1 | 47 R | |
| R9 | 1 | 150 k | |
| R11 | 1 | 220 k | |
| R12 | 1 | 330 R | |
| J1 | 1 | RJ12 socket (6P6C), horizontal | **XpressNet** |
| J2 | 1 | RJ12 socket (6P6C), horizontal | **LocoNet** |
| J3 | 1 | 4-pin 2.54 mm header, horizontal | OLED connection |
| J4 | 1 | 2-pin 2.54 mm header, with jumper | "LNet Pwr Master" |
| SW1 | 1 | Tactile pushbutton | **Setup** button |
| TP1 to TP8 | 8 | Test points | 5 V, 12 V, 3.3 V, GND, DI/RO, DE/RE, RS485 A, RS485 B |
| | | DIP-8 socket, pin headers and sockets for U1, U3 and U6 | Make the modules removable |
| | | 15 V DC, 2 A power supply, 5.5 x 2.1 mm plug, **centre positive** | |
| | | Enclosure EIE AP-E1-SQUARE-BK, with 3D-printed lid | See chapter 7 |

> **Note** A diode D1 that appears in the original design (1N4004 on the supply) was **removed**: it dropped too much voltage. The board has no place for it and it is not fitted.

## 2.3 The power supply

- **15 V DC, 2 A**, 5.5 x 2.1 mm barrel plug, **centre positive**.
- Minimum 13.6 V: the 12 V regulator and the power shield need that much headroom.
- The components tolerate up to 24 V, but 15 V is the recommended supply. The board is marked **15VDC 2A MAX**.

# 3. The motherboard

The board is single-sided in function and has silkscreen labels on the component side. Look for these markings on your board:

| Marking | Meaning |
|---|---|
| **V2.1** | Board version |
| **SETUP** | The Setup pushbutton |
| **IIC OLED** with GND, 3.3V, SCL, SDA | Display header J3 |
| **XNET RX/TX**, **XNET S/R** | XpressNet data and direction pads from the D1 Mini |
| **LNET TX**, **LNET RX** | LocoNet signals from the D1 Mini |
| **LNet Pwr Master** | Jumper J4 |
| **15VDC 2A MAX** | Power input rating |
| **A**, **B**, **DI**, **DE**, **RE**, **R0**, **VCC**, **GND** | The MAX485 module's pins |

## 3.1 Which part goes where

| Fit permanently (solder) | Fit in a socket (removable) |
|---|---|
| Resistors R1 to R12 | D1 Mini (U1) |
| Transistors Q1, Q2, Q3 | MAX485 module (U6) |
| Mean Well N7812 (U2) | DC power shield (U3) |
| RJ12 sockets J1, J2 | LM311 (U4), in a DIP-8 socket |
| Header J3, header J4 | |
| Pushbutton SW1 | |
| Test points | |

Fitting the plug-in modules in sockets makes them easy to replace and keeps heat from the soldering iron away from them.

# 4. Building the board

Work from the smallest parts up to the tallest. Check each step off before moving on.

## 4.1 Step by step

1. **Resistors.** Fit R1 to R12 using the table in 2.2, as shown by the silkscreen on the board. Many are fitted vertically to save space, and a few lie horizontally. Bend the leads, push them through, solder and trim. Some resistors lie flat and some stand upright; follow the outlines printed on the board.
2. **Test points.** Solder the eight test points. They are used in chapter 6.
3. **DIP socket for U4.** Fit the 8-pin socket. The notch must match the notch printed on the board; pin 1 is towards the D1 Mini (bottom left of the socket when the board's RJ12 sockets face you). **Do not insert the LM311 yet.**
4. **Transistors Q1, Q2, Q3.** Fit the three BC547 with the flat face matching the silkscreen outline. Do not let them touch each other.
5. **Pin headers J3 and J4.** J3 is the 4-pin right-angle header for the display; J4 is the 2-pin header with a jumper.
6. **Pushbutton SW1.**
7. **Sockets for the three modules.** Fit the pin sockets for the D1 Mini (U1), the MAX485 module (U6) and the DC power shield (U3). Solder one pin at each end first, check the socket sits flat and straight, then solder the rest.
8. **Mean Well N7812 (U2).** This is a switching regulator that is pin-compatible with the common LM7812 but far more efficient, so it needs no heat sink and no cooling. Fit it in the outline printed on the board, matching the pin positions, and solder it.
9. **RJ12 sockets J1 and J2.** Fit and solder both. They sit on the edge of the board, and J1 (XpressNet) and J2 (LocoNet) need good mechanical strength, since cables are plugged into them repeatedly. Solder the shield and the mounting tabs too.
10. **Check.** Inspect every joint with a magnifying glass or phone camera for bridges. Check there is no solder between neighbouring pins, especially on the RJ12 sockets and the DIP socket.

> **Do not fit the plug-in modules yet.** The next chapters have you measure voltages with the modules still out. This protects them from a wiring fault.

## 4.2 The display

The OLED has four wires: GND, VCC, SCL, SDA. Connect it to J3, matching the board's silkscreen:

| J3 pin | Silkscreen | Goes to |
|---|---|---|
| 1 | GND | OLED GND |
| 2 | 3.3V | OLED VCC |
| 3 | SCL | OLED SCL |
| 4 | SDA | OLED SDA |

> **Warning** OLED modules from different suppliers have the same four pins in **different orders**. Check the labels printed on your module, and do not assume the order of the wires. A reversed supply destroys the display.

The display's I2C address is normally 0x3C. The firmware uses it automatically.

# 5. Connectors and jumpers

## 5.1 XpressNet (J1)

The socket is labelled **Xnet** on the enclosure lid.

> **Important** J1 and J2 are **throttle sockets only**. They do not support boosters: the sockets do not carry the DCC signal a booster needs, and the track signal always comes from the ECoS. Do not connect a booster, a command station or any track-power equipment to either socket.

The sockets are standard RJ12. With the clip facing down and the socket facing you, pin 1 is on the left.

| Pin | Signal | Notes |
|---|---|---|
| 1 | not connected | |
| 2 | GND | |
| 3 | B (RS485) | |
| 4 | A (RS485) | |
| 5 | +12 V | Power for the throttle |
| 6 | not connected | |

A MultiMaus plugs in directly with a standard XpressNet cable. OmniConnect is the **only** master on this cable.

## 5.2 LocoNet (J2)

The socket is labelled **LNet-T** on the enclosure lid (T for throttles).

| Pin | Signal | Notes |
|---|---|---|
| 1 | +12 V | Power for the throttle |
| 2 | GND | |
| 3 | LocoNet data | |
| 4 | LocoNet data | |
| 5 | GND | |
| 6 | not connected | |

A UT6 and other LocoNet throttles plug in with a standard LocoNet cable. The socket is on the board's right front.

## 5.3 LocoNet power jumper (J4, "LNet Pwr Master")

J4 is a two-pin header with a jumper. With the jumper **fitted** OmniConnect powers the LocoNet data line (it behaves as the LocoNet master, which a throttle such as the UT6 needs to work). OmniConnect is always used this way, so **leave the jumper fitted**.

If you ever need OmniConnect to join a LocoNet that already has its own master, remove the jumper. You could also solder the two pins together instead of using a header, since normal use never removes it.

## 5.4 Setup button (SW1)

The button, marked **SETUP**, is on the top left corner of the board when you face the unit with the **Xnet** socket at the front left and the **LNet-T** socket at the front right. Hold it for **3 seconds** while the unit is running to enter Setup Mode.

> **Note** The board is marked "Hold 5 Seconds" beside the button, to make it clear that the button must be **held**, not tapped. The firmware reacts after **3 seconds**, so holding it a little longer does no harm.

## 5.5 Wemos D1 Mini connections

For reference, the connections the firmware uses. The module is plugged into its socket and these are wired on the board:

| Function | D1 Mini pin | GPIO | Goes to |
|---|---|---|---|
| XpressNet data | D6 | 12 | MAX485 DI and RO, through R12 (330 R) |
| XpressNet direction | D0 | 16 | MAX485 DE and RE |
| Display SDA | D2 | 4 | J3 pin 4 |
| Display SCL | D1 | 5 | J3 pin 3 |
| Setup button | D7 | 13 | SW1 to GND |
| LocoNet transmit | D3 | 0 | Q3 base, through R6 (4.7 k) |
| LocoNet receive | RX | 3 | LM311 output |
| Power | 5V, 3V3, GND | | From the power shield |

> **Important** The LocoNet signals use the D1 Mini's USB-serial pins. **While the D1 Mini is in its socket, USB flashing and the serial monitor do not work.** Remove the D1 Mini to flash it by USB, or update it over Wi-Fi (chapter 8).

## 5.6 Test points

| Test point | What it measures | Expected |
|---|---|---|
| GND | Ground | 0 V (reference for every other reading) |
| 12V | Regulator output | 12 V |
| 5V | Supply from the power shield | 5 V |
| 3.3V | Supply from the power shield | 3.3 V |
| RS485 A, RS485 B | XpressNet data lines | A is pulled up to 5 V through R2 and B down to ground through R1; the levels move when a throttle talks. Measure with the module fitted and a throttle connected. |
| DI/RO | XpressNet data from the D1 Mini | Switches between 0 V and 3.3 V while the bus is active |
| DE/RE | XpressNet direction | Low (0 V) in normal listening |

# 6. Power-up check

Do this **before** the D1 Mini, the MAX485 module, the power shield or the LM311 are fitted to their sockets, then again once they are fitted. You need the multimeter.

1. Put the black probe on the **GND** test point.
2. **Check polarity:** the supply plug must be **centre positive**. If you are unsure, measure the plug with the multimeter.
3. Fit only the **DC power shield** (U3) in its socket. Connect the 15 V supply. Measure:

| Test point | Expected | If wrong |
|---|---|---|
| 12V | 11.5 V to 12.5 V | Switch off. Check the N7812's orientation and U3's socket. |
| 5V | 4.8 V to 5.2 V | Switch off. Check the shield's fitment. |
| 3.3V | 3.2 V to 3.4 V | Switch off. Check the shield's fitment. |

4. Switch off, wait for the voltages to fall to zero, then fit the **LM311** (notch matching the socket, pin 1 towards the D1 Mini), the **MAX485 module** and the **D1 Mini**. Make sure each is the right way round: match each module's pin labels to the silkscreen on the board, and the D1 Mini's USB connector to the outline printed for it.
5. Switch on and measure the three supplies again. They must not drop.
6. The OLED should show the OmniConnect logo for about 10 seconds. A blank display means the firmware is not loaded yet (chapter 8) or the display connection is wrong (chapter 4.2).
7. With nothing connected to J1 or J2, measure on the XpressNet socket (J1): pin 5 to pin 2 should be about **12 V**. Do the same on J2: pin 1 to pin 2 about **12 V**.

> **Warning** If any reading is wrong, stop. Do not connect a throttle until the cause is found.

# 7. The enclosure

OmniConnect is designed for the **EIE AP-E1-SQUARE-BK** enclosure. The original lid has no openings, so OmniConnect uses a **replacement lid, 3D-printed**, with cut-outs for the power socket, the display and the two RJ12 sockets. The original base and screws are used. The lid's file is `case lid v2.stl` in the `hardware/enclosure` folder.

A matching **base**, with the mounting posts for the board, is planned and will be added to this guide and to the project files.

Printing hints:

- print the lid with its outside face down
- 0.2 mm layers and a sturdy plastic such as PETG work well
- check the cut-out for the RJ12 sockets with a test fit before printing the whole lid

# 8. Loading the firmware

The first load must go over **USB**, because a new D1 Mini has no OmniConnect firmware on it yet.

1. **Remove the D1 Mini from its socket** (see the warning in 5.5).
2. Connect it to your computer with a USB cable.
3. Load the OmniConnect firmware (a file ending in **.bin**, or build it from the project sources with PlatformIO, `python -m platformio run -e wemos -t upload`).
4. Unplug the USB cable and **refit the D1 Mini** in its socket.
5. Connect the 15 V supply. The display shows the logo, then the setup instructions: on a new unit OmniConnect starts in Setup Mode on its own.

After this first load, all later updates can be made over Wi-Fi from the **/update** page described in the User Manual, without taking the D1 Mini out.

# 9. Connecting to the layout

Connect in this order:

1. The **ECoS** is connected by Ethernet cable to your router, the same router OmniConnect joins by Wi-Fi (the ECoS has no Wi-Fi of its own).
2. Plug the **XpressNet throttles** into J1, using standard XpressNet cables.
3. Plug a **LocoNet throttle** into J2.
4. Switch on **last**: connect the power supply. Z21 throttles use Wi-Fi.
5. Follow the Quick Start Guide to complete the first-time setup.

> **Important** Do not connect OmniConnect's J1 to an XpressNet cable that has another master (a command station, or a Z21 with an XpressNet socket) on it. OmniConnect is the master.

# 10. Build troubleshooting

| Symptom | Likely cause | What to do |
|---|---|---|
| No 12 V at the test point | N7812 wrong way round, or a bad joint, or supply under 13.6 V | Check orientation and joints; measure the supply plug. |
| 12 V correct but 5 V and 3.3 V missing | Power shield not seated, or its socket joints are bad | Reseat the shield; reflow its socket pins. |
| 5 V low once the modules are fitted | A short, or a wrongly fitted module | Remove the modules one by one to find the one that pulls it down. |
| Display blank | No firmware, or the wires to J3 are wrong | Check 4.2 and the firmware. |
| Display garbled or upside-down | A different display type | Confirm it is a 128x64 SSD1306 module. |
| MultiMaus shows error 13 | The bus is not working, or another master is present | Check J1 wiring and that no other master is on the cable. A replug after an OmniConnect restart is normal. |
| UT6 does nothing | LocoNet jumper J4 not fitted, or J2 wiring | Fit the J4 jumper; check 12 V at J2. |
| Setup button does nothing | Not held long enough, or the pushbutton is not soldered well | Hold for 3 seconds; check SW1. |
| Cannot flash by USB | D1 Mini is in the socket | Remove it (see 5.5). |

# Appendix A. Board connection summary

| Net | Connected to |
|---|---|
| +12 V | N7812 output, J1 pin 5, J2 pin 1, test point 12V |
| +5 V | Power shield, MAX485 VCC, LM311 pin 8, R2, R10 |
| +3.3 V | Power shield, D1 Mini, J3 pin 2 |
| GND | Everything |
| XpressNet A / B | MAX485 A / B, J1 pin 4 / pin 3, R2 bias to 5 V / R1 bias to ground |

# Appendix B. Reference

| Item | Value |
|---|---|
| Board | OmniConnect motherboard v2.1, 83.5 x 75.5 mm |
| Supply | 15 V DC, 2 A, 5.5 x 2.1 mm, centre positive (13.6 V minimum, 24 V maximum) |
| Processor | Wemos D1 Mini (ESP8266), 160 MHz |
| XpressNet | RS485, 62500 baud |
| LocoNet | 16660 baud |
| Setup | Button held 3 seconds |
| Enclosure | EIE AP-E1-SQUARE-BK, replacement 3D-printed lid |
