#include "Arduino.h"
#include "NukiWrapper.h"
#include "NukiNetworkLock.h"
#include "WebCfgServer.h"
#include <RTOS.h>
#include "PreferencesKeys.h"
#include "PresenceDetection.h"
#include "hardware/W5500EthServer.h"
#include "hardware/WifiEthServer.h"
#include "NukiOpenerWrapper.h"
#include "Gpio.h"
#include "Logger.h"
#include "Config.h"
#include "RestartReason.h"
#include "CharBuffer.h"
#include "NukiDeviceId.h"

NukiNetwork* network = nullptr;
NukiNetworkLock* networkLock = nullptr;
NukiNetworkOpener* networkOpener = nullptr;
WebCfgServer* webCfgServer = nullptr;
BleScanner::Scanner* bleScanner = nullptr;
NukiWrapper* nuki = nullptr;
NukiOpenerWrapper* nukiOpener = nullptr;
PresenceDetection* presenceDetection = nullptr;
NukiDeviceId* deviceIdLock = nullptr;
NukiDeviceId* deviceIdOpener = nullptr;
Preferences* preferences = nullptr;
EthServer* ethServer = nullptr;
Gpio* gpio = nullptr;

bool lockEnabled = false;
bool openerEnabled = false;
unsigned long restartTs = (2^32) - 5 * 60000;

RTC_NOINIT_ATTR int restartReason;
RTC_NOINIT_ATTR uint64_t restartReasonValidDetect;
RTC_NOINIT_ATTR bool rebuildGpioRequested;
bool restartReason_isValid;
RestartReason currentRestartReason = RestartReason::NotApplicable;

TaskHandle_t networkTaskHandle = nullptr;
TaskHandle_t nukiTaskHandle = nullptr;
TaskHandle_t presenceDetectionTaskHandle = nullptr;

void networkTask(void *pvParameters)
{
    while(true)
    {
        bool connected = network->update();
        if(connected && openerEnabled)
        {
            networkOpener->update();
        }

        if(preferences->getBool(preference_webserver_enabled, true))
        {
            webCfgServer->update();
        }

        // millis() is about to overflow. Restart device to prevent problems with overflow
        if(millis() > restartTs)
        {
            Log->println(F("Restart timer expired, restarting device."));
            delay(200);
            restartEsp(RestartReason::RestartTimer);
        }

        delay(100);

//        if(wmts < millis())
//        {
//            Serial.print("# ");
//            Serial.println(uxTaskGetStackHighWaterMark(NULL));
//            wmts = millis() + 60000;
//        }
    }
}

void nukiTask(void *pvParameters)
{
    while(true)
    {
        bleScanner->update();
        delay(20);

        bool needsPairing = (lockEnabled && !nuki->isPaired()) || (openerEnabled && !nukiOpener->isPaired());

        if (needsPairing)
        {
            delay(5000);
        }

        if(lockEnabled)
        {
            nuki->update();
        }
        if(openerEnabled)
        {
            nukiOpener->update();
        }

    }
}

void presenceDetectionTask(void *pvParameters)
{
    while(true)
    {
        presenceDetection->update();
    }
}


void setupTasks()
{
    // configMAX_PRIORITIES is 25

    xTaskCreatePinnedToCore(networkTask, "ntw", preferences->getInt(preference_task_size_network, NETWORK_TASK_SIZE), NULL, 3, &networkTaskHandle, 1);
    xTaskCreatePinnedToCore(nukiTask, "nuki", preferences->getInt(preference_task_size_nuki, NUKI_TASK_SIZE), NULL, 2, &nukiTaskHandle, 1);

    if(preferences->getInt(preference_presence_detection_timeout) >= 0)
    {
        xTaskCreatePinnedToCore(presenceDetectionTask, "prdet", preferences->getInt(preference_task_size_pd, PD_TASK_SIZE), NULL, 5, &presenceDetectionTaskHandle, 1);
    }
}

void initEthServer(const NetworkDeviceType device)
{
    switch (device)
    {
        case NetworkDeviceType::W5500:
            ethServer = new W5500EthServer(80);
            break;
        case NetworkDeviceType::WiFi:
            ethServer = new WifiEthServer(80);
            break;
        default:
            ethServer = new WifiEthServer(80);
            break;
    }
}

bool initPreferences()
{
    preferences = new Preferences();
    preferences->begin("nukihub", false);

//    preferences->putBool(preference_network_wifi_fallback_disabled, false);

    bool firstStart = !preferences->getBool(preference_started_before);

    if(firstStart)
    {
        preferences->putBool(preference_started_before, true);
        preferences->putBool(preference_lock_enabled, true);
        uint32_t aclPrefs[17] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
        preferences->putBytes(preference_acl, (byte*)(&aclPrefs), sizeof(aclPrefs));
        uint32_t basicLockConfigAclPrefs[16] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
        preferences->putBytes(preference_conf_lock_basic_acl, (byte*)(&basicLockConfigAclPrefs), sizeof(basicLockConfigAclPrefs));
        uint32_t basicOpenerConfigAclPrefs[14] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
        preferences->putBytes(preference_conf_opener_basic_acl, (byte*)(&basicOpenerConfigAclPrefs), sizeof(basicOpenerConfigAclPrefs));
        uint32_t advancedLockConfigAclPrefs[22] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
        preferences->putBytes(preference_conf_lock_advanced_acl, (byte*)(&advancedLockConfigAclPrefs), sizeof(advancedLockConfigAclPrefs));
        uint32_t advancedOpenerConfigAclPrefs[20] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
        preferences->putBytes(preference_conf_opener_advanced_acl, (byte*)(&advancedOpenerConfigAclPrefs), sizeof(advancedOpenerConfigAclPrefs));
    }
    else
    {
        int configVer = preferences->getInt(preference_config_version);

        if(configVer < (atof(NUKI_HUB_VERSION) * 100))
        {
            if (configVer < 834)
            {
                if(preferences->getInt(preference_keypad_control_enabled))
                {
                    preferences->putBool(preference_keypad_info_enabled, true);
                }
                else
                {
                    preferences->putBool(preference_keypad_info_enabled, false);
                }

                switch(preferences->getInt(preference_access_level))
                {
                    case 0:
                        {
                            preferences->putBool(preference_keypad_control_enabled, true);
                            uint32_t aclPrefs[17] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
                            preferences->putBytes(preference_acl, (byte*)(&aclPrefs), sizeof(aclPrefs));
                            uint32_t basicLockConfigAclPrefs[16] = {0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0};
                            preferences->putBytes(preference_conf_lock_basic_acl, (byte*)(&basicLockConfigAclPrefs), sizeof(basicLockConfigAclPrefs));
                            uint32_t basicOpenerConfigAclPrefs[14] = {0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0};
                            preferences->putBytes(preference_conf_opener_basic_acl, (byte*)(&basicOpenerConfigAclPrefs), sizeof(basicOpenerConfigAclPrefs));
                            uint32_t advancedLockConfigAclPrefs[22] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0};
                            preferences->putBytes(preference_conf_lock_advanced_acl, (byte*)(&advancedLockConfigAclPrefs), sizeof(advancedLockConfigAclPrefs));
                            uint32_t advancedOpenerConfigAclPrefs[20] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0};
                            preferences->putBytes(preference_conf_opener_advanced_acl, (byte*)(&advancedOpenerConfigAclPrefs), sizeof(advancedOpenerConfigAclPrefs));
                            break;
                        }
                    case 1:
                        {
                            preferences->putBool(preference_keypad_control_enabled, false);
                            uint32_t aclPrefs[17] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0};
                            preferences->putBytes(preference_acl, (byte*)(&aclPrefs), sizeof(aclPrefs));
                            uint32_t basicLockConfigAclPrefs[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
                            preferences->putBytes(preference_conf_lock_basic_acl, (byte*)(&basicLockConfigAclPrefs), sizeof(basicLockConfigAclPrefs));
                            uint32_t basicOpenerConfigAclPrefs[14] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
                            preferences->putBytes(preference_conf_opener_basic_acl, (byte*)(&basicOpenerConfigAclPrefs), sizeof(basicOpenerConfigAclPrefs));
                            uint32_t advancedLockConfigAclPrefs[22] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
                            preferences->putBytes(preference_conf_lock_advanced_acl, (byte*)(&advancedLockConfigAclPrefs), sizeof(advancedLockConfigAclPrefs));
                            uint32_t advancedOpenerConfigAclPrefs[20] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
                            preferences->putBytes(preference_conf_opener_advanced_acl, (byte*)(&advancedOpenerConfigAclPrefs), sizeof(advancedOpenerConfigAclPrefs));
                            break;
                        }
                    case 2:
                        {
                            preferences->putBool(preference_keypad_control_enabled, false);
                            uint32_t aclPrefs[17] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
                            preferences->putBytes(preference_acl, (byte*)(&aclPrefs), sizeof(aclPrefs));
                            uint32_t basicLockConfigAclPrefs[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
                            preferences->putBytes(preference_conf_lock_basic_acl, (byte*)(&basicLockConfigAclPrefs), sizeof(basicLockConfigAclPrefs));
                            uint32_t basicOpenerConfigAclPrefs[14] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
                            preferences->putBytes(preference_conf_opener_basic_acl, (byte*)(&basicOpenerConfigAclPrefs), sizeof(basicOpenerConfigAclPrefs));
                            uint32_t advancedLockConfigAclPrefs[22] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
                            preferences->putBytes(preference_conf_lock_advanced_acl, (byte*)(&advancedLockConfigAclPrefs), sizeof(advancedLockConfigAclPrefs));
                            uint32_t advancedOpenerConfigAclPrefs[20] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
                            preferences->putBytes(preference_conf_opener_advanced_acl, (byte*)(&advancedOpenerConfigAclPrefs), sizeof(advancedOpenerConfigAclPrefs));
                            break;
                        }
                    case 3:
                        {
                            preferences->putBool(preference_keypad_control_enabled, false);
                            uint32_t aclPrefs[17] = {1, 1, 0, 1, 0, 1, 0, 0, 0, 1, 1, 0, 1, 1, 0, 0, 0};
                            preferences->putBytes(preference_acl, (byte*)(&aclPrefs), sizeof(aclPrefs));
                            uint32_t basicLockConfigAclPrefs[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
                            preferences->putBytes(preference_conf_lock_basic_acl, (byte*)(&basicLockConfigAclPrefs), sizeof(basicLockConfigAclPrefs));
                            uint32_t basicOpenerConfigAclPrefs[14] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
                            preferences->putBytes(preference_conf_opener_basic_acl, (byte*)(&basicOpenerConfigAclPrefs), sizeof(basicOpenerConfigAclPrefs));
                            uint32_t advancedLockConfigAclPrefs[22] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
                            preferences->putBytes(preference_conf_lock_advanced_acl, (byte*)(&advancedLockConfigAclPrefs), sizeof(advancedLockConfigAclPrefs));
                            uint32_t advancedOpenerConfigAclPrefs[20] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
                            preferences->putBytes(preference_conf_opener_advanced_acl, (byte*)(&advancedOpenerConfigAclPrefs), sizeof(advancedOpenerConfigAclPrefs));
                            break;
                        }
                }
            }

            preferences->putInt(preference_config_version, atof(NUKI_HUB_VERSION) * 100);
        }
    }

    return firstStart;
}

void setup()
{
    Serial.begin(115200);
    Log = &Serial;

    Log->print(F("Nuki Hub version ")); Log->println(NUKI_HUB_VERSION);
    Log->print(F("Nuki Hub build ")); Log->println(NUKI_HUB_BUILD);

    bool firstStart = initPreferences();

    initializeRestartReason();

    if(preferences->getBool(preference_enable_bootloop_reset, false) &&
    (esp_reset_reason() == esp_reset_reason_t::ESP_RST_PANIC ||
    esp_reset_reason() == esp_reset_reason_t::ESP_RST_INT_WDT ||
    esp_reset_reason() == esp_reset_reason_t::ESP_RST_TASK_WDT ||
    esp_reset_reason() == esp_reset_reason_t::ESP_RST_WDT))
    {
        preferences->putInt(preference_bootloop_counter, preferences->getInt(preference_bootloop_counter, 0) + 1);
        Log->println(F("Bootloop counter incremented"));

        if(preferences->getInt(preference_bootloop_counter) == 10)
        {
            preferences->putInt(preference_buffer_size, CHAR_BUFFER_SIZE);
            preferences->putInt(preference_task_size_network, NETWORK_TASK_SIZE);
            preferences->putInt(preference_task_size_nuki, NUKI_TASK_SIZE);
            preferences->putInt(preference_task_size_pd, PD_TASK_SIZE);
            preferences->putInt(preference_authlog_max_entries, MAX_AUTHLOG);
            preferences->putInt(preference_keypad_max_entries, MAX_KEYPAD);
            preferences->putInt(preference_timecontrol_max_entries, MAX_TIMECONTROL);
            preferences->putInt(preference_bootloop_counter, 0);
        }
    }

    uint32_t devIdOpener = preferences->getUInt(preference_device_id_opener);

    deviceIdLock = new NukiDeviceId(preferences, preference_device_id_lock);
    deviceIdOpener = new NukiDeviceId(preferences, preference_device_id_opener);

    if(deviceIdLock->get() != 0 && devIdOpener == 0)
    {
        deviceIdOpener->assignId(deviceIdLock->get());
    }

    char16_t buffer_size = preferences->getInt(preference_buffer_size, 4096);

    CharBuffer::initialize(buffer_size);

    if(preferences->getInt(preference_restart_timer) != 0)
    {
        preferences->remove(preference_restart_timer);
    }

    gpio = new Gpio(preferences);
    String gpioDesc;
    gpio->getConfigurationText(gpioDesc, gpio->pinConfiguration(), "\n\r");
    Serial.print(gpioDesc.c_str());

    lockEnabled = preferences->getBool(preference_lock_enabled);
    openerEnabled = preferences->getBool(preference_opener_enabled);

    const String mqttLockPath = preferences->getString(preference_mqtt_lock_path);
    network = new NukiNetwork(preferences, gpio, mqttLockPath, CharBuffer::get(), buffer_size);
    network->initialize();

    networkLock = new NukiNetworkLock(network, preferences, CharBuffer::get(), buffer_size);
    networkLock->initialize();

    if(openerEnabled)
    {
        networkOpener = new NukiNetworkOpener(network, preferences, CharBuffer::get(), buffer_size);
        networkOpener->initialize();
    }

    initEthServer(network->networkDeviceType());

    bleScanner = new BleScanner::Scanner();
    bleScanner->initialize("NukiHub");
    bleScanner->setScanDuration(10 * 1000);

    Log->println(lockEnabled ? F("Nuki Lock enabled") : F("Nuki Lock disabled"));
    if(lockEnabled)
    {
        nuki = new NukiWrapper("NukiHub", deviceIdLock, bleScanner, networkLock, gpio, preferences);
        nuki->initialize(firstStart);
    }

    Log->println(openerEnabled ? F("Nuki Opener enabled") : F("Nuki Opener disabled"));
    if(openerEnabled)
    {
        nukiOpener = new NukiOpenerWrapper("NukiHub", deviceIdOpener, bleScanner, networkOpener, gpio, preferences);
        nukiOpener->initialize();
    }

    if(preferences->getBool(preference_webserver_enabled, true))
    {
        webCfgServer = new WebCfgServer(nuki, nukiOpener, network, gpio, ethServer, preferences, network->networkDeviceType() == NetworkDeviceType::WiFi);
        webCfgServer->initialize();
    }

    if(preferences->getInt(preference_presence_detection_timeout) >= 0)
    {
        presenceDetection = new PresenceDetection(preferences, bleScanner, network, CharBuffer::getPresence(), CHAR_BUFFER_SIZE);
        presenceDetection->initialize();
    }

    setupTasks();
}

void loop()
{
    vTaskDelete(NULL);
}

void printBeforeSetupInfo()
{
}

void printAfterSetupInfo()
{
}