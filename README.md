# Rootlet

Rootlet reads a capacitive soil moisture sensor with an ESP32-C3 and serves a
live moisture page over Wi-Fi. It also prints the raw ADC value to the serial
monitor and flashes an indicator on GPIO8 when each reading is taken.

## Hardware and wiring

The project is configured for the RYMCU ESP32-C3 DevKitM-1 board.

| Sensor / indicator | ESP32-C3 |
| --- | --- |
| Sensor AOUT | GPIO4 |
| Sensor VCC | 3V3 |
| Sensor GND | GND |
| Reading indicator LED | GPIO8 |

Use a 3.3 V-compatible sensor output. The firmware reports raw 12-bit ADC
readings and a linear moisture estimate: 1500 is 100% wet and 3000 is 0% wet.
Readings below 1500 or above 3000 are clamped to 100% or 0%, respectively.
These calibration points are specific to the sensor setup.

## Build and upload

Install [PlatformIO](https://platformio.org/) and connect the board over USB.
From the project directory, run:

```sh
pio run
pio run -t upload
pio device monitor
```

The serial monitor is configured for **115200 baud**.

## Wi-Fi setup

1. With no saved Wi-Fi, connect your phone or computer to the open **rootlet**
   network. It has no password. Open **http://192.168.4.1/** in a browser.
2. Scan for networks, select your home Wi-Fi, enter its password (leave it
   blank for an open network), and select **Connect**. If the connection fails,
   check the password and try again. Your device may need to reconnect to
   **rootlet** if the access point changes channels during connection.
3. Once connected, note the displayed home-network IP address and select
   **Finish setup** to turn off the open setup network. Switch your phone or
   computer to your home Wi-Fi and open the displayed IP address. You can also
   try **http://rootlet.local/** if your device and network support mDNS.
4. The home-network page shows the current raw reading and moisture estimate.
   The serial monitor also prints the home-network IP address.

Rootlet remembers the last successfully connected network across power loss
and restarts. If it cannot connect to that network, or loses the connection
and cannot recover within about 20 seconds, it opens **rootlet** for setup
again. The previous credentials remain saved until another connection succeeds
or you select **Reset Wi-Fi** on the moisture page. Reset removes the saved
credentials and opens the setup network so you can choose a different Wi-Fi.

The ESP32-C3 scans visible 2.4 GHz networks; 5 GHz and hidden networks are not
listed. Open, WPA2 personal, and supported WPA3 personal networks can be
selected; enterprise and other unsupported security types cannot. The setup
page does not use a captive portal, so enter the address manually.

The setup network is open, and the web pages do not use authentication or
HTTPS. Anyone with access to the setup or home network can interact with
Rootlet; Wi-Fi credentials sent during setup are unencrypted and saved in
device flash. Use setup in a trusted location and finish it promptly.

## Hardware checks

The web-interface checks can be run with Node.js using
`node --test test/web.test.js`.

After uploading, verify setup, a failed password, and a successful connection.
Check that the moisture page updates, the indicator flashes, and the setup
network disappears after finishing. Power-cycle the board to check reconnection;
then make the router unavailable to check the 20-second setup fallback. Restore
the router and use **Reset Wi-Fi** to check that provisioning works again.
