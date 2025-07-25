#define IS_VALID_DETECT 0xa00ab00bc00bd00d;

#include "Arduino.h"
#include "esp_crt_bundle.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_task_wdt.h"
#ifdef CONFIG_HEAP_TASK_TRACKING
#include "esp_heap_task_info.h"
#include "esp_heap_caps.h"
#endif
#include "Config.h"
#include "esp32-hal-log.h"
#include "hal/wdt_hal.h"
#include "esp_chip_info.h"
#include "esp_netif_sntp.h"
#include "esp_core_dump.h"
#include "FS.h"
#include "SPIFFS.h"
//#include <ESPmDNS.h>
#ifdef NUKI_HUB_HTTPS_SERVER
bool nuki_hub_https_server_enabled = true;
#else
bool nuki_hub_https_server_enabled = false;
#endif
#if defined(CONFIG_SOC_SPIRAM_SUPPORTED) && defined(CONFIG_SPIRAM)
#include "esp_psram.h"
#endif

#ifndef NUKI_HUB_UPDATER
#include "SerialReader.h"
#include "NukiWrapper.h"
#include "NukiNetworkLock.h"
#include "NukiOpenerWrapper.h"
#include "Gpio.h"
#include "Gpio.h"
#include "CharBuffer.h"
#include "NukiDeviceId.h"
#include "WebCfgServer.h"
#include "Logger.h"
#include "PreferencesKeys.h"
#include "RestartReason.h"
#include "EspMillis.h"
#include "NimBLEDevice.h"
#include "ImportExport.h"

NukiNetworkLock* networkLock = nullptr;
NukiNetworkOpener* networkOpener = nullptr;
BleScanner::Scanner* bleScanner = nullptr;
NukiWrapper* nuki = nullptr;
NukiOfficial* nukiOfficial = nullptr;
NukiOpenerWrapper* nukiOpener = nullptr;
NukiDeviceId* deviceIdLock = nullptr;
NukiDeviceId* deviceIdOpener = nullptr;
Gpio* gpio = nullptr;
SerialReader* serialReader = nullptr;

bool bleDone = false;
bool lockEnabled = false;
bool openerEnabled = false;
bool wifiConnected = false;
bool rebootLock = false;
uint8_t lockRestartControllerCount = 0;
uint8_t openerRestartControllerCount = 0;
char16_t buffer_size = CHAR_BUFFER_SIZE;

TaskHandle_t nukiTaskHandle = nullptr;

int64_t restartTs = (pow(2,63) - (5 * 1000 * 60000)) / 1000;

#else
#include "../../src/WebCfgServer.h"
#include "../../src/Logger.h"
#include "../../src/PreferencesKeys.h"
#include "../../src/RestartReason.h"
#include "../../src/NukiNetwork.h"
#include "../../src/EspMillis.h"
#include "../../src/ImportExport.h"

int64_t restartTs = 10 * 60 * 1000;

#endif

char log_print_buffer[1024];

PsychicHttpServer* psychicServer = nullptr;
PsychicHttpServer* psychicServerRedirect = nullptr;
PsychicHttpsServer* psychicSSLServer = nullptr;
PsychicWebSocketHandler* websocketHandler = nullptr;

NukiNetwork* network = nullptr;
WebCfgServer* webCfgServer = nullptr;
WebCfgServer* webCfgServerSSL = nullptr;
Preferences* preferences = nullptr;
ImportExport* importExport = nullptr;

RTC_NOINIT_ATTR int espRunning;
RTC_NOINIT_ATTR int restartReason;
RTC_NOINIT_ATTR uint64_t restartReasonValidDetect;
RTC_NOINIT_ATTR bool rebuildGpioRequested;
RTC_NOINIT_ATTR uint64_t bootloopValidDetect;
RTC_NOINIT_ATTR int8_t bootloopCounter;
RTC_NOINIT_ATTR bool forceEnableWebServer;
RTC_NOINIT_ATTR bool disableNetwork;
RTC_NOINIT_ATTR bool wifiFallback;
RTC_NOINIT_ATTR bool ethCriticalFailure;

bool coredumpPrinted = true;
bool timeSynced = false;
bool webStarted = false;
bool webSSLStarted = false;
bool lockStarted = false;
bool openerStarted = false;
bool bleScannerStarted = false;
bool webSerialEnabled = false;
uint8_t partitionType = -1;

int lastHTTPeventId = -1;
bool doOta = false;
bool restartReason_isValid;
RestartReason currentRestartReason = RestartReason::NotApplicable;

TaskHandle_t otaTaskHandle = nullptr;
TaskHandle_t networkTaskHandle = nullptr;

ssize_t write_fn(void* cookie, const char* buf, ssize_t size)
{
    Log->write((uint8_t *)buf, (size_t)size);
    return size;
}

void ets_putc_handler(char c)
{
    static char buf[1024];
    static size_t buf_pos = 0;
    buf[buf_pos] = c;
    buf_pos++;
    if (c == '\n' || buf_pos == sizeof(buf))
    {
        write_fn(NULL, buf, buf_pos);
        buf_pos = 0;
    }
}

int _log_vprintf(const char *fmt, va_list args)
{
    int ret = vsnprintf(log_print_buffer, sizeof(log_print_buffer), fmt, args);
    if (ret >= 0)
    {
        Log->write((uint8_t *)log_print_buffer, (size_t)ret);
    }
    return 0;
}

void setReroute()
{
    esp_log_set_vprintf(_log_vprintf);

    #ifdef DEBUG_NUKIHUB
    esp_log_level_set("*", ESP_LOG_DEBUG);
    esp_log_level_set("nvs", ESP_LOG_INFO);
    esp_log_level_set("wifi", ESP_LOG_INFO);
    #else
    /*
    esp_log_level_set("*", ESP_LOG_NONE);
    esp_log_level_set("httpd", ESP_LOG_ERROR);
    esp_log_level_set("httpd_sess", ESP_LOG_ERROR);
    esp_log_level_set("httpd_parse", ESP_LOG_ERROR);
    esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
    esp_log_level_set("event", ESP_LOG_ERROR);
    esp_log_level_set("psychic", ESP_LOG_ERROR);
    esp_log_level_set("ARDUINO", ESP_LOG_DEBUG);
    esp_log_level_set("nvs", ESP_LOG_ERROR);
    esp_log_level_set("wifi", ESP_LOG_ERROR);
    */
    #endif

    if(preferences->getBool(preference_mqtt_log_enabled))
    {
        esp_log_level_set("mqtt", ESP_LOG_NONE);
    }

}

uint8_t checkPartition()
{
    const esp_partition_t* running_partition = esp_ota_get_running_partition();
    Log->print("Partition size: ");
    Log->println(running_partition->size);
    Log->print("Partition subtype: ");
    Log->println(running_partition->subtype);


    #if !defined(CONFIG_IDF_TARGET_ESP32C5) && !defined(CONFIG_IDF_TARGET_ESP32P4)
    if(running_partition->size == 1966080)
    {
        return 0;    //OLD PARTITION TABLE
    }
    #endif

    if(running_partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0)
    {
        return 1;    //NEW PARTITION TABLE, RUNNING MAIN APP
    }
    else
    {
        return 2;    //NEW PARTITION TABLE, RUNNING UPDATER APP
    }
}

void listDir(fs::FS &fs, const char *dirname, uint8_t levels) {
  Serial.printf("Listing directory: %s\r\n", dirname);

  File root = fs.open(dirname);
  if (!root) {
    Serial.println("- failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println(" - not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if (levels) {
        listDir(fs, file.path(), levels - 1);
      }
    } else {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("\tSIZE: ");
      Serial.println(file.size());
    }

    if (file.size() > (int)(SPIFFS.totalBytes() * 0.4))
    {
        SPIFFS.remove((String)"/" + file.name());
    }

    file = root.openNextFile();
  }
}

void cbSyncTime(struct timeval *tv)  {
  Log->println("NTP time synced");
  timeSynced = true;
}

#ifndef NUKI_HUB_UPDATER
void startWebServer()
{
    bool failed = true;
    
    webSerialEnabled = preferences->getBool(preference_webserial_enabled, false);
    
    if (!nuki_hub_https_server_enabled) 
    {
        Log->println("Not running on PSRAM enabled device");
    }
    else
    {
        if (!SPIFFS.begin(true)) 
        {
            Log->println("SPIFFS Mount Failed");
        }
        else
        {
            File file = SPIFFS.open("/http_ssl.crt");
            if (!file || file.isDirectory()) {
                Log->println("http_ssl.crt not found");
            }
            else
            {
                Log->println("Reading http_ssl.crt");
                size_t filesize = file.size();
                char cert[filesize + 1];

                file.read((uint8_t *)cert, sizeof(cert));
                file.close();
                cert[filesize] = '\0';

                File file2 = SPIFFS.open("/http_ssl.key");
                if (!file2 || file2.isDirectory()) 
                {
                    Log->println("http_ssl.key not found");
                }
                else
                {
                    Log->println("Reading http_ssl.key");
                    size_t filesize2 = file2.size();
                    char key[filesize2 + 1];

                    file2.read((uint8_t *)key, sizeof(key));
                    file2.close();
                    key[filesize2] = '\0';

                    psychicServerRedirect = new PsychicHttpServer();
                    psychicServerRedirect->config.ctrl_port = 20424;
                    psychicServerRedirect->onNotFound([](PsychicRequest* request, PsychicResponse* response) {
                        String url = "https://" + request->host() + request->url();
                        if (preferences->getString(preference_https_fqdn, "") != "")
                        {
                            url = "https://" + preferences->getString(preference_https_fqdn) + request->url();
                        }

                        response->setCode(301);
                        response->addHeader("Cache-Control", "no-cache");
                        return response->redirect(url.c_str());
                    });
                    psychicServerRedirect->begin();
                    psychicSSLServer = new PsychicHttpsServer;
                    psychicSSLServer->ssl_config.httpd.max_open_sockets = 8;
                    psychicSSLServer->setCertificate(cert, key);
                    psychicSSLServer->config.stack_size = HTTPD_TASK_SIZE;
                    webCfgServerSSL = new WebCfgServer(nuki, nukiOpener, network, gpio, preferences, network->networkDeviceType() == NetworkDeviceType::WiFi, partitionType, psychicSSLServer, importExport);
                    webCfgServerSSL->initialize();
                    psychicSSLServer->onNotFound([](PsychicRequest* request, PsychicResponse* response) {
                        return response->redirect("/");
                    });
                    psychicSSLServer->begin();
                    webSSLStarted = true;
                    failed = false;
                }
            }
        }
    }

    if (failed)
    {
        psychicServer = new PsychicHttpServer;
        psychicServer->config.stack_size = HTTPD_TASK_SIZE;
        webCfgServer = new WebCfgServer(nuki, nukiOpener, network, gpio, preferences, network->networkDeviceType() == NetworkDeviceType::WiFi, partitionType, psychicServer, importExport);
        webCfgServer->initialize();
        psychicServer->onNotFound([](PsychicRequest* request, PsychicResponse* response) {
            return response->redirect("/");
        });
        psychicServer->begin();
        webStarted = true;
    }
}

void startNuki(bool lock)
{
    if (lock)
    {
        nukiOfficial = new NukiOfficial(preferences);
        networkLock = new NukiNetworkLock(network, nukiOfficial, preferences, CharBuffer::get(), buffer_size);

        if(!disableNetwork)
        {
            networkLock->initialize();
        }

        lockStarted = true;
    }
    else
    {
        networkOpener = new NukiNetworkOpener(network, preferences, CharBuffer::get(), buffer_size);

        if(!disableNetwork)
        {
            networkOpener->initialize();
        }

        openerStarted = true;
    }
}

void restartServices(bool reconnect)
{
    bleDone = false;
    lockEnabled = preferences->getBool(preference_lock_enabled);
    openerEnabled = preferences->getBool(preference_opener_enabled);
    importExport->readSettings();
    network->readSettings();
    gpio->setPins();

    if (reconnect)
    {
        network->reconnect(true);
    }

    if(webSSLStarted)
    {
        Log->println("Reset Psychic SSL server");
        psychicSSLServer->reset();
        Log->println("Reset Psychic SSL server done");
        Log->println("Deleting Psychic SSL server");
        delete psychicSSLServer;
        psychicSSLServer = nullptr;
        Log->println("Deleting Psychic SSL server done");
    }

    if(webStarted)
    {
        Log->println("Reset Psychic server");
        psychicServer->reset();
        Log->println("Reset Psychic server done");
        Log->println("Deleting Psychic server");
        delete psychicServer;
        psychicServer = nullptr;
        Log->println("Deleting Psychic server done");
    }

    if(webStarted || webSSLStarted)
    {
        Log->println("Deleting webCfgServer");
        delete webCfgServer;
        webCfgServer = nullptr;
        Log->println("Deleting webCfgServer done");
    }

    if(lockStarted)
    {
        Log->println("Deleting nuki");
        delete nuki;
        nuki = nullptr;
        if (reconnect)
        {
            lockStarted = false;
            delete networkLock;
            networkLock = nullptr;
            delete nukiOfficial;
            nukiOfficial = nullptr;
        }
        Log->println("Deleting nuki done");
    }

    if(openerStarted)
    {
        Log->println("Deleting nukiOpener");
        delete nukiOpener;
        nukiOpener = nullptr;
        if (reconnect)
        {
            openerStarted = false;            
            delete networkOpener;
            networkOpener = nullptr;
        }
        Log->println("Deleting nukiOpener done");
    }

    if (bleScannerStarted)
    {
        bleScannerStarted = false;
        Log->println("Destroying scanner from main");
        delete bleScanner;
        Log->println("Scanner deleted");
        bleScanner = nullptr;
        Log->println("Scanner nulled from main");
    }

    if (BLEDevice::isInitialized()) {
        Log->println("Deinit BLE device");
        BLEDevice::deinit(false);
        Log->println("Deinit BLE device done");
    }

    if (esp_task_wdt_status(NULL) == ESP_OK) {
        esp_task_wdt_reset();
    }
    vTaskDelay(2000 / portTICK_PERIOD_MS);

    if(lockEnabled || openerEnabled)
    {
        Log->println("Restarting BLE Scanner");
        bleScanner = new BleScanner::Scanner();
        bleScanner->initialize("NukiHub", true, 40, 40);
        bleScanner->setScanDuration(0);
        bleScannerStarted = true;
        Log->println("Restarting BLE Scanner done");
    }

    if(lockEnabled)
    {
        Log->println("Restarting Nuki lock");

        if (reconnect)
        {
            startNuki(true);
        }

        nuki = new NukiWrapper("NukiHub", deviceIdLock, bleScanner, networkLock, nukiOfficial, gpio, preferences, CharBuffer::get(), buffer_size);
        nuki->initialize();
        bleScanner->whitelist(nuki->getBleAddress());
        Log->println("Restarting Nuki lock done");
    }

    if(openerEnabled)
    {
        Log->println("Restarting Nuki opener");

        if (reconnect)
        {
            startNuki(false);
        }

        nukiOpener = new NukiOpenerWrapper("NukiHub", deviceIdOpener, bleScanner, networkOpener, gpio, preferences, CharBuffer::get(), buffer_size);
        nukiOpener->initialize();
        bleScanner->whitelist(nukiOpener->getBleAddress());
        Log->println("Restarting Nuki opener done");
    }


    if (esp_task_wdt_status(NULL) == ESP_OK) {
        esp_task_wdt_reset();
    }
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    bleDone = true;

    if(webStarted || webSSLStarted)
    {
        Log->println("Restarting web server");
        startWebServer();
        Log->println("Restarting web server done");
    }
    else if(!doOta && !disableNetwork && (forceEnableWebServer || preferences->getBool(preference_webserver_enabled, true) || preferences->getBool(preference_webserial_enabled, false)))
    {
        if(forceEnableWebServer || preferences->getBool(preference_webserver_enabled, true))
        {
            Log->println("Starting web server");
            startWebServer();
            Log->println("Starting web server done");
        }
    }
}
#endif

void networkTask(void *pvParameters)
{
    int64_t networkLoopTs = 0;
    bool reroute = true;
    if(preferences->getBool(preference_show_secrets, false))
    {
        preferences->putBool(preference_show_secrets, false);
    }
    while(true)
    {
        int64_t ts = espMillis();
        if(ts > 120000 && ts < 125000)
        {
            if(bootloopCounter > 0)
            {
                bootloopCounter = (int8_t)0;
                Log->println("Bootloop counter reset");
            }
        }

#ifndef NUKI_HUB_UPDATER
        if(serialReader != nullptr)
        {
            serialReader->update();
        }
#endif
        network->update();
        if (esp_task_wdt_status(NULL) == ESP_OK) {
            esp_task_wdt_reset();
        }
        vTaskDelay(50 / portTICK_PERIOD_MS);
        bool connected = network->isConnected();

        if(connected && reroute)
        {
            if(preferences->getBool(preference_update_time, false))
            {
                esp_netif_sntp_start();
            }

            /* MDNS currently disabled for causing issues (9.10 / 2025-04-01)
            if(webSSLStarted) {
                if (MDNS.begin(preferences->getString(preference_hostname, "nukihub").c_str())) {
                    MDNS.addService("http", "tcp", 443);
                }
            }
            else if(webStarted) {
                if (MDNS.begin(preferences->getString(preference_hostname, "nukihub").c_str())) {
                    MDNS.addService("http", "tcp", 80);
                }
            }
            */

            reroute = false;
            setReroute();
        }

#ifndef NUKI_HUB_UPDATER
        wifiConnected = network->wifiConnected();
        int restartServ = network->getRestartServices();

        if (restartServ == 1)
        {
            restartServices(false);            
        }
        else if (restartServ == 2)
        {
            restartServices(true);
        }
        else
        {
            if(connected && webSerialEnabled && (webSSLStarted || webStarted))
            {
                webCfgServerSSL->updateWebSerial();
                if (esp_task_wdt_status(NULL) == ESP_OK) {
                    esp_task_wdt_reset();
                }
                vTaskDelay(50 / portTICK_PERIOD_MS);
            }
            
            if(connected && lockStarted)
            {
                rebootLock = networkLock->update();
                if (esp_task_wdt_status(NULL) == ESP_OK) {
                    esp_task_wdt_reset();
                }
                vTaskDelay(50 / portTICK_PERIOD_MS);
            }

            if(connected && openerStarted)
            {
                networkOpener->update();
                if (esp_task_wdt_status(NULL) == ESP_OK) {
                    esp_task_wdt_reset();
                }
                vTaskDelay(50 / portTICK_PERIOD_MS);
            }
        }
#endif

        if(espMillis() - networkLoopTs > 120000)
        {
            Log->println("networkTask is running");
            networkLoopTs = espMillis();
        }

        if(espMillis() > restartTs)
        {
            partitionType = checkPartition();

            if(partitionType!=1)
            {
                esp_ota_set_boot_partition(esp_ota_get_next_update_partition(NULL));
            }

            restartEsp(RestartReason::RestartTimer);
        }
        
        if (esp_task_wdt_status(NULL) == ESP_OK) {
            esp_task_wdt_reset();
        }
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}

#ifndef NUKI_HUB_UPDATER
#ifdef CONFIG_HEAP_TASK_TRACKING
static void print_all_tasks_info(void)
{
    heap_all_tasks_stat_t tasks_stat;
    /* call API to dynamically allocate the memory necessary to store the
     * information collected while calling heap_caps_get_all_task_stat */
    const esp_err_t ret_val = heap_caps_alloc_all_task_stat_arrays(&tasks_stat);
    assert(ret_val == ESP_OK);

    /* collect the information */
    heap_caps_get_all_task_stat(&tasks_stat);

    /* process the information retrieved */
    Log->printf("\n--------------------------------------------------------------------------------\n");
    Log->printf("PRINTING ALL TASKS INFO\n");
    Log->printf("--------------------------------------------------------------------------------\n");
    for (size_t task_idx = 0; task_idx < tasks_stat.task_count; task_idx++) {
        task_stat_t task_stat = tasks_stat.stat_arr[task_idx];
        Log->printf("%s: %s: Peak Usage %" PRIu16 ", Current Usage %" PRIu16 "\n", task_stat.name,
                                                                          task_stat.is_alive ? "ALIVE  " : "DELETED",
                                                                          task_stat.overall_peak_usage,
                                                                          task_stat.overall_current_usage);

        for (size_t heap_idx = 0; heap_idx < task_stat.heap_count; heap_idx++) {
            heap_stat_t heap_stat = task_stat.heap_stat[heap_idx];
            Log->printf("    %s: Caps: %" PRIu32 ". Size %" PRIu16 ", Current Usage %" PRIu16 ", Peak Usage %" PRIu16 ", alloc count %" PRIu16 "\n", heap_stat.name,
                                                                                                                                      heap_stat.caps,
                                                                                                                                      heap_stat.size,
                                                                                                                                      heap_stat.current_usage,
                                                                                                                                      heap_stat.peak_usage,
                                                                                                                                      heap_stat.alloc_count);

            for (size_t alloc_idx = 0; alloc_idx < heap_stat.alloc_count; alloc_idx++) {
                heap_task_block_t alloc_stat = heap_stat.alloc_stat[alloc_idx];
                Log->printf("        %p: Size: %" PRIu32 "\n", alloc_stat.address, alloc_stat.size);
            }
        }
    }

    /* delete the memory dynamically allocated while calling heap_caps_alloc_all_task_stat_arrays */
    heap_caps_free_all_task_stat_arrays(&tasks_stat);
}
#endif
void nukiTask(void *pvParameters)
{
    esp_task_wdt_add(NULL);
    
    if (preferences->getBool(preference_mqtt_ssl_enabled, false))
    {
        #if defined(CONFIG_SOC_SPIRAM_SUPPORTED) && defined(CONFIG_SPIRAM)
        if (esp_psram_get_size() <= 0)
        {
            Log->println("Waiting 20 seconds to start BLE because of MQTT SSL");
            if (esp_task_wdt_status(NULL) == ESP_OK) {
                esp_task_wdt_reset();
            }
            vTaskDelay(20000 / portTICK_PERIOD_MS);
        }
        #else
        Log->println("Waiting 20 seconds to start BLE because of MQTT SSL");
        if (esp_task_wdt_status(NULL) == ESP_OK) {
            esp_task_wdt_reset();
        }
        vTaskDelay(20000 / portTICK_PERIOD_MS);
        #endif
    }
    int64_t nukiLoopTs = 0;
    bool whiteListed = false;
    while(true)
    {
        if((disableNetwork || wifiConnected) && bleDone)
        {
            if(bleScannerStarted)
            {
                bleScanner->update();
                if (esp_task_wdt_status(NULL) == ESP_OK) {
                    esp_task_wdt_reset();
                }
                vTaskDelay(20 / portTICK_PERIOD_MS);
            }
            
            bool needsPairing = (lockStarted && !nuki->isPaired()) || (openerStarted && !nukiOpener->isPaired());

            if (needsPairing)
            {
                if (esp_task_wdt_status(NULL) == ESP_OK) {
                    esp_task_wdt_reset();
                }
                vTaskDelay(2500 / portTICK_PERIOD_MS);
            }
            else if (!whiteListed)
            {
                whiteListed = true;
                if(lockEnabled)
                {
                    bleScanner->whitelist(nuki->getBleAddress());
                }
                if(openerEnabled)
                {
                    bleScanner->whitelist(nukiOpener->getBleAddress());
                }
            }

            if(lockStarted)
            {
                if (nuki->restartController() > 0)
                {
                    if (lockRestartControllerCount > 3)
                    {
                        if (nuki->restartController() == 1)
                        {
                            restartEsp(RestartReason::BLEError);
                        }
                        else if (nuki->restartController() == 2)
                        {
                            restartEsp(RestartReason::BLEBeaconWatchdog);
                        }
                    }
                    else
                    {
                        lockRestartControllerCount += 1;
                        restartServices(false);
                        continue;
                    }
                }
                else
                {
                    if (lockRestartControllerCount > 0 && nuki->hasConnected())
                    {
                        lockRestartControllerCount = 0;
                    }

                    nuki->update(rebootLock);
                    rebootLock = false;
                }
            }
            if(openerStarted)
            {
                if (nukiOpener->restartController() > 0)
                {
                    if (openerRestartControllerCount > 3)
                    {
                        if (nukiOpener->restartController() == 1)
                        {
                            restartEsp(RestartReason::BLEError);
                        }
                        else if (nukiOpener->restartController() == 2)
                        {
                            restartEsp(RestartReason::BLEBeaconWatchdog);
                        }
                    }
                    else
                    {
                        openerRestartControllerCount += 1;
                        restartServices(false);
                        continue;
                    }
                }
                else
                {
                    if (openerRestartControllerCount > 0 && nukiOpener->hasConnected())
                    {
                        openerRestartControllerCount = 0;
                    }

                    nukiOpener->update();
                }
            }
        }

        if(espMillis() - nukiLoopTs > 120000)
        {
            Log->println("nukiTask is running");
            nukiLoopTs = espMillis();
        }
        if (esp_task_wdt_status(NULL) == ESP_OK) {
            esp_task_wdt_reset();
        }
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}

void bootloopDetection()
{
    uint64_t cmp = IS_VALID_DETECT;
    bool bootloopIsValid = (bootloopValidDetect == cmp);
    Log->println(bootloopIsValid);

    if(!bootloopIsValid)
    {
        bootloopCounter = (int8_t)0;
        bootloopValidDetect = IS_VALID_DETECT;
        return;
    }

    if(esp_reset_reason() == esp_reset_reason_t::ESP_RST_PANIC ||
            esp_reset_reason() == esp_reset_reason_t::ESP_RST_INT_WDT ||
            esp_reset_reason() == esp_reset_reason_t::ESP_RST_TASK_WDT ||
            esp_reset_reason() == esp_reset_reason_t::ESP_RST_WDT)
    {
        bootloopCounter++;
        Log->print("Bootloop counter incremented: ");
        Log->println(bootloopCounter);

        if(bootloopCounter == 10)
        {
            Log->print("Bootloop detected.");

            preferences->putInt(preference_buffer_size, CHAR_BUFFER_SIZE);
            preferences->putInt(preference_task_size_network, NETWORK_TASK_SIZE);
            preferences->putInt(preference_task_size_nuki, NUKI_TASK_SIZE);
            preferences->putInt(preference_authlog_max_entries, MAX_AUTHLOG);
            preferences->putInt(preference_keypad_max_entries, MAX_KEYPAD);
            preferences->putInt(preference_timecontrol_max_entries, MAX_TIMECONTROL);
            preferences->putInt(preference_auth_max_entries, MAX_AUTH);
            bootloopCounter = 0;
        }
    }
}
#endif

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    if (lastHTTPeventId != int(evt->event_id))
    {
        Log->println("");
        switch (evt->event_id)
        {
            case HTTP_EVENT_ERROR:
                Log->println("HTTP_EVENT_ERROR");
                break;
            case HTTP_EVENT_ON_CONNECTED:
                Log->print("HTTP_EVENT_ON_CONNECTED");
                break;
            case HTTP_EVENT_HEADER_SENT:
                Log->print("HTTP_EVENT_HEADER_SENT");
                break;
            case HTTP_EVENT_ON_HEADER:
                Log->print("HTTP_EVENT_ON_HEADER");
                break;
            case HTTP_EVENT_ON_DATA:
                Log->print("HTTP_EVENT_ON_DATA");
                break;
            case HTTP_EVENT_ON_FINISH:
                Log->println("HTTP_EVENT_ON_FINISH");
                break;
            case HTTP_EVENT_DISCONNECTED:
                Log->println("HTTP_EVENT_DISCONNECTED");
                break;
            case HTTP_EVENT_REDIRECT:
                Log->print("HTTP_EVENT_REDIRECT");
                break;
        }
    }
    else
    {
        Log->print(".");
    }
    lastHTTPeventId = int(evt->event_id);
    wdt_hal_context_t rtc_wdt_ctx = RWDT_HAL_CONTEXT_DEFAULT();
    wdt_hal_write_protect_disable(&rtc_wdt_ctx);
    wdt_hal_feed(&rtc_wdt_ctx);
    wdt_hal_write_protect_enable(&rtc_wdt_ctx);

    return ESP_OK;
}

void otaTask(void *pvParameter)
{
    esp_task_wdt_add(NULL);
    
    partitionType = checkPartition();
    String updateUrl;

    if(partitionType==1)
    {
        updateUrl = preferences->getString(preference_ota_updater_url);
        preferences->putString(preference_ota_updater_url, "");
    }
    else
    {
        updateUrl = preferences->getString(preference_ota_main_url);
        preferences->putString(preference_ota_main_url, "");
    }

    while(!network->isConnected())
    {
        Log->println("OTA waiting for network");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }

    Log->println("Starting OTA task");
    esp_http_client_config_t config =
    {
        .url = updateUrl.c_str(),
        .event_handler = _http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = true,
    };

    esp_https_ota_config_t ota_config =
    {
        .http_config = &config,
    };
    Log->print("Attempting to download update from ");
    Log->println(config.url);

    int retryMax = 3;
    int retryCount = 0;

    while (retryCount <= retryMax)
    {
        esp_err_t ret = esp_https_ota(&ota_config);
        if (ret == ESP_OK)
        {
            Log->println("OTA Succeeded, Rebooting...");
            esp_ota_set_boot_partition(esp_ota_get_next_update_partition(NULL));
            restartEsp(RestartReason::OTACompleted);
            break;
        }
        else
        {
            Log->println("Firmware upgrade failed, retrying in 5 seconds");
            retryCount++;
            if (esp_task_wdt_status(NULL) == ESP_OK) {
                esp_task_wdt_reset();
            }
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            continue;
        }
        while (1)
        {
            if (esp_task_wdt_status(NULL) == ESP_OK) {
                esp_task_wdt_reset();
            }
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }
    Log->println("Firmware upgrade failed, restarting");
    esp_ota_set_boot_partition(esp_ota_get_next_update_partition(NULL));
    restartEsp(RestartReason::OTAAborted);
}

void setupTasks(bool ota)
{
    // configMAX_PRIORITIES is 25
    esp_task_wdt_config_t twdt_config =
    {
        .timeout_ms = 300000,
        .idle_core_mask = (1 << CONFIG_FREERTOS_NUMBER_OF_CORES) - 1,
        .trigger_panic = true,
    };
    esp_task_wdt_reconfigure(&twdt_config);

    esp_chip_info_t info;
    esp_chip_info(&info);
    uint8_t espCores = info.cores;
    Log->print("Cores: ");
    Log->println(espCores);

    if(ota)
    {
        xTaskCreatePinnedToCore(otaTask, "ota", 8192, NULL, 2, &otaTaskHandle, (espCores > 1) ? 1 : 0);
    }
    else
    {
        if(!disableNetwork)
        {
            xTaskCreatePinnedToCore(networkTask, "ntw", preferences->getInt(preference_task_size_network, NETWORK_TASK_SIZE), NULL, 3, &networkTaskHandle, (espCores > 1) ? 1 : 0);
        }
#ifndef NUKI_HUB_UPDATER
        if(!network->isApOpen() && (lockEnabled || openerEnabled))
        {
            xTaskCreatePinnedToCore(nukiTask, "nuki", preferences->getInt(preference_task_size_nuki, NUKI_TASK_SIZE), NULL, 2, &nukiTaskHandle, 0);
        }
#endif
    }
}

void logCoreDump()
{
    coredumpPrinted = false;
    if (esp_task_wdt_status(NULL) == ESP_OK) {
        esp_task_wdt_reset();
    }
    vTaskDelay(500 / portTICK_PERIOD_MS);
    Log->println("Printing coredump and saving to coredump.hex on SPIFFS");
    size_t size = 0;
    size_t address = 0;
    if (esp_core_dump_image_get(&address, &size) == ESP_OK)
    {
        const esp_partition_t *pt = NULL;
        pt = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, "coredump");

        if (pt != NULL)
        {
            File file;
            uint8_t bf[256];
            char str_dst[640];
            int16_t toRead;

            if (!SPIFFS.begin(true))
            {
                Log->println("SPIFFS Mount Failed");
            }
            else
            {
                file = SPIFFS.open("/coredump.hex", FILE_WRITE);
                if (!file) {
                    Log->println("Failed to open /coredump.hex for writing");
                }
                else
                {
                    file.printf("%s\r\n", NUKI_HUB_HW);
                    file.printf("%s\r\n", NUKI_HUB_BUILD);
                }
            }

            Serial.printf("%s\r\n", NUKI_HUB_HW);
            Serial.printf("%s\r\n", NUKI_HUB_BUILD);

            for (int16_t i = 0; i < (size/256)+1; i++)
            {
                strcpy(str_dst, "");
                toRead = (size - i*256) > 256 ? 256 : (size - i*256);

                esp_err_t er = esp_partition_read(pt, i*256, bf, toRead);
                if (er != ESP_OK)
                {
                    Serial.printf("FAIL [%x]", er);
                    break;
                }

                for (int16_t j = 0; j < 256; j++)
                {
                    char str_tmp[2];
                    if (bf[j] <= 0x0F)
                    {
                        sprintf(str_tmp, "0%x", bf[j]);
                    }
                    else
                    {
                        sprintf(str_tmp, "%x", bf[j]);
                    }
                    strcat(str_dst, str_tmp);
                }
                Serial.printf("%s", str_dst);

                if (file) {
                    file.printf("%s", str_dst);
                }
            }

            Serial.println("");

            if (file) {
                file.println("");
                file.close();
            }
        }
        else
        {
            Serial.println("Partition NULL");
        }
    }
    else
    {
        Serial.println("esp_core_dump_image_get() FAIL");
    }
    coredumpPrinted = true;
}

void setup()
{
    #if defined(CONFIG_SOC_SPIRAM_SUPPORTED) && defined(CONFIG_SPIRAM)
    #ifndef FORCE_NUKI_HUB_HTTPS_SERVER
    if(esp_psram_get_size() <= 0)
    {
        nuki_hub_https_server_enabled = false;
    }
    #endif
    #endif
    
    //Set Log level to error for all TAGS
    esp_log_level_set("*", ESP_LOG_ERROR);
    //Set Log level to none for mqtt TAG
    esp_log_level_set("mqtt", ESP_LOG_NONE);
    //Start Serial and setup Log class
    Serial.begin(115200);
    Log = &Serial;

    #if !defined(NUKI_HUB_UPDATER) && !defined(CONFIG_IDF_TARGET_ESP32C5)
    stdout = funopen(NULL, NULL, &write_fn, NULL, NULL);
    static char linebuf[1024];
    setvbuf(stdout, linebuf, _IOLBF, sizeof(linebuf));
    esp_rom_install_channel_putc(1, &ets_putc_handler);
    //ets_install_putc1(&ets_putc_handler);
    #endif

    preferences = new Preferences();
    preferences->begin("nukihub", false);
    initPreferences(preferences);
    initializeRestartReason();

    if(esp_reset_reason() == esp_reset_reason_t::ESP_RST_PANIC ||
            esp_reset_reason() == esp_reset_reason_t::ESP_RST_INT_WDT ||
            esp_reset_reason() == esp_reset_reason_t::ESP_RST_TASK_WDT)
            //|| esp_reset_reason() == esp_reset_reason_t::ESP_RST_WDT)
    {
        logCoreDump();
    }

    if (SPIFFS.begin(true))
    {
        listDir(SPIFFS, "/", 1);
    }

    partitionType = checkPartition();

    //default disableNetwork RTC_ATTR to false on power-on
    if(espRunning != 1)
    {
        espRunning = 1;
        forceEnableWebServer = false;
        disableNetwork = false;
        wifiFallback = false;
        ethCriticalFailure = false;
    }

    //determine if an OTA update was requested
    if((partitionType==1 && preferences->getString(preference_ota_updater_url, "").length() > 0) || (partitionType==2 && preferences->getString(preference_ota_main_url, "").length() > 0))
    {
        doOta = true;
    }

#ifdef NUKI_HUB_UPDATER
    Log->print("Nuki Hub OTA version ");
    Log->println(NUKI_HUB_VERSION);
    Log->print("Nuki Hub OTA build ");
    Log->println();

    if(preferences->getString(preference_updater_version, "") != NUKI_HUB_VERSION)
    {
        preferences->putString(preference_updater_version, NUKI_HUB_VERSION);
    }
    if(preferences->getString(preference_updater_build, "") != NUKI_HUB_BUILD)
    {
        preferences->putString(preference_updater_build, NUKI_HUB_BUILD);
    }
    if(preferences->getString(preference_updater_date, "") != NUKI_HUB_DATE)
    {
        preferences->putString(preference_updater_date, NUKI_HUB_DATE);
    }

    importExport = new ImportExport(preferences);

    network = new NukiNetwork(preferences);
    network->initialize();

    if(!doOta)
    {
        bool failed = true;

        if (!nuki_hub_https_server_enabled) {
            Log->println("Not running on HTTPS server enabled device");
        }
        else
        {
            if (!SPIFFS.begin(true)) {
                Log->println("SPIFFS Mount Failed");
            }
            else
            {
                File file = SPIFFS.open("/http_ssl.crt");
                if (!file || file.isDirectory()) {
                    Log->println("http_ssl.crt not found");
                }
                else
                {
                    Log->println("Reading http_ssl.crt");
                    size_t filesize = file.size();
                    char cert[filesize + 1];

                    file.read((uint8_t *)cert, sizeof(cert));
                    file.close();
                    cert[filesize] = '\0';

                    File file2 = SPIFFS.open("/http_ssl.key");
                    if (!file2 || file2.isDirectory()) {
                        Log->println("http_ssl.key not found");
                    }
                    else
                    {
                        Log->println("Reading http_ssl.key");
                        size_t filesize2 = file2.size();
                        char key[filesize2 + 1];

                        file2.read((uint8_t *)key, sizeof(key));
                        file2.close();
                        key[filesize2] = '\0';

                        psychicServer = new PsychicHttpServer();
                        psychicServer->config.ctrl_port = 20424;
                        psychicServer->onNotFound([](PsychicRequest* request, PsychicResponse* response) {
                            String url = "https://" + request->host() + request->url();
                            if (preferences->getString(preference_https_fqdn, "") != "")
                            {
                                url = "https://" + preferences->getString(preference_https_fqdn) + request->url();
                            }

                            response->setCode(301);
                            response->addHeader("Cache-Control", "no-cache");
                            return response->redirect(url.c_str());
                        });
                        psychicServer->begin();
                        psychicSSLServer = new PsychicHttpsServer;
                        psychicSSLServer->ssl_config.httpd.max_open_sockets = 8;
                        psychicSSLServer->setCertificate(cert, key);
                        psychicSSLServer->config.stack_size = HTTPD_TASK_SIZE;
                        webCfgServerSSL = new WebCfgServer(network, preferences, network->networkDeviceType() == NetworkDeviceType::WiFi, partitionType, psychicSSLServer, importExport);
                        webCfgServerSSL->initialize();
                        psychicSSLServer->onNotFound([](PsychicRequest* request, PsychicResponse* response) {
                            return response->redirect("/");
                        });
                        psychicSSLServer->begin();
                        webSSLStarted = true;
                        failed = false;
                    }
                }
            }
        }

        if (failed)
        {
            psychicServer = new PsychicHttpServer;
            psychicServer->config.stack_size = HTTPD_TASK_SIZE;
            webCfgServer = new WebCfgServer(network, preferences, network->networkDeviceType() == NetworkDeviceType::WiFi, partitionType, psychicServer, importExport);
            webCfgServer->initialize();
            psychicServer->onNotFound([](PsychicRequest* request, PsychicResponse* response) {
                return response->redirect("/");
            });
            psychicServer->begin();
            webStarted = true;
        }
    }
#else
    if(preferences->getBool(preference_enable_bootloop_reset, false))
    {
        bootloopDetection();
    }

    Log->print("Nuki Hub version ");
    Log->println(NUKI_HUB_VERSION);
    Log->print("Nuki Hub build ");
    Log->println(NUKI_HUB_BUILD);
    
    uint32_t devIdOpener = preferences->getUInt(preference_device_id_opener);

    deviceIdLock = new NukiDeviceId(preferences, preference_device_id_lock);
    deviceIdOpener = new NukiDeviceId(preferences, preference_device_id_opener);

    if(deviceIdLock->get() != 0 && devIdOpener == 0)
    {
        deviceIdOpener->assignId(deviceIdLock->get());
    }

    buffer_size = preferences->getInt(preference_buffer_size, CHAR_BUFFER_SIZE);
    CharBuffer::initialize(buffer_size);

    gpio = new Gpio(preferences);
    String gpioDesc;
    gpio->getConfigurationText(gpioDesc, gpio->pinConfiguration(), "\n\r");
    Log->print(gpioDesc.c_str());

    importExport = new ImportExport(preferences);

    network = new NukiNetwork(preferences, gpio, CharBuffer::get(), buffer_size, importExport);
    network->initialize();

    lockEnabled = preferences->getBool(preference_lock_enabled);
    openerEnabled = preferences->getBool(preference_opener_enabled);

    if(network->isApOpen())
    {
        forceEnableWebServer = true;
        doOta = false;
        lockEnabled = false;
        openerEnabled = false;
#ifndef NUKI_HUB_UPDATER
        serialReader = new SerialReader(importExport, network);
#endif
    }

    if(lockEnabled || openerEnabled)
    {
        bleScanner = new BleScanner::Scanner();
        // Scan interval and window according to Nuki recommendations:
        // https://developer.nuki.io/t/bluetooth-specification-questions/1109/27
        bleScanner->initialize("NukiHub", true, 40, 40);
        bleScanner->setScanDuration(0);
        bleScannerStarted = true;
    }

    Log->println(lockEnabled ? F("Nuki Lock enabled") : F("Nuki Lock disabled"));
    if(lockEnabled)
    {
        startNuki(true);

        nuki = new NukiWrapper("NukiHub", deviceIdLock, bleScanner, networkLock, nukiOfficial, gpio, preferences, CharBuffer::get(), buffer_size);
        nuki->initialize();
    }

    Log->println(openerEnabled ? F("Nuki Opener enabled") : F("Nuki Opener disabled"));
    if(openerEnabled)
    {
        startNuki(false);

        nukiOpener = new NukiOpenerWrapper("NukiHub", deviceIdOpener, bleScanner, networkOpener, gpio, preferences, CharBuffer::get(), buffer_size);
        nukiOpener->initialize();
    }

    bleDone = true;

    if(!doOta && !disableNetwork && (forceEnableWebServer || preferences->getBool(preference_webserver_enabled, true) || preferences->getBool(preference_webserial_enabled, false)))
    {
        if(forceEnableWebServer || preferences->getBool(preference_webserver_enabled, true))
        {
            startWebServer();
        }
    }
#endif

    String timeserver = preferences->getString(preference_time_server, "pool.ntp.org");
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(timeserver.c_str());
    config.start = false;
    config.server_from_dhcp = true;
    config.renew_servers_after_new_IP = true;
    config.index_of_first_server = 1;

    if (network->networkDeviceType() == NetworkDeviceType::WiFi)
    {
        config.ip_event_to_renew = IP_EVENT_STA_GOT_IP;
    }
    else
    {
        config.ip_event_to_renew = IP_EVENT_ETH_GOT_IP;
    }
    config.sync_cb = cbSyncTime;
    esp_netif_sntp_init(&config);

    if(doOta)
    {
        setupTasks(true);
    }
    else
    {
        setupTasks(false);
    }

    //print_all_tasks_info();
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
