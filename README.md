Windows 10 ARM64 BSP for Raspberry Pi 3B
===

This is a direct port to ARM64 of the drivers for Windows 10 on Raspberry 3B.

The code is based on Microsoft's BSP for Windows 10 IoT Core, orignal README from Microsoft can be found in README2.md.

This port is not fully tested, but Audio and SDHC drivers are proved to be working, at least to some extent.

## Current Progress
Device|Driver|Progress
------|------|--------
Arasan SDHC|bcm2836sdhc.sys|Working
SDHOST|rpisdhc.sys|Working
GPIO|bcmgpio.sys|Working
SPI|bcmspi.sys|Loads, but not tested
AUXSPI|bcmauxspi.sys|Won't load
I2C|bcmi2c.sys|Loads, but not tested
Audio|rpiwav.sys|Working
PWM|bcm2836pwm.sys|Working
Mini UART|pi_miniuart.sys|Working
PL011 UART|SerPL011.sys|Loads, but not tested
RPIQ|rpiq.sys|Loads, but not tested
VCHIQ|vchiq.sys|Loads, but not tested
