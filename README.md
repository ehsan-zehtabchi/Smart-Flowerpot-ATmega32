# Smart Flowerpot Controller – ATmega32
# ATmega32 smart flowerpot controller with LM35 temperature monitoring, LDR-based light control, soil moisture sensing, PWM water pump control, and 16x2 LCD interface.
# This project is a smart flowerpot / mini greenhouse controller built around the AVR **ATmega32** microcontroller.  
# It monitors **temperature**, **ambient light**, and **soil moisture**, displays real-time values on a **16x2 LCD**, controls **grow lights**, regulates temperature using **heater/fan outputs**, and drives a **DC water pump** using **PWM**.

## Features

- ATmega32-based embedded control system
- **LM35** temperature sensing
- **LDR voltage divider** for light measurement
- **Soil moisture sensor** analog monitoring
- **Potentiometer-based temperature setpoint**
- **Automatic / manual light control**
- **Temperature control with hysteresis**
- **PWM pump control** using Timer0 / OC0
- **16x2 LCD** user interface
- Clean modular firmware written in **C** for AVR

## Hardware Overview

The system uses the following hardware blocks:

- **ATmega32**
- **16x2 LCD** in 4-bit mode
- **LM35** temperature sensor
- **10k potentiometer** for temperature setpoint
- **Soil moisture sensor module**
- **LDR + resistor divider** for ambient light sensing
- **Grow light outputs**
- **Cooling fan output**
- **Heater output**
- **DC water pump driver stage**
- Mode and control switches with internal pull-up configuration

## Pin Mapping

- **PA0 / ADC0** → LM35 temperature sensor
- **PA2 / ADC2** → Setpoint potentiometer
- **PA5 / ADC5** → Soil moisture sensor
- **PA6 / ADC6** → Light sensor (LDR divider)

- **PA1** → Auto / Manual light mode select
- **PA3** → Manual light switch
- **PA4** → Pump enable switch

- **PB6** → Grow Light 1
- **PB7** → Grow Light 2
- **PB4** → Cooling Fan
- **PB5** → Heater
- **PB3 / OC0** → PWM output for pump driver
- **PB0, PB1, PB2** → Pump control lines

- **PORTC** → 16x2 LCD (CodeVisionAVR default 4-bit LCD interface)

## Control Logic

### Light Control
- In **auto mode**, the controller reads the LDR voltage and switches the grow lights in stages:
  - low light → both lights ON
  - medium light → one light ON
  - high light → both lights OFF
- In **manual mode**, the lights are controlled by a switch.

### Temperature Control
- The LM35 measures the current temperature.
- A potentiometer sets the target temperature.
- The controller uses a hysteresis band to avoid rapid switching:
  - above setpoint → cooling fan ON
  - below setpoint → heater ON
  - near setpoint → both OFF

### Pump Control
- Soil moisture is converted to a moisture percentage after calibration.
- If the pump system is enabled:
  - very dry soil → higher PWM duty cycle
  - moderately dry soil → lower PWM duty cycle
  - wet soil → pump OFF

## Firmware Notes

This code is written for **CodeVisionAVR** using:

- `mega32.h`
- `lcd.h`
- `delay.h`

The LCD uses the default CodeVisionAVR 4-bit LCD connection on **PORTC**.

## Calibration

Two values should be adjusted on real hardware for accurate soil readings:

```c
#define SOIL_DRY_ADC 850
#define SOIL_WET_ADC 350
