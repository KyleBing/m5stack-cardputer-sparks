# I2C Scan

Main menu keys:

- `n` — **InI2C** internal bus  
- `e` — **ExI2C** external bus  

Scans addresses 1–119, lists responding devices, and shows the SDA / SCL pins in use.

## Screenshots

**InI2C / ExI2C**

<div class="shot-row">

![i2c-in](/shots/app_hardware_ini2.png)
![i2c-ex](/shots/app_hardware_exi2.png)

</div>

## Shortcuts

| Key | Action |
|-----|--------|
| `h` | Help (scan range and pins) |

Entering the App runs a scan; leave and re-enter to scan again.

## Usage

1. **InI2C**: confirm onboard peripheral addresses (e.g. IMU).  
2. **ExI2C**: troubleshoot Grove / external I2C devices.  
3. No devices → empty list; poor contact can make scans flaky.
