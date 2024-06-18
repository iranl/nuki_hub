#pragma once

#include "sdkconfig.h"

#define NUKI_HUB_VERSION "8.34"
#define NUKI_HUB_BUILD "unknownbuildnr"

#define GITHUB_LATEST_RELEASE_URL (char*)"https://github.com/technyon/nuki_hub/releases/latest"
#define GITHUB_LATEST_RELEASE_API_URL (char*)"https://api.github.com/repos/technyon/nuki_hub/releases/latest"

#if defined(CONFIG_IDF_TARGET_ESP32C3)
#define GITHUB_LATEST_RELEASE_BINARY_URL (char*)"https://github.com/technyon/nuki_hub/raw/master/ota/nuki_hub_esp32c3.bin"
#define GITHUB_LATEST_UPDATER_BINARY_URL (char*)"https://github.com/technyon/nuki_hub/raw/master/ota/nuki_hub_updater_esp32c3.bin"
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#define GITHUB_LATEST_RELEASE_BINARY_URL (char*)"https://github.com/technyon/nuki_hub/raw/master/ota/nuki_hub_esp32s3.bin"
#define GITHUB_LATEST_UPDATER_BINARY_URL (char*)"https://github.com/technyon/nuki_hub/raw/master/ota/nuki_hub_updater_esp32s3.bin"
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
#define GITHUB_LATEST_RELEASE_BINARY_URL (char*)"https://github.com/technyon/nuki_hub/raw/master/ota/nuki_hub_esp32c6.bin"
#define GITHUB_LATEST_UPDATER_BINARY_URL (char*)"https://github.com/technyon/nuki_hub/raw/master/ota/nuki_hub_updater_esp32c6.bin"
#else
#if defined(FRAMEWORK_ARDUINO_SOLO1)
#define GITHUB_LATEST_RELEASE_BINARY_URL (char*)"https://github.com/technyon/nuki_hub/raw/master/ota/nuki_hub_esp32solo1.bin"
#define GITHUB_LATEST_UPDATER_BINARY_URL (char*)"https://github.com/technyon/nuki_hub/raw/master/ota/nuki_hub_updater_esp32solo1.bin"
#else
#define GITHUB_LATEST_RELEASE_BINARY_URL "https://github.com/technyon/nuki_hub/raw/master/ota/nuki_hub_esp32.bin"
#define GITHUB_LATEST_UPDATER_BINARY_URL (char*)"https://github.com/technyon/nuki_hub/raw/master/ota/nuki_hub_updater_esp32.bin"
#endif
#endif

#ifndef NUKI_HUB_UPDATER
#define MQTT_QOS_LEVEL 1
#define MQTT_CLEAN_SESSIONS false
#define GPIO_DEBOUNCE_TIME 200
#define CHAR_BUFFER_SIZE 4096
#define NUKI_TASK_SIZE 8192
#define PD_TASK_SIZE 1024
#define MAX_AUTHLOG 5
#define MAX_KEYPAD 10
#define MAX_TIMECONTROL 10
#endif

#define NETWORK_TASK_SIZE 12288