Windows 10 ARM64 BSP for Raspberry Pi 3B
===

This is a direct port to ARM64 of the drivers for Windows 10 on Raspberry 3B.

The code is based on Microsoft's BSP for Windows 10 IoT Core, orignal README from Microsoft can be found in README2.md.

## Current Progress
Device|Driver|Progress
------|------|--------
Arasan SDHC|bcm2836sdhc.sys|Working
SDHOST|rpisdhc.sys|Working
GPIO|bcmgpio.sys|Working
SPI|bcmspi.sys|Working
AUXSPI|bcmauxspi.sys|Working
I2C|bcmi2c.sys|Working
Audio|rpiwav.sys|Working
PWM|bcm2836pwm.sys|Working
Mini UART|pi_miniuart.sys|Working
PL011 UART|SerPL011.sys|Loads, but not tested
RPIQ|rpiq.sys|Working
VCHIQ|vchiq.sys|Loads, but not tested

### Notes
- "Working" means basic functionality works, but with no guarantee in stability or performance.
- "Loads, but not tested" means the driver loads, and reports as "working properly" in Device Manager, but we haven't test if it actually works yet.
- In order to get bcmauxspi.sys to load, you'll need to set the following registry key:
```
HKLM\SYSTEM\CurrentControlSet\Services\bcmauxspi\Parameters
DWORD ForceEnable=1
```
# Enabling User Mode GPIO/SPI/I2C
With Windows.Device.* APIs, you can access GPIO/SPI/I2C pins in usermode, samples are provided in https://github.com/driver1998/rpi3win10demos.

You'll need the Oct 1st 2018 build of RaspberryPiPkg or newer for this functionality.

Unlike IoT Core, you'll need to call these APIs in a full trust desktop application, not sandboxed UWP.

# Accessing RPIQ Mailboxes in User Mode
To access RPIQ Mailboxes (like querying voltages and temperatures of your Raspberry Pi), you'll need to access device `\\.\RPIQ`, and invoke ioctl operations with `DeviceIoControl`. Samples are provided in https://github.com/driver1998/rpi3win10demos.

# Networking through modem emulation over UART
Networking is possible through modem emulation over UART, though slow (literal dial-up speeds, bring back the 90s :) ) and not stable.
For details, check out https://github.com/driver1998/bsp/issues/1.
