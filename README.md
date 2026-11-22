# Rootlet

Rootlet reads a capacitive soil moisture sensor with an ESP32-C3 and prints its
raw ADC value to the serial monitor. It also flashes an indicator on GPIO8 when
each reading is taken.

## Hardware and wiring

The project is configured for the RYMCU ESP32-C3 DevKitM-1 board.

| Sensor / indicator | ESP32-C3 |
| --- | --- |
| Sensor AOUT | GPIO4 |
| Sensor VCC | 3V3 |
| Sensor GND | GND |
| Reading indicator LED | GPIO8 |

Use a 3.3 V-compatible sensor output. The firmware reports raw 12-bit ADC
readings; it does not convert them to a moisture percentage.

## Build and upload

Install [PlatformIO](https://platformio.org/) and connect the board over USB.
From the project directory, run:

```sh
pio run
pio run -t upload
pio device monitor
```

The serial monitor is configured for **115200 baud**.
