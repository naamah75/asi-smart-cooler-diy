# ASI585MC Peltier Cooler Controller

This project is an ESP32-based thermoelectric cooler controller designed for a ZWO ASI585MC planetary camera.

Status: this project is still under active development. Hardware, firmware, control strategy, and integrations are not yet final.

## Purpose

The goal is to turn a non-cooled ASI585MC camera into a temperature-controlled cooled camera using:

- a Peltier cell
- an ESP32 controller
- PWM power control through a MOSFET
- closed-loop PID temperature regulation
- dew-point protection to reduce condensation risk

## Main Hardware Platform

- Board: `Wemos / LOLIN D32 Pro`
- MCU: `ESP32`
- Connectivity: `WiFi + Bluetooth Low Energy`

## Main Components

### Cooling System

- `Peltier / TEC module`
  Used to cool the camera body or cold finger assembly.

- `Logic-level MOSFET`
  Used to modulate the Peltier power with PWM from the ESP32.

- `Hot-side heatsink and fan`
  Required to remove heat from the hot side of the Peltier.

- `Cold finger / thermal interface`
  Mechanical part that transfers cooling from the TEC to the camera body.

### Sensors

- `NTC 10K B3950` cold-side thermistor
  Measures the controlled cooling point temperature.

- `NTC 10K B3950` hot-side thermistor, optional
  Protects the TEC and heatsink by shutting down PWM if the hot side overheats.

- `BME280`
  Measures ambient temperature and relative humidity.

- `INA219`
  Measures Peltier current and bus voltage.

## Control Features

- PID loop for cold-side temperature regulation
- target temperature control
- dew point calculation from ambient temperature and humidity
- dew-point override so the effective target does not drop below a safe margin
- hot-side thermal protection when the optional hot thermistor is enabled
- current monitoring for power diagnostics

## Network and User Access

- Web dashboard for live status and configuration
- BLE JSON service
- BLE structured service with dedicated characteristics
- AP fallback for onboarding
- captive portal for first WiFi setup
- persistent network settings page
- mDNS support with `.local`

## Stored Settings

The controller stores settings in non-volatile memory, including:

- WiFi SSID and password
- hostname
- DHCP or static IP configuration
- PID parameters
- cooler target and limits
- sensor calibration offsets
- saved cooling presets

## Default Pin Mapping

- `GPIO25`: Peltier PWM output
- `GPIO34`: cold-side NTC input
- `GPIO35`: hot-side NTC input
- `GPIO21`: I2C SDA
- `GPIO22`: I2C SCL

## Suggested Electrical Notes

- Use a proper external power supply for the TEC
- Do not power the Peltier directly from the ESP32 board
- Use adequate wiring, grounding, and fuse protection
- Add a MOSFET gate pulldown and, if needed, a gate driver stage
- Ensure the hot side has enough thermal dissipation before extended testing

## Suggested Mechanical Notes

- Insulate the cold side to reduce condensation and thermal losses
- Use thermal paste or suitable thermal pads where needed
- Avoid stressing the camera body mechanically
- Test gradually with limited duty cycle before using aggressive cooling targets

## Development Note

This repository currently represents a development-stage prototype.

That means:

- wiring may still change
- firmware behavior may still evolve
- the user interface and onboarding flow may still be refined
- the Alpaca bridge is currently focused on cooler control only
- full long-term hardware validation is still pending

## Recommended Validation Before Real Use

1. Verify sensor readings with the Peltier disabled.
2. Verify current readings and PWM control at low duty cycle.
3. Verify hot-side temperature protection.
4. Verify dew-point protection in humid conditions.
5. Run longer tests before attaching the final assembly to the camera.
