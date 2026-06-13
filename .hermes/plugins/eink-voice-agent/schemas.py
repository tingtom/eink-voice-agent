BUILD_FIRMWARE = {
    "name": "build_firmware",
    "description": (
        "Build the ESP-IDF firmware for the E-Ink Voice Agent. "
        "Sources ESP-IDF from /opt/esp-idf (v5.4), runs idf.py build "
        "in the firmware/ directory."
    ),
    "parameters": {
        "type": "object",
        "properties": {},
    },
}

FLASH_FIRMWARE = {
    "name": "flash_firmware",
    "description": (
        "Flash the built firmware to a connected ESP32-C6 device "
        "via serial port. Default port is /dev/ttyUSB0."
    ),
    "parameters": {
        "type": "object",
        "properties": {
            "port": {
                "type": "string",
                "description": "Serial port (default: /dev/ttyUSB0)",
            },
        },
    },
}

MONITOR_DEVICE = {
    "name": "monitor_device",
    "description": (
        "Open the ESP-IDF serial monitor to view device logs. "
        "Exits with Ctrl+]."
    ),
    "parameters": {
        "type": "object",
        "properties": {
            "port": {
                "type": "string",
                "description": "Serial port (default: /dev/ttyUSB0)",
            },
        },
    },
}

CI_STATUS = {
    "name": "ci_status",
    "description": (
        "Check the GitHub Actions CI status for the latest commit "
        "on the main branch."
    ),
    "parameters": {
        "type": "object",
        "properties": {},
    },
}
