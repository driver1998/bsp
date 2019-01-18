Windows 10 ARM64 BSP for Raspberry Pi 3B
===

This is a direct port to ARM64 of the drivers for Windows 10 on Raspberry 3B.

The code is based on Microsoft's BSP for Windows 10 IoT Core, orignal README from Microsoft can be found in README2.md.

## Current Progress
Device|Driver|Progress
------|------|--------
Arasan SDHC|bcm2836sdhc.sys|Working
Broadcom SDHOST|rpisdhc.sys|Working
GPIO|bcmgpio.sys|Working
SPI0|bcmspi.sys|Working
SPI1|bcmauxspi.sys|Working
I2C|bcmi2c.sys|Working
Audio (3.5mm jack only)|rpiwav.sys|Working
PWM|bcm2836pwm.sys|Working
Mini UART|pi_miniuart.sys|Working
PL011 UART|SerPL011.sys|Loads, but not tested
Mailbox Interface|rpiq.sys|Working
VCHIQ|vchiq.sys|Loads, but not tested

### Notes
- "Working" means basic functionality works
- "Loads, but not tested" means the driver loads, but its functionality is not tested.
- In order to get bcmauxspi.sys to load, you'll need to set the following registry key:
```
HKLM\SYSTEM\CurrentControlSet\Services\bcmauxspi\Parameters
DWORD ForceEnable=1
```
- Closed source drivers like USB/LAN/WiFi/Bluetooth are not included in this repo. \
There is an open-source USB driver by NTAuthority and maintained by Googulator at [dwusb](https://github.com/Googulator/dwusb).

# User Mode GPIO/SPI/I2C
GPIO/SPI/I2C can be accessed in usermode by Windows.Device.* WinRT APIs. \
Use Oct 1st 2018 build of RaspberryPiPkg or newer for this functionality. 

Samples are provided in [rpi3win10demos](https://github.com/driver1998/rpi3win10demos).

Unlike IoT Core, you'll need to call these APIs in a full trust desktop application, not sandboxed UWP.

# RPIQ Mailbox
RPIQ is at `\\.\RPIQ`, you can do handy things like query CPU frequency and temperature with it.

Samples are provided in [rpi3win10demos](https://github.com/driver1998/rpi3win10demos).

# Networking over UART
Networking is possible through modem emulation over UART, though slow (literal dial-up speeds, bring back the 90s :) ) and not stable.

For details, check out [Wiki](https://github.com/driver1998/bsp/wiki/Modem-Emulation-and-Networking).
