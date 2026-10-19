#pragma once

// Firmware-Version und Projektkennung. Beide stecken als Marker
// "SM-FW-ID:<ID>:<VERSION>:SM-FW-END" in jeder .bin (siehe main.cpp); der
// OTA-Upload prueft damit Herkunft und Version (siehe OtaManager).
#define DEVICE_FIRMWARE_VERSION "0.2.0"
#define FIRMWARE_PROJECT_ID "ESP-P4D"
