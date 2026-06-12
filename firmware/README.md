# E-Ink Voice Agent — ESP-IDF Firmware

## Build Requirements

- ESP-IDF v5.1+
- CMake 3.16+
- Python 3.8+

## Quick Start

```bash
# Set target
idf.py set-target esp32c6

# Configure
idf.py menuconfig

# Build
idf.py build

# Flash and monitor
idf.py -p /dev/ttyUSB0 flash monitor
```

## Project Structure

See [firmware plan](../docs/firmware-plan.md) for full architecture.

## Configuration

Edit `main/app_config.h` to set:
- WiFi SSID and password
- Hermes server URL
- Device ID and auth token
- Pin assignments
- Wake word sensitivity

## License

MIT
