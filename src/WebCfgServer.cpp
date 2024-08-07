#include "WebCfgServer.h"
#include "WebCfgServerConstants.h"
#include "PreferencesKeys.h"
#include "hardware/WifiEthServer.h"
#include "Logger.h"
#include "Config.h"
#include "RestartReason.h"
#include <esp_task_wdt.h>
#include <esp_wifi.h>

#ifndef NUKI_HUB_UPDATER
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include "ArduinoJson.h"

WebCfgServer::WebCfgServer(NukiWrapper* nuki, NukiOpenerWrapper* nukiOpener, NukiNetwork* network, Gpio* gpio, EthServer* ethServer, Preferences* preferences, bool allowRestartToPortal, uint8_t partitionType)
: _server(ethServer),
  _nuki(nuki),
  _nukiOpener(nukiOpener),
  _network(network),
  _gpio(gpio),
  _preferences(preferences),
  _allowRestartToPortal(allowRestartToPortal),
  _partitionType(partitionType)
#else
WebCfgServer::WebCfgServer(NukiNetwork* network, EthServer* ethServer, Preferences* preferences, bool allowRestartToPortal, uint8_t partitionType)
: _server(ethServer),
  _network(network),
  _preferences(preferences),
  _allowRestartToPortal(allowRestartToPortal),
  _partitionType(partitionType)
#endif
{
    _hostname = _preferences->getString(preference_hostname);
    String str = _preferences->getString(preference_cred_user);

    if(str.length() > 0)
    {
        memset(&_credUser, 0, sizeof(_credUser));
        memset(&_credPassword, 0, sizeof(_credPassword));

        _hasCredentials = true;
        const char *user = str.c_str();
        memcpy(&_credUser, user, str.length());

        str = _preferences->getString(preference_cred_password);
        const char *pass = str.c_str();
        memcpy(&_credPassword, pass, str.length());
    }

    #ifndef NUKI_HUB_UPDATER
    _confirmCode = generateConfirmCode();
    _pinsConfigured = true;

    if(_nuki != nullptr && !_nuki->isPinSet())
    {
        _pinsConfigured = false;
    }
    if(_nukiOpener != nullptr && !_nukiOpener->isPinSet())
    {
        _pinsConfigured = false;
    }

    _brokerConfigured = _preferences->getString(preference_mqtt_broker).length() > 0 && _preferences->getInt(preference_mqtt_broker_port) > 0;
    #endif
}

void WebCfgServer::initialize()
{
    _server.on("/", [&]() {
        if (_hasCredentials && !_server.authenticate(_credUser, _credPassword)) {
            return _server.requestAuthentication();
        }
        String response = "";
        #ifndef NUKI_HUB_UPDATER
        buildHtml(response);
        #else
        buildOtaHtml(response, _server.arg("errored") != "");
        #endif
        _server.send(200, "text/html", response);
    });
    _server.on("/style.css", [&]() {
        if (_hasCredentials && !_server.authenticate(_credUser, _credPassword)) {
            return _server.requestAuthentication();
        }
        sendCss();
    });
    _server.on("/favicon.ico", HTTP_GET, [&]() {
        if (_hasCredentials && !_server.authenticate(_credUser, _credPassword)) {
            return _server.requestAuthentication();
        }
        sendFavicon();
    });
    #ifndef NUKI_HUB_UPDATER
    _server.on("/import", [&]()
    {
        if (_hasCredentials && !_server.authenticate(_credUser, _credPassword)) {
            return _server.requestAuthentication();
        }
        String message = "";
        bool restart = processImport(message);
        if(restart)
        {
            String response = "";
            buildConfirmHtml(response, message);
            _server.send(200, "text/html", response);
            Log->println(F("Restarting"));

            waitAndProcess(true, 1000);
            restartEsp(RestartReason::ImportCompleted);
        }
        else
        {
            String response = "";
            buildConfirmHtml(response, message, 3);
            _server.send(200, "text/html", response);
            waitAndProcess(false, 1000);
        }
    });
    _server.on("/export", HTTP_GET, [&]() {
        if (_hasCredentials && !_server.authenticate(_credUser, _credPassword)) {
            return _server.requestAuthentication();
        }
        sendSettings();
    });
    _server.on("/impexpcfg", [&]() {
        if (_hasCredentials && !_server.authenticate(_credUser, _credPassword)) {
            return _server.requestAuthentication();
        }
        String response = "";
        buildImportExportHtml(response);
        _server.send(200, "text/html", response);
    });
    _server.on("/status", HTTP_GET, [&]() {
        if (_hasCredentials && !_server.authenticate(_credUser, _credPassword)) {
            return _server.requestAuthentication();
        }
        String response = "";
        buildStatusHtml(response);
        _server.send(200, "application/json", response);
    });
    _server.on("/acclvl", [&]() {
        if (_hasCredentials && !_server.authenticate(_credUser, _credPassword)) {
            return _server.requestAuthentication();
        }
        String response = "";
        buildAccLvlHtml(response);
        _server.send(200, "text/html", response);
    });
    _server.on("/advanced", [&]() {
        if (_hasCredentials && !_server.authenticate(_credUser, _credPassword)) {
            return _server.requestAuthentication();
        }
        String response = "";
        buildAdvancedConfigHtml(response);
        _server.send(200, "text/html", response);
    });
    _server.on("/cred", [&]() {
        if (_hasCredentials && !_server.authenticate(_credUser, _credPassword)) {
            return _server.requestAuthentication();
        }
        String response = "";
        buildCredHtml(response);
        _server.send(200, "text/html", response);
    });
    _server.on("/mqttconfig", [&]() {
        if (_hasCredentials && !_server.authenticate(_credUser, _credPassword)) {
            return _server.requestAuthentication();
        }
        String response = "";
        buildMqttConfigHtml(response);
        _server.send(200, "text/html", response);
    });
    _server.on("/nukicfg", [&]() {
        if (_hasCredentials && !_server.authenticate(_credUser, _credPassword)) {
            return _server.requestAuthentication();
        }
        String response = "";
        buildNukiConfigHtml(response);
        _server.send(200, "text/html", response);
    });
    _server.on("/gpiocfg", [&]() {
        if (_hasCredentials && !_server.authenticate(_credUser, _credPassword)) {
            return _server.requestAuthentication();
        }
        String response = "";
        buildGpioConfigHtml(response);
        _server.send(200, "text/html", response);
    });
    _server.on("/wifi", [&]() {
        if (_hasCredentials && !_server.authenticate(_credUser, _credPassword)) {
            return _server.requestAuthentication();
        }
        String response = "";
        buildConfigureWifiHtml(response);
        _server.send(200, "text/html", response);
    });
    _server.on("/unpairlock", [&]() {
        if (_hasCredentials && !_server.authenticate(_credUser, _credPassword)) {
            return _server.requestAuthentication();
        }

        processUnpair(false);
    });
    _server.on("/unpairopener", [&]() {
        if (_hasCredentials && !_server.authenticate(_credUser, _credPassword)) {
            return _server.requestAuthentication();
        }

        processUnpair(true);
    });
    _server.on("/factoryreset", [&]() {
        if (_hasCredentials && !_server.authenticate(_credUser, _credPassword)) {
            return _server.requestAuthentication();
        }

        processFactoryReset();
    });
    _server.on("/wifimanager", [&]() {
        if (_hasCredentials && !_server.authenticate(_credUser, _credPassword)) {
            return _server.requestAuthentication();
        }
        if(_allowRestartToPortal)
        {
            String response = "";
            buildConfirmHtml(response, "Restarting. Connect to ESP access point to reconfigure Wi-Fi.", 0);
            _server.send(200, "text/html", response);
            waitAndProcess(true, 2000);
            _network->reconfigureDevice();
        }
    });
    _server.on("/savecfg", [&]()
    {
        if (_hasCredentials && !_server.authenticate(_credUser, _credPassword)) {
            return _server.requestAuthentication();
        }
        String message = "";
        bool restart = processArgs(message);
        if(restart)
        {
            String response = "";
            buildConfirmHtml(response, message);
            _server.send(200, "text/html", response);
            Log->println(F("Restarting"));

            waitAndProcess(true, 1000);
            restartEsp(RestartReason::ConfigurationUpdated);
        }
        else
        {
            String response = "";
            buildConfirmHtml(response, message, 3);
            _server.send(200, "text/html", response);
            waitAndProcess(false, 1000);
        }
    });
    _server.on("/savegpiocfg", [&]()
    {
        if (_hasCredentials && !_server.authenticate(_credUser, _credPassword)) {
            return _server.requestAuthentication();
        }
        processGpioArgs();

        String response = "";
        buildConfirmHtml(response, "");
        _server.send(200, "text/html", response);
        Log->println(F("Restarting"));

        waitAndProcess(true, 1000);
        restartEsp(RestartReason::GpioConfigurationUpdated);
    });
    _server.on("/info", [&]() {
        if (_hasCredentials && !_server.authenticate(_credUser, _credPassword)) {
            return _server.requestAuthentication();
        }
        String response = "";
        buildInfoHtml(response);
        _server.send(200, "text/html", response);
    });
    _server.on("/debugon", [&]() {
        _preferences->putBool(preference_publish_debug_info, true);

        String response = "";
        buildConfirmHtml(response, "OK");
        _server.send(200, "text/html", response);
        Log->println(F("Restarting"));

        waitAndProcess(true, 1000);
        restartEsp(RestartReason::ConfigurationUpdated);
    });
    _server.on("/debugoff", [&]() {
        _preferences->putBool(preference_publish_debug_info, false);

        String response = "";
        buildConfirmHtml(response, "OK");
        _server.send(200, "text/html", response);
        Log->println(F("Restarting"));

        waitAndProcess(true, 1000);
        restartEsp(RestartReason::ConfigurationUpdated);
    });
    _server.on("/webserial", [&]() {
        _server.sendHeader("Location", (String)"http://" + _network->localIP() + ":81/webserial");
        _server.send(302, "text/plain", "");
    });    
    #endif
    _server.on("/ota", [&]() {
        if (_hasCredentials && !_server.authenticate(_credUser, _credPassword)) {
            return _server.requestAuthentication();
        }
        String response = "";
        buildOtaHtml(response, _server.arg("errored") != "");
        _server.send(200, "text/html", response);
    });
    _server.on("/reboottoota", [&]() {
        if (_hasCredentials && !_server.authenticate(_credUser, _credPassword)) {
            return _server.requestAuthentication();
        }
        String response = "";
        buildConfirmHtml(response, "Rebooting to other partition", 2);
        _server.send(200, "text/html", response);
        waitAndProcess(true, 1000);
        esp_ota_set_boot_partition(esp_ota_get_next_update_partition(NULL));
        restartEsp(RestartReason::OTAReboot);
    });
    _server.on("/autoupdate", [&]() {
        if (_hasCredentials && !_server.authenticate(_credUser, _credPassword)) {
            return _server.requestAuthentication();
        }
        String response = "";
        String key = _server.argName(0);
        if(key == "beta")
        {
            buildConfirmHtml(response, "Rebooting to update Nuki Hub and Nuki Hub updater<br/>Updating to latest BETA version", 2);
            _preferences->putString(preference_ota_updater_url, GITHUB_BETA_UPDATER_BINARY_URL);
            _preferences->putString(preference_ota_main_url, GITHUB_BETA_RELEASE_BINARY_URL);
        }
        else if(key == "master")
        {
            buildConfirmHtml(response, "Rebooting to update Nuki Hub and Nuki Hub updater<br/>Updating to latest development version", 2);
            _preferences->putString(preference_ota_updater_url, GITHUB_MASTER_UPDATER_BINARY_URL);
            _preferences->putString(preference_ota_main_url, GITHUB_MASTER_RELEASE_BINARY_URL);
        }
        else
        {
            buildConfirmHtml(response, "Rebooting to update Nuki Hub and Nuki Hub updater<br/>Updating to latest RELEASE version", 2);
            _preferences->putString(preference_ota_updater_url, GITHUB_LATEST_UPDATER_BINARY_URL);
            _preferences->putString(preference_ota_main_url, GITHUB_LATEST_RELEASE_BINARY_URL);
        }
        _server.send(200, "text/html", response);
        waitAndProcess(true, 1000);
        restartEsp(RestartReason::OTAReboot);
    });
    _server.on("/uploadota", HTTP_POST, [&]() {
        if (_hasCredentials && !_server.authenticate(_credUser, _credPassword)) {
            return _server.requestAuthentication();
        }

        if (_ota.updateStarted() && _ota.updateCompleted()) {
            String response = "";
            buildOtaCompletedHtml(response);
            _server.send(200, "text/html", response);
            delay(2000);
            restartEsp(RestartReason::OTACompleted);
        } else {
            _ota.restart();
            _server.sendHeader("Location", "/ota?errored=true");
            _server.send(302, "text/plain", "");
        }
    }, [&]() {
        if (_hasCredentials && !_server.authenticate(_credUser, _credPassword)) {
            return _server.requestAuthentication();
        }

        handleOtaUpload();
    });

    const char *headerkeys[] = {"Content-Length"};
    size_t headerkeyssize = sizeof(headerkeys) / sizeof(char *);
    _server.collectHeaders(headerkeys, headerkeyssize);

    _server.begin();

    _network->setKeepAliveCallback([&]()
        {
            update();
        });
}

void WebCfgServer::update()
{
    if(_otaStartTs > 0 && ((esp_timer_get_time() / 1000) - _otaStartTs) > 120000)
    {
        Log->println(F("OTA time out, restarting"));
        delay(200);
        restartEsp(RestartReason::OTATimeout);
    }

    if(!_enabled) return;

    _server.handleClient();
}

void WebCfgServer::buildOtaHtml(String &response, bool errored)
{
    buildHtmlHeader(response);

    if(errored) response.concat("<div>Over-the-air update errored. Please check the logs for more info</div><br/>");

    if(_partitionType == 0)
    {
        response.concat("<h4 class=\"warning\">You are currently running Nuki Hub with an outdated partition scheme. Because of this you cannot use OTA to update to 9.00 or higher. Please check GitHub for instructions on how to update to 9.00 and the new partition scheme</h4>");
        response.concat("<button title=\"Open latest release on GitHub\" onclick=\" window.open('");
        response.concat(GITHUB_LATEST_RELEASE_URL);
        response.concat("', '_blank'); return false;\">Open latest release on GitHub</button>");
        return;
    }

    response.concat("<div id=\"msgdiv\" style=\"visibility:hidden\">Initiating Over-the-air update. This will take about two minutes, please be patient.<br>You will be forwarded automatically when the update is complete.</div>");

    response.concat("<div id=\"autoupdform\"><h4>Update Nuki Hub</h4>");
    response.concat("Click on the button to reboot and automatically update Nuki Hub and the Nuki Hub updater to the latest versions from GitHub");
    response.concat("<div style=\"clear: both\"></div>");
    response.concat("<form onsubmit=\"return confirm('Do you really want to update to the latest release?');\" action=\"/autoupdate\" method=\"get\" style=\"float: left; margin-right: 10px\"><br><input type=\"submit\" style=\"background: green\" value=\"Update to latest release\"></form>");
    response.concat("<form onsubmit=\"return confirm('Do you really want to update to the latest beta? This version could contain breaking bugs and necessitate downgrading to the latest release version using USB/Serial');\" action=\"/autoupdate\" method=\"get\" style=\"float: left; margin-right: 10px\"><input type=\"hidden\" name=\"beta\" value=\"1\" /><br><input type=\"submit\" style=\"color: black; background: yellow\"  value=\"Update to latest beta\"></form>");
    response.concat("<form onsubmit=\"return confirm('Do you really want to update to the latest development version? This version could contain breaking bugs and necessitate downgrading to the latest release version using USB/Serial');\" action=\"/autoupdate\" method=\"get\" style=\"float: left; margin-right: 10px\"><input type=\"hidden\" name=\"master\" value=\"1\" /><br><input type=\"submit\" style=\"background: red\"  value=\"Update to latest development version\"></form>");
    response.concat("<div style=\"clear: both\"></div><br>");

    response.concat("<b>Current version: </b>");
    response.concat(NUKI_HUB_VERSION);
    response.concat(" (");
    response.concat(NUKI_HUB_BUILD);
    response.concat("), ");
    response.concat(NUKI_HUB_DATE);
    response.concat("<br>");

    #ifndef NUKI_HUB_UPDATER
    NetworkClientSecure *client = new NetworkClientSecure;
    if (client) {
        client->setDefaultCACertBundle();
        {
            HTTPClient https;
            https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
            https.setTimeout(2500);
            https.useHTTP10(true);

            if (https.begin(*client, GITHUB_OTA_MANIFEST_URL)) {
                int httpResponseCode = https.GET();

                if (httpResponseCode == HTTP_CODE_OK || httpResponseCode == HTTP_CODE_MOVED_PERMANENTLY)
                {
                    JsonDocument doc;
                    DeserializationError jsonError = deserializeJson(doc, https.getStream());

                    if (!jsonError)
                    {
                        response.concat("<b>Latest release version: </b>");
                        response.concat(doc["release"]["fullversion"].as<const char*>());
                        response.concat(" (");
                        response.concat(doc["release"]["build"].as<const char*>());
                        response.concat("), ");
                        response.concat(doc["release"]["time"].as<const char*>());
                        response.concat("<br>");
                        response.concat("<b>Latest beta version: </b>");
                        response.concat(doc["beta"]["fullversion"].as<const char*>());
                        if(doc["beta"]["fullversion"] != "No beta available")
                        {
                            response.concat(" (");
                            response.concat(doc["beta"]["build"].as<const char*>());
                            response.concat("), ");
                            response.concat(doc["beta"]["time"].as<const char*>());
                        }
                        response.concat("<br>");
                        response.concat("<b>Latest development version: </b>");
                        response.concat(doc["master"]["fullversion"].as<const char*>());
                        response.concat(" (");
                        response.concat(doc["master"]["build"].as<const char*>());
                        response.concat("), ");
                        response.concat(doc["master"]["time"].as<const char*>());
                        response.concat("<br>");
                    }
                }
                https.end();
            }
        }
        delete client;
    }
    #endif
    response.concat("<br></div>");

    if(_partitionType == 1)
    {
        response.concat("<h4><a onclick=\"hideshowmanual();\">Manually update Nuki Hub</a></h4><div id=\"manualupdate\" style=\"display: none\">");
        response.concat("<div id=\"rebootform\"><h4>Reboot to Nuki Hub Updater</h4>");
        response.concat("Click on the button to reboot to the Nuki Hub updater, where you can select the latest Nuki Hub binary to update");
        response.concat("<form action=\"/reboottoota\" method=\"get\"><br><input type=\"submit\" value=\"Reboot to Nuki Hub Updater\" /></form><br><br></div>");
        response.concat("<div id=\"upform\"><h4>Update Nuki Hub Updater</h4>");
        response.concat("Select the latest Nuki Hub updater binary to update the Nuki Hub updater");
        response.concat("<form enctype=\"multipart/form-data\" action=\"/uploadota\" method=\"post\">Choose the nuki_hub_updater.bin file to upload: <input name=\"uploadedfile\" type=\"file\" accept=\".bin\" /><br/>");
    }
    else
    {
        response.concat("<div id=\"manualupdate\">");
        response.concat("<div id=\"rebootform\"><h4>Reboot to Nuki Hub</h4>");
        response.concat("Click on the button to reboot to Nuki Hub");
        response.concat("<form action=\"/reboottoota\" method=\"get\"><br><input type=\"submit\" value=\"Reboot to Nuki Hub\" /></form><br><br></div>");
        response.concat("<div id=\"upform\"><h4>Update Nuki Hub</h4>");
        response.concat("Select the latest Nuki Hub binary to update Nuki Hub");
        response.concat("<form enctype=\"multipart/form-data\" action=\"/uploadota\" method=\"post\">Choose the nuki_hub.bin file to upload: <input name=\"uploadedfile\" type=\"file\" accept=\".bin\" /><br/>");
    }
    response.concat("<br><input id=\"submitbtn\" type=\"submit\" value=\"Upload File\" /></form><br><br></div>");
    response.concat("<div id=\"gitdiv\">");
    response.concat("<h4>GitHub</h4><br>");
    response.concat("<button title=\"Open latest release on GitHub\" onclick=\" window.open('");
    response.concat(GITHUB_LATEST_RELEASE_URL);
    response.concat("', '_blank'); return false;\">Open latest release on GitHub</button>");
    response.concat("<br><br><button title=\"Download latest binary from GitHub\" onclick=\" window.open('");
    response.concat(GITHUB_LATEST_RELEASE_BINARY_URL);
    response.concat("'); return false;\">Download latest binary from GitHub</button>");
    response.concat("<br><br><button title=\"Download latest updater binary from GitHub\" onclick=\" window.open('");
    response.concat(GITHUB_LATEST_UPDATER_BINARY_URL);
    response.concat("'); return false;\">Download latest updater binary from GitHub</button></div></div>");
    response.concat("<script type=\"text/javascript\">");
    response.concat("window.addEventListener('load', function () {");
    response.concat("	var button = document.getElementById(\"submitbtn\");");
    response.concat("	button.addEventListener('click',hideshow,false);");
    response.concat("	function hideshow() {");
    response.concat("		document.getElementById('autoupdform').style.visibility = 'hidden';");
    response.concat("		document.getElementById('rebootform').style.visibility = 'hidden';");
    response.concat("		document.getElementById('upform').style.visibility = 'hidden';");
    response.concat("		document.getElementById('gitdiv').style.visibility = 'hidden';");
    response.concat("		document.getElementById('msgdiv').style.visibility = 'visible';");
    response.concat("	}");
    response.concat("});");
    response.concat("function hideshowmanual() {");
    response.concat("	var x = document.getElementById(\"manualupdate\");");
    response.concat("	if (x.style.display === \"none\") {");
    response.concat("	    x.style.display = \"block\";");
    response.concat("	} else {");
    response.concat("	    x.style.display = \"none\";");
    response.concat("    }");
    response.concat("}");
    response.concat("</script>");
    response.concat("</body></html>");
}

void WebCfgServer::buildOtaCompletedHtml(String &response)
{
    buildHtmlHeader(response);

    response.concat("<div>Over-the-air update completed.<br>You will be forwarded automatically.</div>");
    response.concat("<script type=\"text/javascript\">");
    response.concat("window.addEventListener('load', function () {");
    response.concat("   setTimeout(\"location.href = '/';\",10000);");
    response.concat("});");
    response.concat("</script>");
    response.concat("</body></html>");
}

void WebCfgServer::buildHtmlHeader(String &response, String additionalHeader)
{
    response.concat("<html><head>");
    response.concat("<meta name='viewport' content='width=device-width, initial-scale=1'>");
    if(strcmp(additionalHeader.c_str(), "") != 0) response.concat(additionalHeader);
    response.concat("<link rel='stylesheet' href='/style.css'>");
    response.concat("<title>Nuki Hub</title></head><body>");

    srand(esp_timer_get_time() / 1000);
}

void WebCfgServer::waitAndProcess(const bool blocking, const uint32_t duration)
{
    int64_t timeout = esp_timer_get_time() + (duration * 1000);
    while(esp_timer_get_time() < timeout)
    {
        _server.handleClient();
        if(blocking)
        {
            delay(10);
        }
        else
        {
            vTaskDelay( 50 / portTICK_PERIOD_MS);
        }
    }
}

void WebCfgServer::handleOtaUpload()
{
    if (_server.uri() != "/uploadota")
    {
        return;
    }

    HTTPUpload& upload = _server.upload();

    if(upload.filename == "")
    {
        Log->println("Invalid file for OTA upload");
        return;
    }

    if(_partitionType == 1 && _server.header("Content-Length").toInt() > 1600000)
    {
        if(upload.totalSize < 2000) Log->println("Uploaded OTA file too large, are you trying to upload a Nuki Hub binary instead of a Nuki Hub updater binary?");
        return;
    }
    else if(_partitionType == 2 && _server.header("Content-Length").toInt() < 1600000)
    {
        if(upload.totalSize < 2000) Log->println("Uploaded OTA file is too small, are you trying to upload a Nuki Hub updater binary instead of a Nuki Hub binary?");
        return;
    }

    if (upload.status == UPLOAD_FILE_START)
    {
        String filename = upload.filename;
        if (!filename.startsWith("/"))
        {
            filename = "/" + filename;
        }
        _otaStartTs = esp_timer_get_time() / 1000;
        esp_task_wdt_config_t twdt_config = {
            .timeout_ms = 30000,
            .idle_core_mask = 0,
            .trigger_panic = false,
        };
        esp_task_wdt_reconfigure(&twdt_config);

        #ifndef NUKI_HUB_UPDATER
        _network->disableAutoRestarts();
        _network->disableMqtt();
        if(_nuki != nullptr)
        {
            _nuki->disableWatchdog();
        }
        if(_nukiOpener != nullptr)
        {
            _nukiOpener->disableWatchdog();
        }
        #endif
        Log->print("handleFileUpload Name: "); Log->println(filename);
    }
    else if (upload.status == UPLOAD_FILE_WRITE)
    {
        _transferredSize = _transferredSize + upload.currentSize;
        Log->println(_transferredSize);
        _ota.updateFirmware(upload.buf, upload.currentSize);
    } else if (upload.status == UPLOAD_FILE_END)
    {
        Log->println();
        Log->print("handleFileUpload Size: "); Log->println(upload.totalSize);
    }
    else if(upload.status == UPLOAD_FILE_ABORTED)
    {
        Log->println();
        Log->println("OTA aborted, restarting ESP.");
        restartEsp(RestartReason::OTAAborted);
    }
    else
    {
        Log->println();
        Log->print("OTA unknown state: ");
        Log->println((int)upload.status);
        restartEsp(RestartReason::OTAUnknownState);
    }
}

void WebCfgServer::buildConfirmHtml(String &response, const String &message, uint32_t redirectDelay)
{
    String delay(redirectDelay);
    String header = "<meta http-equiv=\"Refresh\" content=\"" + delay + "; url=/\" />";

    buildHtmlHeader(response, header);
    response.concat(message);
    response.concat("</body></html>");
}

void WebCfgServer::sendCss()
{
    // escaped by https://www.cescaper.com/
    _server.sendHeader("Cache-Control", "public, max-age=3600");
    _server.send(200, "text/css", stylecss, sizeof(stylecss));
}

void WebCfgServer::sendFavicon()
{
    _server.sendHeader("Cache-Control", "public, max-age=604800");
    _server.send(200, "image/png", (const char*)favicon_32x32, sizeof(favicon_32x32));
}

#ifndef NUKI_HUB_UPDATER
void WebCfgServer::sendSettings()
{
    bool redacted = false;
    bool pairing = false;
    String key = _server.argName(0);
    String value = _server.arg(0);

    if(key == "redacted" && value == "1")
    {
        redacted = true;
    }

    String key2 = _server.argName(1);
    String value2 = _server.arg(1);

    if(key2 == "pairing" && value2 == "1")
    {
        pairing = true;
    }

    JsonDocument json;
    String jsonPretty;

    DebugPreferences debugPreferences;

    const std::vector<char*> keysPrefs = debugPreferences.getPreferencesKeys();
    const std::vector<char*> boolPrefs = debugPreferences.getPreferencesBoolKeys();
    const std::vector<char*> redactedPrefs = debugPreferences.getPreferencesRedactedKeys();
    const std::vector<char*> bytePrefs = debugPreferences.getPreferencesByteKeys();
    const std::vector<char*> charPrefs = debugPreferences.getPreferencesCharKeys();

    for(const auto& key : keysPrefs)
    {
        if(strcmp(key, preference_show_secrets) == 0) continue;
        if(strcmp(key, preference_latest_version) == 0) continue;
        if(strcmp(key, preference_has_mac_saved) == 0) continue;
        if(strcmp(key, preference_device_id_lock) == 0) continue;
        if(strcmp(key, preference_device_id_opener) == 0) continue;
        if(!redacted) if(std::find(redactedPrefs.begin(), redactedPrefs.end(), key) != redactedPrefs.end()) continue;
        if(std::find(charPrefs.begin(), charPrefs.end(), key) != charPrefs.end()) continue;
        if(!_preferences->isKey(key)) json[key] = "";
        else if(std::find(boolPrefs.begin(), boolPrefs.end(), key) != boolPrefs.end()) json[key] = _preferences->getBool(key) ? "1" : "0";
        else
        {
            switch(_preferences->getType(key))
            {
                case PT_I8:
                    json[key] = String(_preferences->getChar(key));
                    break;
                case PT_I16:
                    json[key] = String(_preferences->getShort(key));
                    break;
                case PT_I32:
                    json[key] = String(_preferences->getInt(key));
                    break;
                case PT_I64:
                    json[key] = String(_preferences->getLong64(key));
                    break;
                case PT_U8:
                    json[key] = String(_preferences->getUChar(key));
                    break;
                case PT_U16:
                    json[key] = String(_preferences->getUShort(key));
                    break;
                case PT_U32:
                    json[key] = String(_preferences->getUInt(key));
                    break;
                case PT_U64:
                    json[key] = String(_preferences->getULong64(key));
                    break;
                case PT_STR:
                    json[key] = _preferences->getString(key);
                    break;
                default:
                    json[key] = _preferences->getString(key);
                    break;
            }
        }
    }

    if(pairing)
    {
        if(_nuki != nullptr)
        {
            unsigned char currentBleAddress[6];
            unsigned char authorizationId[4] = {0x00};
            unsigned char secretKeyK[32] = {0x00};
            uint16_t storedPincode = 0000;
            Preferences nukiBlePref;
            nukiBlePref.begin("NukiHub", false);
            nukiBlePref.getBytes("bleAddress", currentBleAddress, 6);
            nukiBlePref.getBytes("secretKeyK", secretKeyK, 32);
            nukiBlePref.getBytes("authorizationId", authorizationId, 4);
            nukiBlePref.getBytes("securityPinCode", &storedPincode, 2);
            nukiBlePref.end();
            char text[255];
            text[0] = '\0';
            for(int i = 0 ; i < 6 ; i++) {
                size_t offset = strlen(text);
                sprintf(&(text[offset]), "%02x", currentBleAddress[i]);
            }
            json["bleAddressLock"] = text;
            memset(text, 0, sizeof(text));
            text[0] = '\0';
            for(int i = 0 ; i < 32 ; i++) {
                size_t offset = strlen(text);
                sprintf(&(text[offset]), "%02x", secretKeyK[i]);
            }
            json["secretKeyKLock"] = text;
            memset(text, 0, sizeof(text));
            text[0] = '\0';
            for(int i = 0 ; i < 4 ; i++) {
                size_t offset = strlen(text);
                sprintf(&(text[offset]), "%02x", authorizationId[i]);
            }
            json["authorizationIdLock"] = text;
            memset(text, 0, sizeof(text));
            json["securityPinCodeLock"] = storedPincode;
        }
        if(_nukiOpener != nullptr)
        {
            unsigned char currentBleAddressOpn[6];
            unsigned char authorizationIdOpn[4] = {0x00};
            unsigned char secretKeyKOpn[32] = {0x00};
            uint16_t storedPincodeOpn = 0000;
            Preferences nukiBlePref;
            nukiBlePref.begin("NukiHubopener", false);
            nukiBlePref.getBytes("bleAddress", currentBleAddressOpn, 6);
            nukiBlePref.getBytes("secretKeyK", secretKeyKOpn, 32);
            nukiBlePref.getBytes("authorizationId", authorizationIdOpn, 4);
            nukiBlePref.getBytes("securityPinCode", &storedPincodeOpn, 2);
            nukiBlePref.end();
            char text[255];
            text[0] = '\0';
            for(int i = 0 ; i < 6 ; i++) {
                size_t offset = strlen(text);
                sprintf(&(text[offset]), "%02x", currentBleAddressOpn[i]);
            }
            json["bleAddressOpener"] = text;
            memset(text, 0, sizeof(text));
            text[0] = '\0';
            for(int i = 0 ; i < 32 ; i++) {
                size_t offset = strlen(text);
                sprintf(&(text[offset]), "%02x", secretKeyKOpn[i]);
            }
            json["secretKeyKOpener"] = text;
            memset(text, 0, sizeof(text));
            text[0] = '\0';
            for(int i = 0 ; i < 4 ; i++) {
                size_t offset = strlen(text);
                sprintf(&(text[offset]), "%02x", authorizationIdOpn[i]);
            }
            json["authorizationIdOpener"] = text;
            memset(text, 0, sizeof(text));
            json["securityPinCodeOpener"] = storedPincodeOpn;
        }
    }

    for(const auto& key : bytePrefs)
    {
        size_t storedLength = _preferences->getBytesLength(key);
        if(storedLength == 0) continue;
        uint8_t serialized[storedLength];
        memset(serialized, 0, sizeof(serialized));
        size_t size = _preferences->getBytes(key, serialized, sizeof(serialized));
        if(size == 0) continue;
        char text[255];
        text[0] = '\0';
        for(int i = 0 ; i < size ; i++) {
            size_t offset = strlen(text);
            sprintf(&(text[offset]), "%02x", serialized[i]);
        }
        json[key] = text;
        memset(text, 0, sizeof(text));
    }

    serializeJsonPretty(json, jsonPretty);

    _server.sendHeader("Content-Disposition", "attachment; filename=nuki_hub.json");
    _server.send(200, "application/json", jsonPretty);
}

bool WebCfgServer::processArgs(String& message)
{
    bool configChanged = false;
    bool aclLvlChanged = false;
    bool clearMqttCredentials = false;
    bool clearCredentials = false;
    bool manPairLck = false;
    bool manPairOpn = false;
    unsigned char currentBleAddress[6];
    unsigned char authorizationId[4] = {0x00};
    unsigned char secretKeyK[32] = {0x00};
    unsigned char pincode[2] = {0x00};
    unsigned char currentBleAddressOpn[6];
    unsigned char authorizationIdOpn[4] = {0x00};
    unsigned char secretKeyKOpn[32] = {0x00};

    uint32_t aclPrefs[17] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    uint32_t basicLockConfigAclPrefs[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    uint32_t basicOpenerConfigAclPrefs[14] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    uint32_t advancedLockConfigAclPrefs[22] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    uint32_t advancedOpenerConfigAclPrefs[20] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    int count = _server.args();

    String pass1 = "";
    String pass2 = "";

    for(int index = 0; index < count; index++)
    {
        String key = _server.argName(index);
        String value = _server.arg(index);

        if(key == "MQTTSERVER")
        {
            _preferences->putString(preference_mqtt_broker, value);
            configChanged = true;
        }
        else if(key == "MQTTPORT")
        {
            _preferences->putInt(preference_mqtt_broker_port, value.toInt());
            configChanged = true;
        }
        else if(key == "MQTTUSER")
        {
            if(value == "#")
            {
                clearMqttCredentials = true;
            }
            else
            {
                _preferences->putString(preference_mqtt_user, value);
                configChanged = true;
            }
        }
        else if(key == "MQTTPASS")
        {
            if(value != "*")
            {
                _preferences->putString(preference_mqtt_password, value);
                configChanged = true;
            }
        }
        else if(key == "MQTTPATH")
        {
            _preferences->putString(preference_mqtt_lock_path, value);
            configChanged = true;
        }
        else if(key == "MQTTOPPATH")
        {
            _preferences->putString(preference_mqtt_opener_path, value);
            configChanged = true;
        }
        else if(key == "MQTTCA")
        {
            _preferences->putString(preference_mqtt_ca, value);
            configChanged = true;
        }
        else if(key == "MQTTCRT")
        {
            _preferences->putString(preference_mqtt_crt, value);
            configChanged = true;
        }
        else if(key == "MQTTKEY")
        {
            _preferences->putString(preference_mqtt_key, value);
            configChanged = true;
        }
        else if(key == "NWHW")
        {
            _preferences->putInt(preference_network_hardware, value.toInt());
            configChanged = true;
        }
        else if(key == "NWHWWIFIFB")
        {
            _preferences->putBool(preference_network_wifi_fallback_disabled, (value == "1"));
            configChanged = true;
        }
        else if(key == "RSSI")
        {
            _preferences->putInt(preference_rssi_publish_interval, value.toInt());
            configChanged = true;
        }
        else if(key == "HASSDISCOVERY")
        {
            if(_preferences->getString(preference_mqtt_hass_discovery) != value)
            {
                // Previous HASS config has to be disabled first (remove retained MQTT messages)
                if (_nuki != nullptr)
                {
                    _nuki->disableHASS();
                }
                if (_nukiOpener != nullptr)
                {
                    _nukiOpener->disableHASS();
                }
                _preferences->putString(preference_mqtt_hass_discovery, value);
                configChanged = true;
            }
        }
        else if(key == "OPENERCONT")
        {
            _preferences->putBool(preference_opener_continuous_mode, (value == "1"));
            configChanged = true;
        }
        else if(key == "HASSCUURL")
        {
            _preferences->putString(preference_mqtt_hass_cu_url, value);
            configChanged = true;
        }
        else if(key == "BESTRSSI")
        {
            _preferences->putBool(preference_find_best_rssi, (value == "1"));
            configChanged = true;
        }
        else if(key == "HOSTNAME")
        {
            _preferences->putString(preference_hostname, value);
            configChanged = true;
        }
        else if(key == "NETTIMEOUT")
        {
            _preferences->putInt(preference_network_timeout, value.toInt());
            configChanged = true;
        }
        else if(key == "RSTDISC")
        {
            _preferences->putBool(preference_restart_on_disconnect, (value == "1"));
            configChanged = true;
        }
        else if(key == "RECNWTMQTTDIS")
        {
            _preferences->putBool(preference_recon_netw_on_mqtt_discon, (value == "1"));
            configChanged = true;
        }
        else if(key == "MQTTLOG")
        {
            _preferences->putBool(preference_mqtt_log_enabled, (value == "1"));
            configChanged = true;
        }
        else if(key == "WEBLOG")
        {
            _preferences->putBool(preference_webserial_enabled, (value == "1"));
            configChanged = true;
        }
        else if(key == "CHECKUPDATE")
        {
            _preferences->putBool(preference_check_updates, (value == "1"));
            configChanged = true;
        }
        else if(key == "UPDATEMQTT")
        {
            _preferences->putBool(preference_update_from_mqtt, (value == "1"));
            configChanged = true;
        }
        else if(key == "OFFHYBRID")
        {
            _preferences->putBool(preference_official_hybrid, (value == "1"));
            if((value == "1")) _preferences->putBool(preference_register_as_app, true);
            configChanged = true;
        }
        else if(key == "HYBRIDACT")
        {
            _preferences->putBool(preference_official_hybrid_actions, (value == "1"));
            if(value == "1") _preferences->putBool(preference_register_as_app, true);
            configChanged = true;
        }
        else if(key == "HYBRIDTIMER")
        {
            _preferences->putInt(preference_query_interval_hybrid_lockstate, value.toInt());
            configChanged = true;
        }
        else if(key == "HYBRIDRETRY")
        {
            _preferences->putBool(preference_official_hybrid_retry, (value == "1"));
            configChanged = true;
        }
        else if(key == "DISNONJSON")
        {
            _preferences->putBool(preference_disable_non_json, (value == "1"));
            configChanged = true;
        }
        else if(key == "DHCPENA")
        {
            _preferences->putBool(preference_ip_dhcp_enabled, (value == "1"));
            configChanged = true;
        }
        else if(key == "IPADDR")
        {
            _preferences->putString(preference_ip_address, value);
            configChanged = true;
        }
        else if(key == "IPSUB")
        {
            _preferences->putString(preference_ip_subnet, value);
            configChanged = true;
        }
        else if(key == "IPGTW")
        {
            _preferences->putString(preference_ip_gateway, value);
            configChanged = true;
        }
        else if(key == "DNSSRV")
        {
            _preferences->putString(preference_ip_dns_server, value);
            configChanged = true;
        }
        else if(key == "LSTINT")
        {
            _preferences->putInt(preference_query_interval_lockstate, value.toInt());
            configChanged = true;
        }
        else if(key == "CFGINT")
        {
            _preferences->putInt(preference_query_interval_configuration, value.toInt());
            configChanged = true;
        }
        else if(key == "BATINT")
        {
            _preferences->putInt(preference_query_interval_battery, value.toInt());
            configChanged = true;
        }
        else if(key == "KPINT")
        {
            _preferences->putInt(preference_query_interval_keypad, value.toInt());
            configChanged = true;
        }
        else if(key == "NRTRY")
        {
            _preferences->putInt(preference_command_nr_of_retries, value.toInt());
            configChanged = true;
        }
        else if(key == "TRYDLY")
        {
            _preferences->putInt(preference_command_retry_delay, value.toInt());
            configChanged = true;
        }
        else if(key == "TXPWR")
        {
            if(value.toInt() >= -12 && value.toInt() <= 9)
            {
                _preferences->putInt(preference_ble_tx_power, value.toInt());
                configChanged = true;
            }
        }
#if PRESENCE_DETECTION_ENABLED
        else if(key == "PRDTMO")
        {
            _preferences->putInt(preference_presence_detection_timeout, value.toInt());
            configChanged = true;
        }
#endif
        else if(key == "RSBC")
        {
            _preferences->putInt(preference_restart_ble_beacon_lost, value.toInt());
            configChanged = true;
        }
        else if(key == "TSKNTWK")
        {
            if(value.toInt() > 12287 && value.toInt() < 32769)
            {
            _preferences->putInt(preference_task_size_network, value.toInt());
            configChanged = true;
            }
        }
        else if(key == "TSKNUKI")
        {
            if(value.toInt() > 8191 && value.toInt() < 32769)
            {
                _preferences->putInt(preference_task_size_nuki, value.toInt());
                configChanged = true;
            }
        }
        else if(key == "ALMAX")
        {
            if(value.toInt() > 0 && value.toInt() < 51)
            {
                _preferences->putInt(preference_authlog_max_entries, value.toInt());
                configChanged = true;
            }
        }
        else if(key == "KPMAX")
        {
            if(value.toInt() > 0 && value.toInt() < 101)
            {
                _preferences->putInt(preference_keypad_max_entries, value.toInt());
                configChanged = true;
            }
        }
        else if(key == "TCMAX")
        {
            if(value.toInt() > 0 && value.toInt() < 51)
            {
                _preferences->putInt(preference_timecontrol_max_entries, value.toInt());
                configChanged = true;
            }
        }
        else if(key == "BUFFSIZE")
        {
            if(value.toInt() > 4095 && value.toInt() < 32769)
            {
                _preferences->putInt(preference_buffer_size, value.toInt());
                configChanged = true;
            }
        }
        else if(key == "BTLPRST")
        {
            _preferences->putBool(preference_enable_bootloop_reset, (value == "1"));
            configChanged = true;
        }
        else if(key == "OTAUPD")
        {
            _preferences->putString(preference_ota_updater_url, value);
            configChanged = true;
        }
        else if(key == "OTAMAIN")
        {
            _preferences->putString(preference_ota_main_url, value);
            configChanged = true;
        }
        else if(key == "SHOWSECRETS")
        {
            _preferences->putBool(preference_show_secrets, (value == "1"));
            configChanged = true;
        }
        else if(key == "ACLLVLCHANGED")
        {
            aclLvlChanged = true;
        }
        else if(key == "CONFPUB")
        {
            _preferences->putBool(preference_conf_info_enabled, (value == "1"));
            configChanged = true;
        }
        else if(key == "KPPUB")
        {
            _preferences->putBool(preference_keypad_info_enabled, (value == "1"));
            configChanged = true;
        }
        else if(key == "KPCODE")
        {
            _preferences->putBool(preference_keypad_publish_code, (value == "1"));
            configChanged = true;
        }
        else if(key == "KPENA")
        {
            _preferences->putBool(preference_keypad_control_enabled, (value == "1"));
            configChanged = true;
        }
        else if(key == "TCPUB")
        {
            _preferences->putBool(preference_timecontrol_info_enabled, (value == "1"));
            configChanged = true;
        }
        else if(key == "KPPER")
        {
            _preferences->putBool(preference_keypad_topic_per_entry, (value == "1"));
            configChanged = true;
        }
        else if(key == "TCPER")
        {
            _preferences->putBool(preference_timecontrol_topic_per_entry, (value == "1"));
            configChanged = true;
        }
        else if(key == "TCENA")
        {
            _preferences->putBool(preference_timecontrol_control_enabled, (value == "1"));
            configChanged = true;
        }
        else if(key == "PUBAUTH")
        {
            _preferences->putBool(preference_publish_authdata, (value == "1"));
            configChanged = true;
        }
        else if(key == "ACLLCKLCK")
        {
            aclPrefs[0] = ((value == "1") ? 1 : 0);
        }
        else if(key == "ACLLCKUNLCK")
        {
            aclPrefs[1] = ((value == "1") ? 1 : 0);
        }
        else if(key == "ACLLCKUNLTCH")
        {
            aclPrefs[2] = ((value == "1") ? 1 : 0);
        }
        else if(key == "ACLLCKLNG")
        {
            aclPrefs[3] = ((value == "1") ? 1 : 0);
        }
        else if(key == "ACLLCKLNGU")
        {
            aclPrefs[4] = ((value == "1") ? 1 : 0);
        }
        else if(key == "ACLLCKFLLCK")
        {
            aclPrefs[5] = ((value == "1") ? 1 : 0);
        }
        else if(key == "ACLLCKFOB1")
        {
            aclPrefs[6] = ((value == "1") ? 1 : 0);
        }
        else if(key == "ACLLCKFOB2")
        {
            aclPrefs[7] = ((value == "1") ? 1 : 0);
        }
        else if(key == "ACLLCKFOB3")
        {
            aclPrefs[8] = ((value == "1") ? 1 : 0);
        }
        else if(key == "ACLOPNUNLCK")
        {
            aclPrefs[9] = ((value == "1") ? 1 : 0);
        }
        else if(key == "ACLOPNLCK")
        {
            aclPrefs[10] = ((value == "1") ? 1 : 0);
        }
        else if(key == "ACLOPNUNLTCH")
        {
            aclPrefs[11] = ((value == "1") ? 1 : 0);
        }
        else if(key == "ACLOPNUNLCKCM")
        {
            aclPrefs[12] = ((value == "1") ? 1 : 0);
        }
        else if(key == "ACLOPNLCKCM")
        {
            aclPrefs[13] = ((value == "1") ? 1 : 0);
        }
        else if(key == "ACLOPNFOB1")
        {
            aclPrefs[14] = ((value == "1") ? 1 : 0);
        }
        else if(key == "ACLOPNFOB2")
        {
            aclPrefs[15] = ((value == "1") ? 1 : 0);
        }
        else if(key == "ACLOPNFOB3")
        {
            aclPrefs[16] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFLCKNAME")
        {
            basicLockConfigAclPrefs[0] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFLCKLAT")
        {
            basicLockConfigAclPrefs[1] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFLCKLONG")
        {
            basicLockConfigAclPrefs[2] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFLCKAUNL")
        {
            basicLockConfigAclPrefs[3] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFLCKPRENA")
        {
            basicLockConfigAclPrefs[4] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFLCKBTENA")
        {
            basicLockConfigAclPrefs[5] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFLCKLEDENA")
        {
            basicLockConfigAclPrefs[6] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFLCKLEDBR")
        {
            basicLockConfigAclPrefs[7] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFLCKTZOFF")
        {
            basicLockConfigAclPrefs[8] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFLCKDSTM")
        {
            basicLockConfigAclPrefs[9] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFLCKFOB1")
        {
            basicLockConfigAclPrefs[10] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFLCKFOB2")
        {
            basicLockConfigAclPrefs[11] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFLCKFOB3")
        {
            basicLockConfigAclPrefs[12] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFLCKSGLLCK")
        {
            basicLockConfigAclPrefs[13] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFLCKADVM")
        {
            basicLockConfigAclPrefs[14] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFLCKTZID")
        {
            basicLockConfigAclPrefs[15] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFLCKUPOD")
        {
            advancedLockConfigAclPrefs[0] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFLCKLPOD")
        {
            advancedLockConfigAclPrefs[1] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFLCKSLPOD")
        {
            advancedLockConfigAclPrefs[2] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFLCKUTLTOD")
        {
            advancedLockConfigAclPrefs[3] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFLCKLNGT")
        {
            advancedLockConfigAclPrefs[4] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFLCKSBPA")
        {
            advancedLockConfigAclPrefs[5] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFLCKDBPA")
        {
            advancedLockConfigAclPrefs[6] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFLCKDC")
        {
            advancedLockConfigAclPrefs[7] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFLCKBATT")
        {
            advancedLockConfigAclPrefs[8] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFLCKABTD")
        {
            advancedLockConfigAclPrefs[9] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFLCKUNLD")
        {
            advancedLockConfigAclPrefs[10] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFLCKALT")
        {
            advancedLockConfigAclPrefs[11] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFLCKAUNLD")
        {
            advancedLockConfigAclPrefs[12] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFLCKNMENA")
        {
            advancedLockConfigAclPrefs[13] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFLCKNMST")
        {
            advancedLockConfigAclPrefs[14] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFLCKNMET")
        {
            advancedLockConfigAclPrefs[15] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFLCKNMALENA")
        {
            advancedLockConfigAclPrefs[16] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFLCKNMAULD")
        {
            advancedLockConfigAclPrefs[17] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFLCKNMLOS")
        {
            advancedLockConfigAclPrefs[18] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFLCKALENA")
        {
            advancedLockConfigAclPrefs[19] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFLCKIALENA")
        {
            advancedLockConfigAclPrefs[20] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFLCKAUENA")
        {
            advancedLockConfigAclPrefs[21] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFOPNNAME")
        {
            basicOpenerConfigAclPrefs[0] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFOPNLAT")
        {
            basicOpenerConfigAclPrefs[1] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFOPNLONG")
        {
            basicOpenerConfigAclPrefs[2] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFOPNPRENA")
        {
            basicOpenerConfigAclPrefs[3] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFOPNBTENA")
        {
            basicOpenerConfigAclPrefs[4] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFOPNLEDENA")
        {
            basicOpenerConfigAclPrefs[5] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFOPNTZOFF")
        {
            basicOpenerConfigAclPrefs[6] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFOPNDSTM")
        {
            basicOpenerConfigAclPrefs[7] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFOPNFOB1")
        {
            basicOpenerConfigAclPrefs[8] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFOPNFOB2")
        {
            basicOpenerConfigAclPrefs[9] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFOPNFOB3")
        {
            basicOpenerConfigAclPrefs[10] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFOPNOPM")
        {
            basicOpenerConfigAclPrefs[11] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFOPNADVM")
        {
            basicOpenerConfigAclPrefs[12] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFOPNTZID")
        {
            basicOpenerConfigAclPrefs[13] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFOPNICID")
        {
            advancedOpenerConfigAclPrefs[0] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFOPNBUSMS")
        {
            advancedOpenerConfigAclPrefs[1] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFOPNSCDUR")
        {
            advancedOpenerConfigAclPrefs[2] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFOPNESD")
        {
            advancedOpenerConfigAclPrefs[3] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFOPNRESD")
        {
            advancedOpenerConfigAclPrefs[4] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFOPNESDUR")
        {
            advancedOpenerConfigAclPrefs[5] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFOPNDRTOAR")
        {
            advancedOpenerConfigAclPrefs[6] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFOPNRTOT")
        {
            advancedOpenerConfigAclPrefs[7] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFOPNDRBSUP")
        {
            advancedOpenerConfigAclPrefs[8] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFOPNDRBSUPDUR")
        {
            advancedOpenerConfigAclPrefs[9] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFOPNSRING")
        {
            advancedOpenerConfigAclPrefs[10] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFOPNSOPN")
        {
            advancedOpenerConfigAclPrefs[11] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFOPNSRTO")
        {
            advancedOpenerConfigAclPrefs[12] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFOPNSCM")
        {
            advancedOpenerConfigAclPrefs[13] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFOPNSCFRM")
        {
            advancedOpenerConfigAclPrefs[14] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFOPNSLVL")
        {
            advancedOpenerConfigAclPrefs[15] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFOPNSBPA")
        {
            advancedOpenerConfigAclPrefs[16] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFOPNDBPA")
        {
            advancedOpenerConfigAclPrefs[17] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFOPNBATT")
        {
            advancedOpenerConfigAclPrefs[18] = ((value == "1") ? 1 : 0);
        }
        else if(key == "CONFOPNABTD")
        {
            advancedOpenerConfigAclPrefs[19] = ((value == "1") ? 1 : 0);
        }
        else if(key == "REGAPP")
        {
            _preferences->putBool(preference_register_as_app, (value == "1"));
            configChanged = true;
        }
        else if(key == "REGAPPOPN")
        {
            _preferences->putBool(preference_register_opener_as_app, (value == "1"));
            configChanged = true;
        }
        else if(key == "LOCKENA")
        {
            _preferences->putBool(preference_lock_enabled, (value == "1"));
            configChanged = true;
        }
        else if(key == "OPENA")
        {
            _preferences->putBool(preference_opener_enabled, (value == "1"));
            configChanged = true;
        }
        else if(key == "CREDUSER")
        {
            if(value == "#")
            {
                clearCredentials = true;
            }
            else
            {
                _preferences->putString(preference_cred_user, value);
                configChanged = true;
            }
        }
        else if(key == "CREDPASS")
        {
            pass1 = value;
        }
        else if(key == "CREDPASSRE")
        {
            pass2 = value;
        }
        else if(key == "NUKIPIN" && _nuki != nullptr)
        {
            if(value == "#")
            {
                message = "Nuki Lock PIN cleared";
                _nuki->setPin(0xffff);
            }
            else
            {
                message = "Nuki Lock PIN saved";
                _nuki->setPin(value.toInt());
            }
            configChanged = true;
        }
        else if(key == "NUKIOPPIN" && _nukiOpener != nullptr)
        {
            if(value == "#")
            {
                message = "Nuki Opener PIN cleared";
                _nukiOpener->setPin(0xffff);
            }
            else
            {
                message = "Nuki Opener PIN saved";
                _nukiOpener->setPin(value.toInt());
            }
            configChanged = true;
        }
        else if(key == "LCKMANPAIR" && (value == "1"))
        {
            manPairLck = true;
        }
        else if(key == "OPNMANPAIR" && (value == "1"))
        {
            manPairOpn = true;
        }
        else if(key == "LCKBLEADDR")
        {
            if(value.length() == 12) for(int i=0; i<value.length();i+=2) currentBleAddress[(i/2)] = std::stoi(value.substring(i, i+2).c_str(), nullptr, 16);
        }
        else if(key == "LCKSECRETK")
        {
            if(value.length() == 64) for(int i=0; i<value.length();i+=2) secretKeyK[(i/2)] = std::stoi(value.substring(i, i+2).c_str(), nullptr, 16);
        }
        else if(key == "LCKAUTHID")
        {
            if(value.length() == 8) for(int i=0; i<value.length();i+=2) authorizationId[(i/2)] = std::stoi(value.substring(i, i+2).c_str(), nullptr, 16);
        }
        else if(key == "OPNBLEADDR")
        {
            if(value.length() == 12) for(int i=0; i<value.length();i+=2) currentBleAddressOpn[(i/2)] = std::stoi(value.substring(i, i+2).c_str(), nullptr, 16);
        }
        else if(key == "OPNSECRETK")
        {
            if(value.length() == 64) for(int i=0; i<value.length();i+=2) secretKeyKOpn[(i/2)] = std::stoi(value.substring(i, i+2).c_str(), nullptr, 16);
        }
        else if(key == "OPNAUTHID")
        {
            if(value.length() == 8) for(int i=0; i<value.length();i+=2) authorizationIdOpn[(i/2)] = std::stoi(value.substring(i, i+2).c_str(), nullptr, 16);
        }
    }

    if(manPairLck)
    {
        Log->println(F("Changing lock pairing"));
        Preferences nukiBlePref;
        nukiBlePref.begin("NukiHub", false);
        nukiBlePref.putBytes("bleAddress", currentBleAddress, 6);
        nukiBlePref.putBytes("secretKeyK", secretKeyK, 32);
        nukiBlePref.putBytes("authorizationId", authorizationId, 4);
        nukiBlePref.putBytes("securityPinCode", pincode, 2);

        nukiBlePref.end();
    }

    if(manPairOpn)
    {
        Log->println(F("Changing opener pairing"));
        Preferences nukiBlePref;
        nukiBlePref.begin("NukiHubopener", false);
        nukiBlePref.putBytes("bleAddress", currentBleAddressOpn, 6);
        nukiBlePref.putBytes("secretKeyK", secretKeyKOpn, 32);
        nukiBlePref.putBytes("authorizationId", authorizationIdOpn, 4);
        nukiBlePref.putBytes("securityPinCode", pincode, 2);
        nukiBlePref.end();
    }

    if(pass1 != "" && pass1 == pass2)
    {
        _preferences->putString(preference_cred_password, pass1);
        configChanged = true;
    }

    if(clearMqttCredentials)
    {
        _preferences->putString(preference_mqtt_user, "");
        _preferences->putString(preference_mqtt_password, "");
        configChanged = true;
    }

    if(clearCredentials)
    {
        _preferences->putString(preference_cred_user, "");
        _preferences->putString(preference_cred_password, "");
        configChanged = true;
    }

    if(aclLvlChanged)
    {
        _preferences->putBytes(preference_acl, (byte*)(&aclPrefs), sizeof(aclPrefs));
        _preferences->putBytes(preference_conf_lock_basic_acl, (byte*)(&basicLockConfigAclPrefs), sizeof(basicLockConfigAclPrefs));
        _preferences->putBytes(preference_conf_opener_basic_acl, (byte*)(&basicOpenerConfigAclPrefs), sizeof(basicOpenerConfigAclPrefs));
        _preferences->putBytes(preference_conf_lock_advanced_acl, (byte*)(&advancedLockConfigAclPrefs), sizeof(advancedLockConfigAclPrefs));
        _preferences->putBytes(preference_conf_opener_advanced_acl, (byte*)(&advancedOpenerConfigAclPrefs), sizeof(advancedOpenerConfigAclPrefs));
        configChanged = true;
    }

    if(configChanged)
    {
        message = "Configuration saved ... restarting.";
        _enabled = false;
        _preferences->end();
    }

    return configChanged;
}

bool WebCfgServer::processImport(String& message)
{
    bool configChanged = false;
    unsigned char currentBleAddress[6];
    unsigned char authorizationId[4] = {0x00};
    unsigned char secretKeyK[32] = {0x00};
    unsigned char currentBleAddressOpn[6];
    unsigned char authorizationIdOpn[4] = {0x00};
    unsigned char secretKeyKOpn[32] = {0x00};

    int count = _server.args();

    for(int index = 0; index < count; index++)
    {
        String postKey = _server.argName(index);
        String postValue = _server.arg(index);

        if(postKey == "importjson")
        {
            JsonDocument doc;

            DeserializationError error = deserializeJson(doc, postValue);
            if (error)
            {
                Log->println("Invalid JSON for import");
                message = "Invalid JSON, config not changed";
                return configChanged;
            }

            DebugPreferences debugPreferences;

            const std::vector<char*> keysPrefs = debugPreferences.getPreferencesKeys();
            const std::vector<char*> boolPrefs = debugPreferences.getPreferencesBoolKeys();
            const std::vector<char*> bytePrefs = debugPreferences.getPreferencesByteKeys();
            const std::vector<char*> charPrefs = debugPreferences.getPreferencesCharKeys();
            const std::vector<char*> intPrefs = debugPreferences.getPreferencesIntKeys();

            for(const auto& key : keysPrefs)
            {
                if(doc[key].isNull()) continue;
                if(strcmp(key, preference_show_secrets) == 0) continue;
                if(strcmp(key, preference_latest_version) == 0) continue;
                if(strcmp(key, preference_has_mac_saved) == 0) continue;
                if(strcmp(key, preference_device_id_lock) == 0) continue;
                if(strcmp(key, preference_device_id_opener) == 0) continue;
                if(std::find(charPrefs.begin(), charPrefs.end(), key) != charPrefs.end()) continue;
                if(std::find(boolPrefs.begin(), boolPrefs.end(), key) != boolPrefs.end())
                {
                    if (doc[key].as<String>().length() > 0) _preferences->putBool(key, (doc[key].as<String>() == "1" ? true : false));
                    else _preferences->remove(key);
                    continue;
                }
                if(std::find(intPrefs.begin(), intPrefs.end(), key) != intPrefs.end())
                {
                    if (doc[key].as<String>().length() > 0) _preferences->putInt(key, doc[key].as<int>());
                    else _preferences->remove(key);
                    continue;
                }

                if (doc[key].as<String>().length() > 0) _preferences->putString(key, doc[key].as<String>());
                else _preferences->remove(key);
            }

            for(const auto& key : bytePrefs)
            {
                if(!doc[key].isNull() && doc[key].is<String>())
                {
                    String value = doc[key].as<String>();
                    unsigned char tmpchar[32];
                    for(int i=0; i<value.length();i+=2) tmpchar[(i/2)] = std::stoi(value.substring(i, i+2).c_str(), nullptr, 16);
                    _preferences->putBytes(key, (byte*)(&tmpchar), (value.length() / 2));
                    memset(tmpchar, 0, sizeof(tmpchar));
                }
            }

            Preferences nukiBlePref;
            nukiBlePref.begin("NukiHub", false);

            if(!doc["bleAddressLock"].isNull())
            {
                if (doc["bleAddressLock"].as<String>().length() == 12)
                {
                    String value = doc["bleAddressLock"].as<String>();
                    for(int i=0; i<value.length();i+=2) currentBleAddress[(i/2)] = std::stoi(value.substring(i, i+2).c_str(), nullptr, 16);
                    nukiBlePref.putBytes("bleAddress", currentBleAddress, 6);
                }
                else _preferences->remove("bleAddressLock");
            }
            if(!doc["secretKeyKLock"].isNull())
            {
                if (doc["secretKeyKLock"].as<String>().length() == 64)
                {
                    String value = doc["secretKeyKLock"].as<String>();
                    for(int i=0; i<value.length();i+=2) secretKeyK[(i/2)] = std::stoi(value.substring(i, i+2).c_str(), nullptr, 16);
                    nukiBlePref.putBytes("secretKeyK", secretKeyK, 32);
                }
                else _preferences->remove("secretKeyKLock");
            }
            if(!doc["authorizationIdLock"].isNull())
            {
                if (doc["authorizationIdLock"].as<String>().length() == 8)
                {
                    String value = doc["authorizationIdLock"].as<String>();
                    for(int i=0; i<value.length();i+=2) authorizationId[(i/2)] = std::stoi(value.substring(i, i+2).c_str(), nullptr, 16);
                    nukiBlePref.putBytes("authorizationId", authorizationId, 4);
                }
                else _preferences->remove("authorizationIdLock");
            }
            nukiBlePref.end();
            if(!doc["securityPinCodeLock"].isNull())
            {
                if(doc["securityPinCodeLock"].as<String>().length() > 0) _nuki->setPin(doc["securityPinCodeLock"].as<int>());
                else _nuki->setPin(0xffff);
            }
            nukiBlePref.begin("NukiHubopener", false);
            if(!doc["bleAddressOpener"].isNull())
            {
                if (doc["bleAddressOpener"].as<String>().length() == 12)
                {
                    String value = doc["bleAddressOpener"].as<String>();
                    for(int i=0; i<value.length();i+=2) currentBleAddressOpn[(i/2)] = std::stoi(value.substring(i, i+2).c_str(), nullptr, 16);
                    nukiBlePref.putBytes("bleAddress", currentBleAddressOpn, 6);
                }
                else _preferences->remove("bleAddressOpener");
            }
            if(!doc["secretKeyKOpener"].isNull())
            {
                if (doc["secretKeyKOpener"].as<String>().length() == 64)
                {
                    String value = doc["secretKeyKOpener"].as<String>();
                    for(int i=0; i<value.length();i+=2) secretKeyKOpn[(i/2)] = std::stoi(value.substring(i, i+2).c_str(), nullptr, 16);
                    nukiBlePref.putBytes("secretKeyK", secretKeyKOpn, 32);
                }
                else _preferences->remove("secretKeyKOpener");
            }
            if(!doc["authorizationIdOpener"].isNull())
            {
                if (doc["authorizationIdOpener"].as<String>().length() == 8)
                {
                    String value = doc["authorizationIdOpener"].as<String>();
                    for(int i=0; i<value.length();i+=2) authorizationIdOpn[(i/2)] = std::stoi(value.substring(i, i+2).c_str(), nullptr, 16);
                    nukiBlePref.putBytes("authorizationId", authorizationIdOpn, 4);
                }
                else _preferences->remove("authorizationIdOpener");
            }
            nukiBlePref.end();
            if(!doc["securityPinCodeOpener"].isNull())
            {
                if(doc["securityPinCodeOpener"].as<String>().length() > 0) _nukiOpener->setPin(doc["securityPinCodeOpener"].as<int>());
                else _nukiOpener->setPin(0xffff);
            }

            configChanged = true;
        }
    }

    if(configChanged)
    {
        message = "Configuration saved ... restarting.";
        _enabled = false;
        _preferences->end();
    }

    return configChanged;
}

void WebCfgServer::processGpioArgs()
{
    int count = _server.args();

    std::vector<PinEntry> pinConfiguration;

    for(int index = 0; index < count; index++)
    {
        String key = _server.argName(index);
        String value = _server.arg(index);

        PinRole role = (PinRole)value.toInt();
        if(role != PinRole::Disabled)
        {
            PinEntry entry;
            entry.pin = key.toInt();
            entry.role = role;
            pinConfiguration.push_back(entry);
        }
    }

    _gpio->savePinConfiguration(pinConfiguration);
}

void WebCfgServer::buildImportExportHtml(String &response)
{
    buildHtmlHeader(response);

    response.concat("<div id=\"upform\"><h4>Import configuration</h4>");
    response.concat("<form method=\"post\" action=\"import\"><textarea id=\"importjson\" name=\"importjson\" rows=\"10\" cols=\"50\"></textarea><br/>");
    response.concat("<br><input type=\"submit\" name=\"submit\" value=\"Import\"></form><br><br></div>");
    response.concat("<div id=\"gitdiv\">");
    response.concat("<h4>Export configuration</h4><br>");
    response.concat("<button title=\"Basic export\" onclick=\" window.open('/export', '_self'); return false;\">Basic export</button>");
    response.concat("<br><br><button title=\"Export with redacted settings\" onclick=\" window.open('/export?redacted=1'); return false;\">Export with redacted settings</button>");
    response.concat("<br><br><button title=\"Export with redacted settings and pairing data\" onclick=\" window.open('/export?redacted=1&pairing=1'); return false;\">Export with redacted settings and pairing data</button>");
    response.concat("</div><div id=\"msgdiv\" style=\"visibility:hidden\">Initiating config update. Please be patient.<br>You will be forwarded automatically when the import is complete.</div>");
    response.concat("<script type=\"text/javascript\">");
    response.concat("window.addEventListener('load', function () {");
    response.concat("	var button = document.getElementById(\"submitbtn\");");
    response.concat("	button.addEventListener('click',hideshow,false);");
    response.concat("	function hideshow() {");
    response.concat("		document.getElementById('upform').style.visibility = 'hidden';");
    response.concat("		document.getElementById('gitdiv').style.visibility = 'hidden';");
    response.concat("		document.getElementById('msgdiv').style.visibility = 'visible';");
    response.concat("	}");
    response.concat("});");
    response.concat("</script>");
    response.concat("</body></html>");
}

void WebCfgServer::buildHtml(String& response)
{
    String header = "<script>let intervalId; window.onload = function() { updateInfo(); intervalId = setInterval(updateInfo, 3000); }; function updateInfo() { var request = new XMLHttpRequest(); request.open('GET', '/status', true); request.onload = () => { const obj = JSON.parse(request.responseText); if (obj.stop == 1) { clearInterval(intervalId); } for (var key of Object.keys(obj)) { if(key=='ota' && document.getElementById(key) !== null) { document.getElementById(key).innerText = \"<a href='/ota'>\" + obj[key] + \"</a>\"; } else if(document.getElementById(key) !== null) { document.getElementById(key).innerText = obj[key]; } } }; request.send(); }</script>";
    buildHtmlHeader(response, header);

    response.concat("<br><h3>Info</h3>\n");
    response.concat("<table>");

    printParameter(response, "Hostname", _hostname.c_str(), "", "hostname");
    printParameter(response, "MQTT Connected", _network->mqttConnectionState() > 0 ? "Yes" : "No", "", "mqttState");
    if(_nuki != nullptr)
    {
        char lockStateArr[20];
        NukiLock::lockstateToString(_nuki->keyTurnerState().lockState, lockStateArr);
        printParameter(response, "Nuki Lock paired", _nuki->isPaired() ? ("Yes (BLE Address " + _nuki->getBleAddress().toString() + ")").c_str() : "No", "", "lockPaired");
        printParameter(response, "Nuki Lock state", lockStateArr, "", "lockState");

        if(_nuki->isPaired())
        {
            String lockState = pinStateToString(_preferences->getInt(preference_lock_pin_status, 4));
            printParameter(response, "Nuki Lock PIN status", lockState.c_str(), "", "lockPin");

            if(_preferences->getBool(preference_official_hybrid, false))
            {
                String offConnected = _nuki->offConnected() ? "Yes": "No";
                printParameter(response, "Nuki Lock hybrid mode connected", offConnected.c_str(), "", "lockHybrid");
            }
        }
    }
    if(_nukiOpener != nullptr)
    {
        char openerStateArr[20];
        NukiOpener::lockstateToString(_nukiOpener->keyTurnerState().lockState, openerStateArr);
        printParameter(response, "Nuki Opener paired", _nukiOpener->isPaired() ? ("Yes (BLE Address " + _nukiOpener->getBleAddress().toString() + ")").c_str() : "No", "", "openerPaired");

        if(_nukiOpener->keyTurnerState().nukiState == NukiOpener::State::ContinuousMode) printParameter(response, "Nuki Opener state", "Open (Continuous Mode)", "", "openerState");
        else printParameter(response, "Nuki Opener state", openerStateArr, "", "openerState");

        if(_nukiOpener->isPaired())
        {
            String openerState = pinStateToString(_preferences->getInt(preference_opener_pin_status, 4));
            printParameter(response, "Nuki Opener PIN status", openerState.c_str(), "", "openerPin");
        }
    }

    printParameter(response, "Firmware", NUKI_HUB_VERSION, "/info", "firmware");

    if(_preferences->getBool(preference_check_updates)) printParameter(response, "Latest Firmware", _preferences->getString(preference_latest_version).c_str(), "/ota", "ota");

    response.concat("</table><br>");
    response.concat("<ul id=\"tblnav\">");
    buildNavigationMenuEntry(response, "MQTT and Network Configuration", "/mqttconfig",  _brokerConfigured ? "" : "Please configure MQTT broker");
    buildNavigationMenuEntry(response, "Nuki Configuration", "/nukicfg");
    buildNavigationMenuEntry(response, "Access Level Configuration", "/acclvl");
    buildNavigationMenuEntry(response, "Credentials", "/cred", _pinsConfigured ? "" : "Please configure PIN");
    buildNavigationMenuEntry(response, "GPIO Configuration", "/gpiocfg");
    buildNavigationMenuEntry(response, "Firmware update", "/ota");
    buildNavigationMenuEntry(response, "Import/Export Configuration", "/impexpcfg");

    // buildNavigationButton(response, "Edit", "/mqttconfig", _brokerConfigured ? "" : "<font color=\"#f07000\"><em>(!) Please configure MQTT broker</em></font>");
    // buildNavigationButton(response, "Edit", "/cred", _pinsConfigured ? "" : "<font color=\"#f07000\"><em>(!) Please configure PIN</em></font>");

    if(_preferences->getBool(preference_publish_debug_info, false))
    {
        buildNavigationMenuEntry(response, "Advanced Configuration", "/advanced");
    }
    
    if(_preferences->getBool(preference_webserial_enabled, false))
    {
        buildNavigationMenuEntry(response, "Open Webserial", "/webserial");
    }

    if(_allowRestartToPortal)
    {
        buildNavigationMenuEntry(response, "Configure Wi-Fi", "/wifi");
    }

    response.concat("</ul></body></html>");
}


void WebCfgServer::buildCredHtml(String &response)
{
    buildHtmlHeader(response);

    response.concat("<form class=\"adapt\" method=\"post\" action=\"savecfg\">");
    response.concat("<h3>Credentials</h3>");
    response.concat("<table>");
    printInputField(response, "CREDUSER", "User (# to clear)", _preferences->getString(preference_cred_user).c_str(), 30, "", false, true);
    printInputField(response, "CREDPASS", "Password", "*", 30, "", true, true);
    printInputField(response, "CREDPASSRE", "Retype password", "*", 30, "", true);
    response.concat("</table>");
    response.concat("<br><input type=\"submit\" name=\"submit\" value=\"Save\">");
    response.concat("</form>");

    if(_nuki != nullptr)
    {
        response.concat("<br><br><form class=\"adapt\" method=\"post\" action=\"savecfg\">");
        response.concat("<h3>Nuki Lock PIN</h3>");
        response.concat("<table>");
        printInputField(response, "NUKIPIN", "PIN Code (# to clear)", "*", 20, "", true);
        response.concat("</table>");
        response.concat("<br><input type=\"submit\" name=\"submit\" value=\"Save\">");
        response.concat("</form>");
    }

    if(_nukiOpener != nullptr)
    {
        response.concat("<br><br><form class=\"adapt\" method=\"post\" action=\"savecfg\">");
        response.concat("<h3>Nuki Opener PIN</h3>");
        response.concat("<table>");
        printInputField(response, "NUKIOPPIN", "PIN Code (# to clear)", "*", 20, "", true);
        response.concat("</table>");
        response.concat("<br><input type=\"submit\" name=\"submit\" value=\"Save\">");
        response.concat("</form>");
    }

    _confirmCode = generateConfirmCode();
    if(_nuki != nullptr)
    {
        response.concat("<br><br><h3>Unpair Nuki Lock</h3>");
        response.concat("<form class=\"adapt\" method=\"post\" action=\"/unpairlock\">");
        response.concat("<table>");
        String message = "Type ";
        message.concat(_confirmCode);
        message.concat(" to confirm unpair");
        printInputField(response, "CONFIRMTOKEN", message.c_str(), "", 10, "");
        response.concat("</table>");
        response.concat("<br><button type=\"submit\">OK</button></form>");
    }

    if(_nukiOpener != nullptr)
    {
        response.concat("<br><br><h3>Unpair Nuki Opener</h3>");
        response.concat("<form class=\"adapt\" method=\"post\" action=\"/unpairopener\">");
        response.concat("<table>");
        String message = "Type ";
        message.concat(_confirmCode);
        message.concat(" to confirm unpair");
        printInputField(response, "CONFIRMTOKEN", message.c_str(), "", 10, "");
        response.concat("</table>");
        response.concat("<br><button type=\"submit\">OK</button></form>");
    }

    response.concat("<br><br><h3>Factory reset Nuki Hub</h3>");
    response.concat("<h4 class=\"warning\">This will reset all settings to default and unpair Nuki Lock and/or Opener. Optionally will also reset WiFi settings and reopen WiFi manager portal.</h4>");
    response.concat("<form class=\"adapt\" method=\"post\" action=\"/factoryreset\">");
    response.concat("<table>");
    String message = "Type ";
    message.concat(_confirmCode);
    message.concat(" to confirm factory reset");
    printInputField(response, "CONFIRMTOKEN", message.c_str(), "", 10, "");
    printCheckBox(response, "WIFI", "Also reset WiFi settings", false, "");
    response.concat("</table>");
    response.concat("<br><button type=\"submit\">OK</button></form>");
    response.concat("</body></html>");
}

void WebCfgServer::buildMqttConfigHtml(String &response)
{
    buildHtmlHeader(response);
    response.concat("<form class=\"adapt\" method=\"post\" action=\"savecfg\">");
    response.concat("<h3>Basic MQTT and Network Configuration</h3>");
    response.concat("<table>");
    printInputField(response, "HOSTNAME", "Host name", _preferences->getString(preference_hostname).c_str(), 100, "");
    printInputField(response, "MQTTSERVER", "MQTT Broker", _preferences->getString(preference_mqtt_broker).c_str(), 100, "");
    printInputField(response, "MQTTPORT", "MQTT Broker port", _preferences->getInt(preference_mqtt_broker_port), 5, "");
    printInputField(response, "MQTTUSER", "MQTT User (# to clear)", _preferences->getString(preference_mqtt_user).c_str(), 30, "", false, true);
    printInputField(response, "MQTTPASS", "MQTT Password", "*", 30, "", true, true);
    response.concat("</table><br>");

    response.concat("<h3>Advanced MQTT and Network Configuration</h3>");
    response.concat("<table>");
    printInputField(response, "HASSDISCOVERY", "Home Assistant discovery topic (empty to disable; usually homeassistant)", _preferences->getString(preference_mqtt_hass_discovery).c_str(), 30, "");
    printInputField(response, "HASSCUURL", "Home Assistant device configuration URL (empty to use http://LOCALIP; fill when using a reverse proxy for example)", _preferences->getString(preference_mqtt_hass_cu_url).c_str(), 261, "");
    if(_nukiOpener != nullptr) printCheckBox(response, "OPENERCONT", "Set Nuki Opener Lock/Unlock action in Home Assistant to Continuous mode", _preferences->getBool(preference_opener_continuous_mode), "");
    printTextarea(response, "MQTTCA", "MQTT SSL CA Certificate (*, optional)", _preferences->getString(preference_mqtt_ca).c_str(), TLS_CA_MAX_SIZE, _network->encryptionSupported(), true);
    printTextarea(response, "MQTTCRT", "MQTT SSL Client Certificate (*, optional)", _preferences->getString(preference_mqtt_crt).c_str(), TLS_CERT_MAX_SIZE, _network->encryptionSupported(), true);
    printTextarea(response, "MQTTKEY", "MQTT SSL Client Key (*, optional)", _preferences->getString(preference_mqtt_key).c_str(), TLS_KEY_MAX_SIZE, _network->encryptionSupported(), true);
    printDropDown(response, "NWHW", "Network hardware", String(_preferences->getInt(preference_network_hardware)), getNetworkDetectionOptions());
    printCheckBox(response, "NWHWWIFIFB", "Disable fallback to Wi-Fi / Wi-Fi config portal", _preferences->getBool(preference_network_wifi_fallback_disabled), "");
    printCheckBox(response, "BESTRSSI", "Connect to AP with the best signal in an environment with multiple APs with the same SSID", _preferences->getBool(preference_find_best_rssi), "");
    printInputField(response, "RSSI", "RSSI Publish interval (seconds; -1 to disable)", _preferences->getInt(preference_rssi_publish_interval), 6, "");
    printInputField(response, "NETTIMEOUT", "MQTT Timeout until restart (seconds; -1 to disable)", _preferences->getInt(preference_network_timeout), 5, "");
    printCheckBox(response, "RSTDISC", "Restart on disconnect", _preferences->getBool(preference_restart_on_disconnect), "");
    printCheckBox(response, "RECNWTMQTTDIS", "Reconnect network on MQTT connection failure", _preferences->getBool(preference_recon_netw_on_mqtt_discon), "");
    printCheckBox(response, "MQTTLOG", "Enable MQTT logging", _preferences->getBool(preference_mqtt_log_enabled), "");
    printCheckBox(response, "WEBLOG", "Enable WebSerial logging", _preferences->getBool(preference_webserial_enabled), "");    
    printCheckBox(response, "CHECKUPDATE", "Check for Firmware Updates every 24h", _preferences->getBool(preference_check_updates), "");
    printCheckBox(response, "UPDATEMQTT", "Allow updating using MQTT", _preferences->getBool(preference_update_from_mqtt), "");
    printCheckBox(response, "DISNONJSON", "Disable some extraneous non-JSON topics", _preferences->getBool(preference_disable_non_json), "");
    printCheckBox(response, "OFFHYBRID", "Enable hybrid official MQTT and Nuki Hub setup", _preferences->getBool(preference_official_hybrid), "");
    printCheckBox(response, "HYBRIDACT", "Enable sending actions through official MQTT", _preferences->getBool(preference_official_hybrid_actions), "");
    printInputField(response, "HYBRIDTIMER", "Time between status updates when official MQTT is offline (seconds)", _preferences->getInt(preference_query_interval_hybrid_lockstate), 5, "");
    printCheckBox(response, "HYBRIDRETRY", "Retry command sent using official MQTT over BLE if failed", _preferences->getBool(preference_official_hybrid_retry), "");
    response.concat("</table>");
    response.concat("* If no encryption is configured for the MQTT broker, leave empty. Only supported for Wi-Fi connections.<br><br>");

    response.concat("<h3>IP Address assignment</h3>");
    response.concat("<table>");
    printCheckBox(response, "DHCPENA", "Enable DHCP", _preferences->getBool(preference_ip_dhcp_enabled), "");
    printInputField(response, "IPADDR", "Static IP address", _preferences->getString(preference_ip_address).c_str(), 15, "");
    printInputField(response, "IPSUB", "Subnet", _preferences->getString(preference_ip_subnet).c_str(), 15, "");
    printInputField(response, "IPGTW", "Default gateway", _preferences->getString(preference_ip_gateway).c_str(), 15, "");
    printInputField(response, "DNSSRV", "DNS Server", _preferences->getString(preference_ip_dns_server).c_str(), 15, "");
    response.concat("</table>");

    response.concat("<br><input type=\"submit\" name=\"submit\" value=\"Save\">");
    response.concat("</form>");
    response.concat("</body></html>");
}

void WebCfgServer::buildAdvancedConfigHtml(String &response)
{
    buildHtmlHeader(response);
    response.concat("<form class=\"adapt\" method=\"post\" action=\"savecfg\">");
    response.concat("<h3>Advanced Configuration</h3>");
    response.concat("<h4 class=\"warning\">Warning: Changing these settings can lead to bootloops that might require you to erase the ESP32 and reflash nukihub using USB/serial</h4>");
    response.concat("<table>");
    response.concat("<tr><td>Current bootloop prevention state</td><td>");
    response.concat(_preferences->getBool(preference_enable_bootloop_reset, false) ? "Enabled" : "Disabled");
    response.concat("</td></tr>");
    printCheckBox(response, "BTLPRST", "Enable Bootloop prevention (Try to reset these settings to default on bootloop)", true, "");
    printInputField(response, "BUFFSIZE", "Char buffer size (min 4096, max 32768)", _preferences->getInt(preference_buffer_size, CHAR_BUFFER_SIZE), 6, "");
    response.concat("<tr><td>Advised minimum char buffer size based on current settings</td><td id=\"mincharbuffer\"></td>");
    printInputField(response, "TSKNTWK", "Task size Network (min 12288, max 32768)", _preferences->getInt(preference_task_size_network, NETWORK_TASK_SIZE), 6, "");
    response.concat("<tr><td>Advised minimum network task size based on current settings</td><td id=\"minnetworktask\"></td>");
    printInputField(response, "TSKNUKI", "Task size Nuki (min 8192, max 32768)", _preferences->getInt(preference_task_size_nuki, NUKI_TASK_SIZE), 6, "");
    printInputField(response, "ALMAX", "Max auth log entries (min 1, max 50)", _preferences->getInt(preference_authlog_max_entries, MAX_AUTHLOG), 3, "inputmaxauthlog");
    printInputField(response, "KPMAX", "Max keypad entries (min 1, max 100)", _preferences->getInt(preference_keypad_max_entries, MAX_KEYPAD), 3, "inputmaxkeypad");
    printInputField(response, "TCMAX", "Max timecontrol entries (min 1, max 50)", _preferences->getInt(preference_timecontrol_max_entries, MAX_TIMECONTROL), 3, "inputmaxtimecontrol");
    printCheckBox(response, "SHOWSECRETS", "Show Pairing secrets on Info page (for 120s after next boot)", _preferences->getBool(preference_show_secrets), "");

    if(_nuki != nullptr)
    {
        printCheckBox(response, "LCKMANPAIR", "Manually set lock pairing data (enable to save values below)", false, "");
        printInputField(response, "LCKBLEADDR", "currentBleAddress", "", 12, "");
        printInputField(response, "LCKSECRETK", "secretKeyK", "", 64, "");
        printInputField(response, "LCKAUTHID", "authorizationId", "", 8, "");
    }
    if(_nukiOpener != nullptr)
    {
        printCheckBox(response, "OPNMANPAIR", "Manually set opener pairing data (enable to save values below)", false, "");
        printInputField(response, "OPNBLEADDR", "currentBleAddress", "", 12, "");
        printInputField(response, "OPNSECRETK", "secretKeyK", "", 64, "");
        printInputField(response, "OPNAUTHID", "authorizationId", "", 8, "");
    }
    printInputField(response, "OTAUPD", "Custom URL to update Nuki Hub updater", "", 255, "");
    printInputField(response, "OTAMAIN", "Custom URL to update Nuki Hub", "", 255, "");
    response.concat("</table>");

    response.concat("<br><input type=\"submit\" name=\"submit\" value=\"Save\">");
    response.concat("</form>");
    response.concat("</body><script>window.onload=function(){ document.getElementById(\"inputmaxauthlog\").addEventListener(\"keyup\", calculate);document.getElementById(\"inputmaxkeypad\").addEventListener(\"keyup\", calculate);document.getElementById(\"inputmaxtimecontrol\").addEventListener(\"keyup\", calculate); calculate(); }; function calculate() { var authlog = document.getElementById(\"inputmaxauthlog\").value; var keypad = document.getElementById(\"inputmaxkeypad\").value; var timecontrol = document.getElementById(\"inputmaxtimecontrol\").value; var charbuf = 0; var networktask = 0; var sizeauthlog = 0; var sizekeypad = 0; var sizetimecontrol = 0; if(authlog > 0) { sizeauthlog = 280 * authlog; } if(keypad > 0) { sizekeypad = 350 * keypad; } if(timecontrol > 0) { sizetimecontrol = 120 * timecontrol; } charbuf = sizetimecontrol; networktask = 10240 + sizetimecontrol; if(sizeauthlog>sizekeypad && sizeauthlog>sizetimecontrol) { charbuf = sizeauthlog; networktask = 10240 + sizeauthlog;} else if(sizekeypad>sizeauthlog && sizekeypad>sizetimecontrol) { charbuf = sizekeypad; networktask = 10240 + sizekeypad;} if(charbuf<4096) { charbuf = 4096; } else if (charbuf>32768) { charbuf = 32768; } if(networktask<12288) { networktask = 12288; } else if (networktask>32768) { networktask = 32768; } document.getElementById(\"mincharbuffer\").innerHTML = charbuf; document.getElementById(\"minnetworktask\").innerHTML = networktask; }</script></html>");
}

void WebCfgServer::buildStatusHtml(String &response)
{
    JsonDocument json;
    char _resbuf[2048];
    bool mqttDone = false;
    bool lockDone = false;
    bool openerDone = false;
    bool latestDone = false;

    json["stop"] = 0;

    if(_network->mqttConnectionState() > 0)
    {
        json["mqttState"] = "Yes";
        mqttDone = true;
    }
    else json["mqttState"] = "No";

    if(_nuki != nullptr)
    {
        char lockStateArr[20];
        NukiLock::lockstateToString(_nuki->keyTurnerState().lockState, lockStateArr);
        String lockState = lockStateArr;
        String LockPaired = (_nuki->isPaired() ? ("Yes (BLE Address " + _nuki->getBleAddress().toString() + ")").c_str() : "No");
        json["lockPaired"] = LockPaired;
        json["lockState"] = lockState;

        if(_nuki->isPaired())
        {
            json["lockPin"] = pinStateToString(_preferences->getInt(preference_lock_pin_status, 4));
            if(strcmp(lockStateArr, "undefined") != 0) lockDone = true;
        }
        else json["lockPin"] = "Not Paired";
    }
    else lockDone = true;
    if(_nukiOpener != nullptr)
    {
        char openerStateArr[20];
        NukiOpener::lockstateToString(_nukiOpener->keyTurnerState().lockState, openerStateArr);
        String openerState = openerStateArr;
        String openerPaired = (_nukiOpener->isPaired() ? ("Yes (BLE Address " + _nukiOpener->getBleAddress().toString() + ")").c_str() : "No");
        json["openerPaired"] = openerPaired;

        if(_nukiOpener->keyTurnerState().nukiState == NukiOpener::State::ContinuousMode) json["openerState"] = "Open (Continuous Mode)";
        else json["openerState"] = openerState;

        if(_nukiOpener->isPaired())
        {
            json["openerPin"] = pinStateToString(_preferences->getInt(preference_opener_pin_status, 4));
            if(strcmp(openerStateArr, "undefined") != 0) openerDone = true;
        }
        else json["openerPin"] = "Not Paired";
    }
    else openerDone = true;

    if(_preferences->getBool(preference_check_updates))
    {
        json["latestFirmware"] = _preferences->getString(preference_latest_version);
        latestDone = true;
    }
    else latestDone = true;

    if(mqttDone && lockDone && openerDone && latestDone) json["stop"] = 1;

    serializeJson(json, _resbuf, sizeof(_resbuf));
    response = _resbuf;
}

String WebCfgServer::pinStateToString(uint8_t value) {
    switch(value)
    {
        case 0:
            return (String)"PIN not set";
        case 1:
            return (String)"PIN valid";
        case 2:
            return (String)"PIN set but invalid";;
        default:
            return (String)"Unknown";
    }
}

void WebCfgServer::buildAccLvlHtml(String &response)
{
    buildHtmlHeader(response);
    uint32_t aclPrefs[17];
    _preferences->getBytes(preference_acl, &aclPrefs, sizeof(aclPrefs));

    response.concat("<form method=\"post\" action=\"savecfg\">");
    response.concat("<input type=\"hidden\" name=\"ACLLVLCHANGED\" value=\"1\">");
    response.concat("<h3>Nuki General Access Control</h3>");
    response.concat("<table><tr><th>Setting</th><th>Enabled</th></tr>");
    printCheckBox(response, "CONFPUB", "Publish Nuki configuration information", _preferences->getBool(preference_conf_info_enabled, true), "");

    if((_nuki != nullptr && _nuki->hasKeypad()) || (_nukiOpener != nullptr && _nukiOpener->hasKeypad()))
    {
        printCheckBox(response, "KPPUB", "Publish keypad entries information", _preferences->getBool(preference_keypad_info_enabled), "");
        printCheckBox(response, "KPPER", "Publish a topic per keypad entry and create HA sensor", _preferences->getBool(preference_keypad_topic_per_entry), "");
        printCheckBox(response, "KPCODE", "Also publish keypad codes (<span class=\"warning\">Disadvised for security reasons</span>)", _preferences->getBool(preference_keypad_publish_code, false), "");
        printCheckBox(response, "KPENA", "Add, modify and delete keypad codes", _preferences->getBool(preference_keypad_control_enabled), "");
    }
    printCheckBox(response, "TCPUB", "Publish time control entries information", _preferences->getBool(preference_timecontrol_info_enabled), "");
    printCheckBox(response, "TCPER", "Publish a topic per time control entry and create HA sensor", _preferences->getBool(preference_timecontrol_topic_per_entry), "");
    printCheckBox(response, "TCENA", "Add, modify and delete time control entries", _preferences->getBool(preference_timecontrol_control_enabled), "");
    printCheckBox(response, "PUBAUTH", "Publish authorization log (may reduce battery life)", _preferences->getBool(preference_publish_authdata), "");
    response.concat("</table><br>");
    if(_nuki != nullptr)
    {
        uint32_t basicLockConfigAclPrefs[16];
        _preferences->getBytes(preference_conf_lock_basic_acl, &basicLockConfigAclPrefs, sizeof(basicLockConfigAclPrefs));
        uint32_t advancedLockConfigAclPrefs[22];
        _preferences->getBytes(preference_conf_lock_advanced_acl, &advancedLockConfigAclPrefs, sizeof(advancedLockConfigAclPrefs));

        response.concat("<h3>Nuki Lock Access Control</h3>");
        response.concat("<input type=\"button\" value=\"Allow all\" style=\"margin-right: 10px;\" onclick=\"");
        response.concat("for(el of document.getElementsByClassName('chk_access_lock')){if(el.constructor.name==='HTMLInputElement'&amp;&amp;el.type==='checkbox')el.checked=true;}\">");
        response.concat("<input type=\"button\" value=\"Disallow all\" onclick=\"");
        response.concat("for(el of document.getElementsByClassName('chk_access_lock')){if(el.constructor.name==='HTMLInputElement'&amp;&amp;el.type==='checkbox')el.checked=false;}\">");
        response.concat("<table><tr><th>Action</th><th>Allowed</th></tr>");

        printCheckBox(response, "ACLLCKLCK", "Lock", ((int)aclPrefs[0] == 1), "chk_access_lock");
        printCheckBox(response, "ACLLCKUNLCK", "Unlock", ((int)aclPrefs[1] == 1), "chk_access_lock");
        printCheckBox(response, "ACLLCKUNLTCH", "Unlatch", ((int)aclPrefs[2] == 1), "chk_access_lock");
        printCheckBox(response, "ACLLCKLNG", "Lock N Go", ((int)aclPrefs[3] == 1), "chk_access_lock");
        printCheckBox(response, "ACLLCKLNGU", "Lock N Go Unlatch", ((int)aclPrefs[4] == 1), "chk_access_lock");
        printCheckBox(response, "ACLLCKFLLCK", "Full Lock", ((int)aclPrefs[5] == 1), "chk_access_lock");
        printCheckBox(response, "ACLLCKFOB1", "Fob Action 1", ((int)aclPrefs[6] == 1), "chk_access_lock");
        printCheckBox(response, "ACLLCKFOB2", "Fob Action 2", ((int)aclPrefs[7] == 1), "chk_access_lock");
        printCheckBox(response, "ACLLCKFOB3", "Fob Action 3", ((int)aclPrefs[8] == 1), "chk_access_lock");
        response.concat("</table><br>");

        response.concat("<h3>Nuki Lock Config Control (Requires PIN to be set)</h3>");
        response.concat("<input type=\"button\" value=\"Allow all\" style=\"margin-right: 10px;\" onclick=\"");
        response.concat("for(el of document.getElementsByClassName('chk_config_lock')){if(el.constructor.name==='HTMLInputElement'&amp;&amp;el.type==='checkbox')el.checked=true;}\">");
        response.concat("<input type=\"button\" value=\"Disallow all\" onclick=\"");
        response.concat("for(el of document.getElementsByClassName('chk_config_lock')){if(el.constructor.name==='HTMLInputElement'&amp;&amp;el.type==='checkbox')el.checked=false;}\">");
        response.concat("<table><tr><th>Change</th><th>Allowed</th></tr>");

        printCheckBox(response, "CONFLCKNAME", "Name", ((int)basicLockConfigAclPrefs[0] == 1), "chk_config_lock");
        printCheckBox(response, "CONFLCKLAT", "Latitude", ((int)basicLockConfigAclPrefs[1] == 1), "chk_config_lock");
        printCheckBox(response, "CONFLCKLONG", "Longitude", ((int)basicLockConfigAclPrefs[2] == 1), "chk_config_lock");
        printCheckBox(response, "CONFLCKAUNL", "Auto unlatch", ((int)basicLockConfigAclPrefs[3] == 1), "chk_config_lock");
        printCheckBox(response, "CONFLCKPRENA", "Pairing enabled", ((int)basicLockConfigAclPrefs[4] == 1), "chk_config_lock");
        printCheckBox(response, "CONFLCKBTENA", "Button enabled", ((int)basicLockConfigAclPrefs[5] == 1), "chk_config_lock");
        printCheckBox(response, "CONFLCKLEDENA", "LED flash enabled", ((int)basicLockConfigAclPrefs[6] == 1), "chk_config_lock");
        printCheckBox(response, "CONFLCKLEDBR", "LED brightness", ((int)basicLockConfigAclPrefs[7] == 1), "chk_config_lock");
        printCheckBox(response, "CONFLCKTZOFF", "Timezone offset", ((int)basicLockConfigAclPrefs[8] == 1), "chk_config_lock");
        printCheckBox(response, "CONFLCKDSTM", "DST mode", ((int)basicLockConfigAclPrefs[9] == 1), "chk_config_lock");
        printCheckBox(response, "CONFLCKFOB1", "Fob Action 1", ((int)basicLockConfigAclPrefs[10] == 1), "chk_config_lock");
        printCheckBox(response, "CONFLCKFOB2", "Fob Action 2", ((int)basicLockConfigAclPrefs[11] == 1), "chk_config_lock");
        printCheckBox(response, "CONFLCKFOB3", "Fob Action 3", ((int)basicLockConfigAclPrefs[12] == 1), "chk_config_lock");
        printCheckBox(response, "CONFLCKSGLLCK", "Single Lock", ((int)basicLockConfigAclPrefs[13] == 1), "chk_config_lock");
        printCheckBox(response, "CONFLCKADVM", "Advertising Mode", ((int)basicLockConfigAclPrefs[14] == 1), "chk_config_lock");
        printCheckBox(response, "CONFLCKTZID", "Timezone ID", ((int)basicLockConfigAclPrefs[15] == 1), "chk_config_lock");

        printCheckBox(response, "CONFLCKUPOD", "Unlocked Position Offset Degrees", ((int)advancedLockConfigAclPrefs[0] == 1), "chk_config_lock");
        printCheckBox(response, "CONFLCKLPOD", "Locked Position Offset Degrees", ((int)advancedLockConfigAclPrefs[1] == 1), "chk_config_lock");
        printCheckBox(response, "CONFLCKSLPOD", "Single Locked Position Offset Degrees", ((int)advancedLockConfigAclPrefs[2] == 1), "chk_config_lock");
        printCheckBox(response, "CONFLCKUTLTOD", "Unlocked To Locked Transition Offset Degrees", ((int)advancedLockConfigAclPrefs[3] == 1), "chk_config_lock");
        printCheckBox(response, "CONFLCKLNGT", "Lock n Go timeout", ((int)advancedLockConfigAclPrefs[4] == 1), "chk_config_lock");
        printCheckBox(response, "CONFLCKSBPA", "Single button press action", ((int)advancedLockConfigAclPrefs[5] == 1), "chk_config_lock");
        printCheckBox(response, "CONFLCKDBPA", "Double button press action", ((int)advancedLockConfigAclPrefs[6] == 1), "chk_config_lock");
        printCheckBox(response, "CONFLCKDC", "Detached cylinder", ((int)advancedLockConfigAclPrefs[7] == 1), "chk_config_lock");
        printCheckBox(response, "CONFLCKBATT", "Battery type", ((int)advancedLockConfigAclPrefs[8] == 1), "chk_config_lock");
        printCheckBox(response, "CONFLCKABTD", "Automatic battery type detection", ((int)advancedLockConfigAclPrefs[9] == 1), "chk_config_lock");
        printCheckBox(response, "CONFLCKUNLD", "Unlatch duration", ((int)advancedLockConfigAclPrefs[10] == 1), "chk_config_lock");
        printCheckBox(response, "CONFLCKALT", "Auto lock timeout", ((int)advancedLockConfigAclPrefs[11] == 1), "chk_config_lock");
        printCheckBox(response, "CONFLCKAUNLD", "Auto unlock disabled", ((int)advancedLockConfigAclPrefs[12] == 1), "chk_config_lock");
        printCheckBox(response, "CONFLCKNMENA", "Nightmode enabled", ((int)advancedLockConfigAclPrefs[13] == 1), "chk_config_lock");
        printCheckBox(response, "CONFLCKNMST", "Nightmode start time", ((int)advancedLockConfigAclPrefs[14] == 1), "chk_config_lock");
        printCheckBox(response, "CONFLCKNMET", "Nightmode end time", ((int)advancedLockConfigAclPrefs[15] == 1), "chk_config_lock");
        printCheckBox(response, "CONFLCKNMALENA", "Nightmode auto lock enabled", ((int)advancedLockConfigAclPrefs[16] == 1), "chk_config_lock");
        printCheckBox(response, "CONFLCKNMAULD", "Nightmode auto unlock disabled", ((int)advancedLockConfigAclPrefs[17] == 1), "chk_config_lock");
        printCheckBox(response, "CONFLCKNMLOS", "Nightmode immediate lock on start", ((int)advancedLockConfigAclPrefs[18] == 1), "chk_config_lock");
        printCheckBox(response, "CONFLCKALENA", "Auto lock enabled", ((int)advancedLockConfigAclPrefs[19] == 1), "chk_config_lock");
        printCheckBox(response, "CONFLCKIALENA", "Immediate auto lock enabled", ((int)advancedLockConfigAclPrefs[20] == 1), "chk_config_lock");
        printCheckBox(response, "CONFLCKAUENA", "Auto update enabled", ((int)advancedLockConfigAclPrefs[21] == 1), "chk_config_lock");
        response.concat("</table><br>");
    }
    if(_nukiOpener != nullptr)
    {
        uint32_t basicOpenerConfigAclPrefs[14];
        _preferences->getBytes(preference_conf_opener_basic_acl, &basicOpenerConfigAclPrefs, sizeof(basicOpenerConfigAclPrefs));
        uint32_t advancedOpenerConfigAclPrefs[20];
        _preferences->getBytes(preference_conf_opener_advanced_acl, &advancedOpenerConfigAclPrefs, sizeof(advancedOpenerConfigAclPrefs));

        response.concat("<h3>Nuki Opener Access Control</h3>");
        response.concat("<input type=\"button\" value=\"Allow all\" style=\"margin-right: 10px;\" onclick=\"");
        response.concat("for(el of document.getElementsByClassName('chk_access_opener')){if(el.constructor.name==='HTMLInputElement'&amp;&amp;el.type==='checkbox')el.checked=true;}\">");
        response.concat("<input type=\"button\" value=\"Disallow all\" onclick=\"");
        response.concat("for(el of document.getElementsByClassName('chk_access_opener')){if(el.constructor.name==='HTMLInputElement'&amp;&amp;el.type==='checkbox')el.checked=false;}\">");
        response.concat("<table><tr><th>Action</th><th>Allowed</th></tr>");

        printCheckBox(response, "ACLOPNUNLCK", "Activate Ring-to-Open", ((int)aclPrefs[9] == 1), "chk_access_opener");
        printCheckBox(response, "ACLOPNLCK", "Deactivate Ring-to-Open", ((int)aclPrefs[10] == 1), "chk_access_opener");
        printCheckBox(response, "ACLOPNUNLTCH", "Electric Strike Actuation", ((int)aclPrefs[11] == 1), "chk_access_opener");
        printCheckBox(response, "ACLOPNUNLCKCM", "Activate Continuous Mode", ((int)aclPrefs[12] == 1), "chk_access_opener");
        printCheckBox(response, "ACLOPNLCKCM", "Deactivate Continuous Mode", ((int)aclPrefs[13] == 1), "chk_access_opener");
        printCheckBox(response, "ACLOPNFOB1", "Fob Action 1", ((int)aclPrefs[14] == 1), "chk_access_opener");
        printCheckBox(response, "ACLOPNFOB2", "Fob Action 2", ((int)aclPrefs[15] == 1), "chk_access_opener");
        printCheckBox(response, "ACLOPNFOB3", "Fob Action 3", ((int)aclPrefs[16] == 1), "chk_access_opener");
        response.concat("</table><br>");

        response.concat("<h3>Nuki Opener Config Control (Requires PIN to be set)</h3>");
        response.concat("<input type=\"button\" value=\"Allow all\" style=\"margin-right: 10px;\" onclick=\"");
        response.concat("for(el of document.getElementsByClassName('chk_config_opener')){if(el.constructor.name==='HTMLInputElement'&amp;&amp;el.type==='checkbox')el.checked=true;}\">");
        response.concat("<input type=\"button\" value=\"Disallow all\" onclick=\"");
        response.concat("for(el of document.getElementsByClassName('chk_config_opener')){if(el.constructor.name==='HTMLInputElement'&amp;&amp;el.type==='checkbox')el.checked=false;}\">");
        response.concat("<table><tr><th>Change</th><th>Allowed</th></tr>");

        printCheckBox(response, "CONFOPNNAME", "Name", ((int)basicOpenerConfigAclPrefs[0] == 1), "chk_config_opener");
        printCheckBox(response, "CONFOPNLAT", "Latitude", ((int)basicOpenerConfigAclPrefs[1] == 1), "chk_config_opener");
        printCheckBox(response, "CONFOPNLONG", "Longitude", ((int)basicOpenerConfigAclPrefs[2] == 1), "chk_config_opener");
        printCheckBox(response, "CONFOPNPRENA", "Pairing enabled", ((int)basicOpenerConfigAclPrefs[3] == 1), "chk_config_opener");
        printCheckBox(response, "CONFOPNBTENA", "Button enabled", ((int)basicOpenerConfigAclPrefs[4] == 1), "chk_config_opener");
        printCheckBox(response, "CONFOPNLEDENA", "LED flash enabled", ((int)basicOpenerConfigAclPrefs[5] == 1), "chk_config_opener");
        printCheckBox(response, "CONFOPNTZOFF", "Timezone offset", ((int)basicOpenerConfigAclPrefs[6] == 1), "chk_config_opener");
        printCheckBox(response, "CONFOPNDSTM", "DST mode", ((int)basicOpenerConfigAclPrefs[7] == 1), "chk_config_opener");
        printCheckBox(response, "CONFOPNFOB1", "Fob Action 1", ((int)basicOpenerConfigAclPrefs[8] == 1), "chk_config_opener");
        printCheckBox(response, "CONFOPNFOB2", "Fob Action 2", ((int)basicOpenerConfigAclPrefs[9] == 1), "chk_config_opener");
        printCheckBox(response, "CONFOPNFOB3", "Fob Action 3", ((int)basicOpenerConfigAclPrefs[10] == 1), "chk_config_opener");
        printCheckBox(response, "CONFOPNOPM", "Operating Mode", ((int)basicOpenerConfigAclPrefs[11] == 1), "chk_config_opener");
        printCheckBox(response, "CONFOPNADVM", "Advertising Mode", ((int)basicOpenerConfigAclPrefs[12] == 1), "chk_config_opener");
        printCheckBox(response, "CONFOPNTZID", "Timezone ID", ((int)basicOpenerConfigAclPrefs[13] == 1), "chk_config_opener");

        printCheckBox(response, "CONFOPNICID", "Intercom ID", ((int)advancedOpenerConfigAclPrefs[0] == 1), "chk_config_opener");
        printCheckBox(response, "CONFOPNBUSMS", "BUS mode Switch", ((int)advancedOpenerConfigAclPrefs[1] == 1), "chk_config_opener");
        printCheckBox(response, "CONFOPNSCDUR", "Short Circuit Duration", ((int)advancedOpenerConfigAclPrefs[2] == 1), "chk_config_opener");
        printCheckBox(response, "CONFOPNESD", "Eletric Strike Delay", ((int)advancedOpenerConfigAclPrefs[3] == 1), "chk_config_opener");
        printCheckBox(response, "CONFOPNRESD", "Random Electric Strike Delay", ((int)advancedOpenerConfigAclPrefs[4] == 1), "chk_config_opener");
        printCheckBox(response, "CONFOPNESDUR", "Electric Strike Duration", ((int)advancedOpenerConfigAclPrefs[5] == 1), "chk_config_opener");
        printCheckBox(response, "CONFOPNDRTOAR", "Disable RTO after ring", ((int)advancedOpenerConfigAclPrefs[6] == 1), "chk_config_opener");
        printCheckBox(response, "CONFOPNRTOT", "RTO timeout", ((int)advancedOpenerConfigAclPrefs[7] == 1), "chk_config_opener");
        printCheckBox(response, "CONFOPNDRBSUP", "Doorbell suppression", ((int)advancedOpenerConfigAclPrefs[8] == 1), "chk_config_opener");
        printCheckBox(response, "CONFOPNDRBSUPDUR", "Doorbell suppression duration", ((int)advancedOpenerConfigAclPrefs[9] == 1), "chk_config_opener");
        printCheckBox(response, "CONFOPNSRING", "Sound Ring", ((int)advancedOpenerConfigAclPrefs[10] == 1), "chk_config_opener");
        printCheckBox(response, "CONFOPNSOPN", "Sound Open", ((int)advancedOpenerConfigAclPrefs[11] == 1), "chk_config_opener");
        printCheckBox(response, "CONFOPNSRTO", "Sound RTO", ((int)advancedOpenerConfigAclPrefs[12] == 1), "chk_config_opener");
        printCheckBox(response, "CONFOPNSCM", "Sound CM", ((int)advancedOpenerConfigAclPrefs[13] == 1), "chk_config_opener");
        printCheckBox(response, "CONFOPNSCFRM", "Sound confirmation", ((int)advancedOpenerConfigAclPrefs[14] == 1), "chk_config_opener");
        printCheckBox(response, "CONFOPNSLVL", "Sound level", ((int)advancedOpenerConfigAclPrefs[15] == 1), "chk_config_opener");
        printCheckBox(response, "CONFOPNSBPA", "Single button press action", ((int)advancedOpenerConfigAclPrefs[16] == 1), "chk_config_opener");
        printCheckBox(response, "CONFOPNDBPA", "Double button press action", ((int)advancedOpenerConfigAclPrefs[17] == 1), "chk_config_opener");
        printCheckBox(response, "CONFOPNBATT", "Battery type", ((int)advancedOpenerConfigAclPrefs[18] == 1), "chk_config_opener");
        printCheckBox(response, "CONFOPNABTD", "Automatic battery type detection", ((int)advancedOpenerConfigAclPrefs[19] == 1), "chk_config_opener");
        response.concat("</table><br>");
    }
    response.concat("<br><input type=\"submit\" name=\"submit\" value=\"Save\">");
    response.concat("</form>");
    response.concat("</body></html>");
}

void WebCfgServer::buildNukiConfigHtml(String &response)
{
    buildHtmlHeader(response);

    response.concat("<form class=\"adapt\" method=\"post\" action=\"savecfg\">");
    response.concat("<h3>Basic Nuki Configuration</h3>");
    response.concat("<table>");
    printCheckBox(response, "LOCKENA", "Nuki Smartlock enabled", _preferences->getBool(preference_lock_enabled), "");

    if(_preferences->getBool(preference_lock_enabled))
    {
        printInputField(response, "MQTTPATH", "MQTT Nuki Smartlock Path", _preferences->getString(preference_mqtt_lock_path).c_str(), 180, "");
    }

    printCheckBox(response, "OPENA", "Nuki Opener enabled", _preferences->getBool(preference_opener_enabled), "");

    if(_preferences->getBool(preference_opener_enabled))
    {
        printInputField(response, "MQTTOPPATH", "MQTT Nuki Opener Path", _preferences->getString(preference_mqtt_opener_path).c_str(), 180, "");
    }
    response.concat("</table><br>");

    response.concat("<h3>Advanced Nuki Configuration</h3>");
    response.concat("<table>");

    printInputField(response, "LSTINT", "Query interval lock state (seconds)", _preferences->getInt(preference_query_interval_lockstate), 10, "");
    printInputField(response, "CFGINT", "Query interval configuration (seconds)", _preferences->getInt(preference_query_interval_configuration), 10, "");
    printInputField(response, "BATINT", "Query interval battery (seconds)", _preferences->getInt(preference_query_interval_battery), 10, "");
    if((_nuki != nullptr && _nuki->hasKeypad()) || (_nukiOpener != nullptr && _nukiOpener->hasKeypad()))
    {
        printInputField(response, "KPINT", "Query interval keypad (seconds)", _preferences->getInt(preference_query_interval_keypad), 10, "");
    }
    printInputField(response, "NRTRY", "Number of retries if command failed", _preferences->getInt(preference_command_nr_of_retries), 10, "");
    printInputField(response, "TRYDLY", "Delay between retries (milliseconds)", _preferences->getInt(preference_command_retry_delay), 10, "");
    if(_nuki != nullptr) printCheckBox(response, "REGAPP", "Lock: Nuki Bridge is running alongside Nuki Hub (needs re-pairing if changed)", _preferences->getBool(preference_register_as_app), "");
    if(_nukiOpener != nullptr) printCheckBox(response, "REGAPPOPN", "Opener: Nuki Bridge is running alongside Nuki Hub (needs re-pairing if changed)", _preferences->getBool(preference_register_opener_as_app), "");
#if PRESENCE_DETECTION_ENABLED
    printInputField(response, "PRDTMO", "Presence detection timeout (seconds; -1 to disable)", _preferences->getInt(preference_presence_detection_timeout), 10, "");
#endif
    printInputField(response, "RSBC", "Restart if bluetooth beacons not received (seconds; -1 to disable)", _preferences->getInt(preference_restart_ble_beacon_lost), 10, "");
    printInputField(response, "TXPWR", "BLE transmit power in dB (minimum -12, maximum 9)", _preferences->getInt(preference_ble_tx_power, 9), 10, "");

    response.concat("</table>");
    response.concat("<br><input type=\"submit\" name=\"submit\" value=\"Save\">");
    response.concat("</form>");
    response.concat("</body></html>");
}

void WebCfgServer::buildGpioConfigHtml(String &response)
{
    buildHtmlHeader(response);

    response.concat("<form method=\"post\" action=\"savegpiocfg\">");
    response.concat("<h3>GPIO Configuration</h3>");
    response.concat("<table>");

    const auto& availablePins = _gpio->availablePins();
    for(const auto& pin : availablePins)
    {
        String pinStr = String(pin);
        String pinDesc = "Gpio " + pinStr;

        printDropDown(response, pinStr.c_str(), pinDesc.c_str(), getPreselectionForGpio(pin), getGpioOptions());
    }

    response.concat("</table>");
    response.concat("<br><input type=\"submit\" name=\"submit\" value=\"Save\">");
    response.concat("</form>");
    response.concat("</body></html>");
}

void WebCfgServer::buildConfigureWifiHtml(String &response)
{
    buildHtmlHeader(response);

    response.concat("<h3>Wi-Fi</h3>");
    response.concat("Click confirm to restart ESP into Wi-Fi configuration mode. After restart, connect to ESP access point to reconfigure Wi-Fi.<br><br>");
    buildNavigationButton(response, "Confirm", "/wifimanager");

    response.concat("</body></html>");
}

void WebCfgServer::buildInfoHtml(String &response)
{
    DebugPreferences debugPreferences;

    buildHtmlHeader(response);
    response.concat("<h3>System Information</h3> <pre>");

    response.concat("Nuki Hub version: ");
    response.concat(NUKI_HUB_VERSION);
    response.concat("\n");
    response.concat("Nuki Hub build: ");
    response.concat(NUKI_HUB_BUILD);
    response.concat("\n");
    response.concat("Nuki Hub build type: ");
    #ifndef DEBUG_NUKIHUB
    response.concat("Release\n");
    #else
    response.concat("Debug\n");
    #endif

    response.concat(debugPreferences.preferencesToString(_preferences));

    response.concat("MQTT connected: ");
    response.concat(_network->mqttConnectionState() > 0 ? "Yes\n" : "No\n");

    uint32_t aclPrefs[17];
    _preferences->getBytes(preference_acl, &aclPrefs, sizeof(aclPrefs));

    if(_nuki != nullptr)
    {
        uint32_t basicLockConfigAclPrefs[16];
        _preferences->getBytes(preference_conf_lock_basic_acl, &basicLockConfigAclPrefs, sizeof(basicLockConfigAclPrefs));
        uint32_t advancedLockConfigAclPrefs[22];
        _preferences->getBytes(preference_conf_lock_advanced_acl, &advancedLockConfigAclPrefs, sizeof(advancedLockConfigAclPrefs));

        response.concat("Lock firmware version: ");
        response.concat(_nuki->firmwareVersion().c_str());
        response.concat("\nLock hardware version: ");
        response.concat(_nuki->hardwareVersion().c_str());
        response.concat("\nLock paired: ");
        response.concat(_nuki->isPaired() ? "Yes\n" : "No\n");
        response.concat("Lock valid PIN set: ");
        response.concat(_nuki->isPaired() ? _nuki->isPinValid() ? "Yes\n" : "No\n" : "-\n");
        response.concat("Lock has door sensor: ");
        response.concat(_nuki->hasDoorSensor() ? "Yes\n" : "No\n");
        response.concat("Lock has keypad: ");
        response.concat(_nuki->hasKeypad() ? "Yes\n" : "No\n");
        response.concat("Lock ACL (Lock): ");
        response.concat((int)aclPrefs[0] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock ACL (Unlock): ");
        response.concat((int)aclPrefs[1] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock ACL (Unlatch): ");
        response.concat((int)aclPrefs[2] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock ACL (Lock N Go): ");
        response.concat((int)aclPrefs[3] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock ACL (Lock N Go Unlatch): ");
        response.concat((int)aclPrefs[4] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock ACL (Full Lock): ");
        response.concat((int)aclPrefs[5] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock ACL (Fob Action 1): ");
        response.concat((int)aclPrefs[6] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock ACL (Fob Action 2): ");
        response.concat((int)aclPrefs[7] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock ACL (Fob Action 3): ");
        response.concat((int)aclPrefs[8] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock config ACL (Name): ");
        response.concat((int)basicLockConfigAclPrefs[0] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock config ACL (Latitude): ");
        response.concat((int)basicLockConfigAclPrefs[1] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock config ACL (Longitude): ");
        response.concat((int)basicLockConfigAclPrefs[2] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock config ACL (Auto Unlatch): ");
        response.concat((int)basicLockConfigAclPrefs[3] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock config ACL (Pairing enabled): ");
        response.concat((int)basicLockConfigAclPrefs[4] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock config ACL (Button enabled): ");
        response.concat((int)basicLockConfigAclPrefs[5] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock config ACL (LED flash enabled): ");
        response.concat((int)basicLockConfigAclPrefs[6] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock config ACL (LED brightness): ");
        response.concat((int)basicLockConfigAclPrefs[7] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock config ACL (Timezone offset): ");
        response.concat((int)basicLockConfigAclPrefs[8] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock config ACL (DST mode): ");
        response.concat((int)basicLockConfigAclPrefs[9] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock config ACL (Fob Action 1): ");
        response.concat((int)basicLockConfigAclPrefs[10] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock config ACL (Fob Action 2): ");
        response.concat((int)basicLockConfigAclPrefs[11] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock config ACL (Fob Action 3): ");
        response.concat((int)basicLockConfigAclPrefs[12] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock config ACL (Single Lock): ");
        response.concat((int)basicLockConfigAclPrefs[13] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock config ACL (Advertising Mode): ");
        response.concat((int)basicLockConfigAclPrefs[14] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock config ACL (Timezone ID): ");
        response.concat((int)basicLockConfigAclPrefs[15] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock config ACL (Unlocked Position Offset Degrees): ");
        response.concat((int)advancedLockConfigAclPrefs[0] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock config ACL (Locked Position Offset Degrees): ");
        response.concat((int)advancedLockConfigAclPrefs[1] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock config ACL (Single Locked Position Offset Degrees): ");
        response.concat((int)advancedLockConfigAclPrefs[2] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock config ACL (Unlocked To Locked Transition Offset Degrees): ");
        response.concat((int)advancedLockConfigAclPrefs[3] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock config ACL (Lock n Go timeout): ");
        response.concat((int)advancedLockConfigAclPrefs[4] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock config ACL (Single button press action): ");
        response.concat((int)advancedLockConfigAclPrefs[5] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock config ACL (Double button press action): ");
        response.concat((int)advancedLockConfigAclPrefs[6] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock config ACL (Detached cylinder): ");
        response.concat((int)advancedLockConfigAclPrefs[7] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock config ACL (Battery type): ");
        response.concat((int)advancedLockConfigAclPrefs[8] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock config ACL (Automatic battery type detection): ");
        response.concat((int)advancedLockConfigAclPrefs[9] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock config ACL (Unlatch duration): ");
        response.concat((int)advancedLockConfigAclPrefs[10] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock config ACL (Auto lock timeout): ");
        response.concat((int)advancedLockConfigAclPrefs[11] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock config ACL (Auto unlock disabled): ");
        response.concat((int)advancedLockConfigAclPrefs[12] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock config ACL (Nightmode enabled): ");
        response.concat((int)advancedLockConfigAclPrefs[13] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock config ACL (Nightmode start time): ");
        response.concat((int)advancedLockConfigAclPrefs[14] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock config ACL (Nightmode end time): ");
        response.concat((int)advancedLockConfigAclPrefs[15] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock config ACL (Nightmode auto lock enabled): ");
        response.concat((int)advancedLockConfigAclPrefs[16] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock config ACL (Nightmode auto unlock disabled): ");
        response.concat((int)advancedLockConfigAclPrefs[17] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock config ACL (Nightmode immediate lock on start): ");
        response.concat((int)advancedLockConfigAclPrefs[18] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock config ACL (Auto lock enabled): ");
        response.concat((int)advancedLockConfigAclPrefs[19] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock config ACL (Immediate auto lock enabled): ");
        response.concat((int)advancedLockConfigAclPrefs[20] ? "Allowed\n" : "Disallowed\n");
        response.concat("Lock config ACL (Auto update enabled): ");
        response.concat((int)advancedLockConfigAclPrefs[21] ? "Allowed\n" : "Disallowed\n");
    }

    if(_nukiOpener != nullptr)
    {
        uint32_t basicOpenerConfigAclPrefs[14];
        _preferences->getBytes(preference_conf_opener_basic_acl, &basicOpenerConfigAclPrefs, sizeof(basicOpenerConfigAclPrefs));
        uint32_t advancedOpenerConfigAclPrefs[20];
        _preferences->getBytes(preference_conf_opener_advanced_acl, &advancedOpenerConfigAclPrefs, sizeof(advancedOpenerConfigAclPrefs));
        response.concat("Opener firmware version: ");
        response.concat(_nukiOpener->firmwareVersion().c_str());
        response.concat("\nOpener hardware version: ");
        response.concat(_nukiOpener->hardwareVersion().c_str());        response.concat("\nOpener paired: ");
        response.concat(_nukiOpener->isPaired() ? "Yes\n" : "No\n");
        response.concat("Opener valid PIN set: ");
        response.concat(_nukiOpener->isPaired() ? _nukiOpener->isPinValid() ? "Yes\n" : "No\n" : "-\n");
        response.concat("Opener has keypad: ");
        response.concat(_nukiOpener->hasKeypad() ? "Yes\n" : "No\n");
        response.concat("Opener ACL (Activate Ring-to-Open): ");
        response.concat((int)aclPrefs[9] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener ACL (Deactivate Ring-to-Open): ");
        response.concat((int)aclPrefs[10] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener ACL (Electric Strike Actuation): ");
        response.concat((int)aclPrefs[11] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener ACL (Activate Continuous Mode): ");
        response.concat((int)aclPrefs[12] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener ACL (Deactivate Continuous Mode): ");
        response.concat((int)aclPrefs[13] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener ACL (Fob Action 1): ");
        response.concat((int)aclPrefs[14] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener ACL (Fob Action 2): ");
        response.concat((int)aclPrefs[15] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener ACL (Fob Action 3): ");
        response.concat((int)aclPrefs[16] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener config ACL (Name): ");
        response.concat((int)basicOpenerConfigAclPrefs[0] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener config ACL (Latitude): ");
        response.concat((int)basicOpenerConfigAclPrefs[1] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener config ACL (Longitude): ");
        response.concat((int)basicOpenerConfigAclPrefs[2] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener config ACL (Pairing enabled): ");
        response.concat((int)basicOpenerConfigAclPrefs[3] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener config ACL (Button enabled): ");
        response.concat((int)basicOpenerConfigAclPrefs[4] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener config ACL (LED flash enabled): ");
        response.concat((int)basicOpenerConfigAclPrefs[5] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener config ACL (Timezone offset): ");
        response.concat((int)basicOpenerConfigAclPrefs[6] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener config ACL (DST mode): ");
        response.concat((int)basicOpenerConfigAclPrefs[7] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener config ACL (Fob Action 1): ");
        response.concat((int)basicOpenerConfigAclPrefs[8] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener config ACL (Fob Action 2): ");
        response.concat((int)basicOpenerConfigAclPrefs[9] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener config ACL (Fob Action 3): ");
        response.concat((int)basicOpenerConfigAclPrefs[10] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener config ACL (Operating Mode): ");
        response.concat((int)basicOpenerConfigAclPrefs[11] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener config ACL (Advertising Mode): ");
        response.concat((int)basicOpenerConfigAclPrefs[12] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener config ACL (Timezone ID): ");
        response.concat((int)basicOpenerConfigAclPrefs[13] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener config ACL (Intercom ID): ");
        response.concat((int)advancedOpenerConfigAclPrefs[0] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener config ACL (BUS mode Switch): ");
        response.concat((int)advancedOpenerConfigAclPrefs[1] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener config ACL (Short Circuit Duration): ");
        response.concat((int)advancedOpenerConfigAclPrefs[2] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener config ACL (Eletric Strike Delay): ");
        response.concat((int)advancedOpenerConfigAclPrefs[3] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener config ACL (Random Electric Strike Delay): ");
        response.concat((int)advancedOpenerConfigAclPrefs[4] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener config ACL (Electric Strike Duration): ");
        response.concat((int)advancedOpenerConfigAclPrefs[5] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener config ACL (Disable RTO after ring): ");
        response.concat((int)advancedOpenerConfigAclPrefs[6] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener config ACL (RTO timeout): ");
        response.concat((int)advancedOpenerConfigAclPrefs[7] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener config ACL (Doorbell suppression): ");
        response.concat((int)advancedOpenerConfigAclPrefs[8] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener config ACL (Doorbell suppression duration): ");
        response.concat((int)advancedOpenerConfigAclPrefs[9] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener config ACL (Sound Ring): ");
        response.concat((int)advancedOpenerConfigAclPrefs[10] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener config ACL (Sound Open): ");
        response.concat((int)advancedOpenerConfigAclPrefs[11] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener config ACL (Sound RTO): ");
        response.concat((int)advancedOpenerConfigAclPrefs[12] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener config ACL (Sound CM): ");
        response.concat((int)advancedOpenerConfigAclPrefs[13] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener config ACL (Sound confirmation): ");
        response.concat((int)advancedOpenerConfigAclPrefs[14] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener config ACL (Sound level): ");
        response.concat((int)advancedOpenerConfigAclPrefs[15] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener config ACL (Single button press action): ");
        response.concat((int)advancedOpenerConfigAclPrefs[16] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener config ACL (Double button press action): ");
        response.concat((int)advancedOpenerConfigAclPrefs[17] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener config ACL (Battery type): ");
        response.concat((int)advancedOpenerConfigAclPrefs[18] ? "Allowed\n" : "Disallowed\n");
        response.concat("Opener config ACL (Automatic battery type detection): ");
        response.concat((int)advancedOpenerConfigAclPrefs[19] ? "Allowed\n" : "Disallowed\n");
    }

    if(_preferences->getBool(preference_show_secrets))
    {
        if(_nuki != nullptr)
        {
            char tmp[16];
            unsigned char currentBleAddress[6];
            unsigned char authorizationId[4] = {0x00};
            unsigned char secretKeyK[32] = {0x00};
            Preferences nukiBlePref;
            nukiBlePref.begin("NukiHub", false);
            nukiBlePref.getBytes("bleAddress", currentBleAddress, 6);
            nukiBlePref.getBytes("secretKeyK", secretKeyK, 32);
            nukiBlePref.getBytes("authorizationId", authorizationId, 4);
            nukiBlePref.end();
            response.concat("Lock bleAddress: ");
            for (int i = 0; i < 6; i++) {
              sprintf(tmp, "%02x", currentBleAddress[i]);
              response.concat(tmp);
            }
            response.concat("\nLock secretKeyK: ");
            for (int i = 0; i < 32; i++) {
              sprintf(tmp, "%02x", secretKeyK[i]);
              response.concat(tmp);
            }
            response.concat("\nLock authorizationId: ");
            for (int i = 0; i < 4; i++) {
              sprintf(tmp, "%02x", authorizationId[i]);
              response.concat(tmp);
            }
            response.concat("\n");
        }
        if(_nukiOpener != nullptr)
        {
            char tmp[16];
            unsigned char currentBleAddressOpn[6];
            unsigned char authorizationIdOpn[4] = {0x00};
            unsigned char secretKeyKOpn[32] = {0x00};
            Preferences nukiBlePref;
            nukiBlePref.begin("NukiHubopener", false);
            nukiBlePref.getBytes("bleAddress", currentBleAddressOpn, 6);
            nukiBlePref.getBytes("secretKeyK", secretKeyKOpn, 32);
            nukiBlePref.getBytes("authorizationId", authorizationIdOpn, 4);
            nukiBlePref.end();
            response.concat("Opener bleAddress: ");
            for (int i = 0; i < 6; i++) {
              sprintf(tmp, "%02x", currentBleAddressOpn[i]);
              response.concat(tmp);
            }
            response.concat("\nOpener secretKeyK: ");
            for (int i = 0; i < 32; i++) {
              sprintf(tmp, "%02x", secretKeyKOpn[i]);
              response.concat(tmp);
            }
            response.concat("\nOpener authorizationId: ");
            for (int i = 0; i < 4; i++) {
              sprintf(tmp, "%02x", authorizationIdOpn[i]);
              response.concat(tmp);
            }
            response.concat("\n");
        }
    }

    response.concat("Network device: ");
    response.concat(_network->networkDeviceName());
    response.concat("\n");

    if(_network->networkDeviceName() == "Built-in Wi-Fi")
    {
        response.concat("BSSID of AP: ");
        response.concat(_network->networkBSSID());
        response.concat("\n");
    }

    response.concat("Uptime: ");
    response.concat(esp_timer_get_time() / 1000 / 1000 / 60);
    response.concat(" minutes\n");

    response.concat("Heap: ");
    response.concat(esp_get_free_heap_size());
    response.concat("\n");

    response.concat("Stack watermarks: nw: ");
    response.concat(uxTaskGetStackHighWaterMark(networkTaskHandle));
    response.concat(", nuki: ");
    response.concat(uxTaskGetStackHighWaterMark(nukiTaskHandle));
    response.concat("\n");

    _gpio->getConfigurationText(response, _gpio->pinConfiguration());

    response.concat("Restart reason FW: ");
    response.concat(getRestartReason());
    response.concat( "\n");

    response.concat("Restart reason ESP: ");
    response.concat(getEspRestartReason());
    response.concat("\n");

    response.concat("</pre> </body></html>");
}

void WebCfgServer::processUnpair(bool opener)
{
    String response = "";
    if(_server.args() == 0)
    {
        buildConfirmHtml(response, "Confirm code is invalid.", 3);
        _server.send(200, "text/html", response);
        return;
    }
    else
    {
        String key = _server.argName(0);
        String value = _server.arg(0);

        if(key != "CONFIRMTOKEN" || value != _confirmCode)
        {
            buildConfirmHtml(response, "Confirm code is invalid.", 3);
            _server.send(200, "text/html", response);
            return;
        }
    }

    buildConfirmHtml(response, opener ? "Unpairing Nuki Opener and restarting." : "Unpairing Nuki Lock and restarting.", 3);
    _server.send(200, "text/html", response);
    if(!opener && _nuki != nullptr)
    {
        _nuki->disableHASS();
        _nuki->unpair();
    }
    if(opener && _nukiOpener != nullptr)
    {
        _nukiOpener->disableHASS();
        _nukiOpener->unpair();
    }
    waitAndProcess(false, 1000);
    restartEsp(RestartReason::DeviceUnpaired);
}

void WebCfgServer::processFactoryReset()
{
    bool resetWifi = false;
    String response = "";
    if(_server.args() == 0)
    {
        buildConfirmHtml(response, "Confirm code is invalid.", 3);
        _server.send(200, "text/html", response);
        return;
    }
    else
    {
        String key = _server.argName(0);
        String value = _server.arg(0);

        if(key != "CONFIRMTOKEN" || value != _confirmCode)
        {
            buildConfirmHtml(response, "Confirm code is invalid.", 3);
            _server.send(200, "text/html", response);
            return;
        }

        String key2 = _server.argName(2);
        String value2 = _server.arg(2);

        if(key2 == "WIFI" && value2 == "1")
        {
            resetWifi = true;
            buildConfirmHtml(response, "Factory resetting Nuki Hub, unpairing Nuki Lock and Nuki Opener and resetting WiFi.", 3);
        }
        else buildConfirmHtml(response, "Factory resetting Nuki Hub, unpairing Nuki Lock and Nuki Opener.", 3);
    }

    _server.send(200, "text/html", response);
    waitAndProcess(false, 2000);

    if(_nuki != nullptr)
    {
        _nuki->disableHASS();
        _nuki->unpair();
    }
    if(_nukiOpener != nullptr)
    {
        _nukiOpener->disableHASS();
        _nukiOpener->unpair();
    }

    _preferences->clear();

    if(resetWifi)
    {
        wifi_config_t current_conf;
        esp_wifi_get_config((wifi_interface_t)ESP_IF_WIFI_STA, &current_conf);
        memset(current_conf.sta.ssid, 0, sizeof(current_conf.sta.ssid));
        memset(current_conf.sta.password, 0, sizeof(current_conf.sta.password));
        esp_wifi_set_config((wifi_interface_t)ESP_IF_WIFI_STA, &current_conf);
        _network->reconfigureDevice();
    }

    waitAndProcess(false, 3000);
    restartEsp(RestartReason::NukiHubReset);
}

void WebCfgServer::printInputField(String& response,
                                   const char *token,
                                   const char *description,
                                   const char *value,
                                   const size_t& maxLength,
                                   const char *id,
                                   const bool& isPassword,
                                   const bool& showLengthRestriction)
{
    char maxLengthStr[20];

    itoa(maxLength, maxLengthStr, 10);

    response.concat("<tr><td>");
    response.concat(description);

    if(showLengthRestriction)
    {
        response.concat(" (Max. ");
        response.concat(maxLength);
        response.concat(" characters)");
    }

    response.concat("</td><td>");
    response.concat("<input type=");
    response.concat(isPassword ? "\"password\"" : "\"text\"");
    if(strcmp(id, "") != 0)
    {
        response.concat(" id=\"");
        response.concat(id);
        response.concat("\"");
    }
    if(strcmp(value, "") != 0)
    {
    response.concat(" value=\"");
    response.concat(value);
    }
    response.concat("\" name=\"");
    response.concat(token);
    response.concat("\" size=\"25\" maxlength=\"");
    response.concat(maxLengthStr);
    response.concat("\"/>");
    response.concat("</td></tr>");
}

void WebCfgServer::printInputField(String& response,
                                   const char *token,
                                   const char *description,
                                   const int value,
                                   size_t maxLength,
                                   const char *id)
{
    char valueStr[20];
    itoa(value, valueStr, 10);
    printInputField(response, token, description, valueStr, maxLength, id);
}

void WebCfgServer::printCheckBox(String &response, const char *token, const char *description, const bool value, const char *htmlClass)
{
    response.concat("<tr><td>");
    response.concat(description);
    response.concat("</td><td>");

    response.concat("<input type=hidden name=\"");
    response.concat(token);
    response.concat("\" value=\"0\"");
    response.concat("/>");

    response.concat("<input type=checkbox name=\"");
    response.concat(token);

    response.concat("\" class=\"");
    response.concat(htmlClass);

    response.concat("\" value=\"1\"");
    response.concat(value ? " checked=\"checked\"" : "");
    response.concat("/></td></tr>");
}

void WebCfgServer::printTextarea(String& response,
                                   const char *token,
                                   const char *description,
                                   const char *value,
                                   const size_t& maxLength,
                                   const bool& enabled,
                                   const bool& showLengthRestriction)
{
    char maxLengthStr[20];

    itoa(maxLength, maxLengthStr, 10);

    response.concat("<tr><td>");
    response.concat(description);
    if(showLengthRestriction)
    {
        response.concat(" (Max. ");
        response.concat(maxLength);
        response.concat(" characters)");
    }
    response.concat("</td><td>");
    response.concat(" <textarea ");
    if(!enabled)
    {
        response.concat("disabled");
    }
    response.concat(" name=\"");
    response.concat(token);
    response.concat("\" maxlength=\"");
    response.concat(maxLengthStr);
    response.concat("\">");
    response.concat(value);
    response.concat("</textarea>");
    response.concat("</td></tr>");
}

void WebCfgServer::printDropDown(String &response, const char *token, const char *description, const String preselectedValue, const std::vector<std::pair<String, String>> options)
{
    response.concat("<tr><td>");
    response.concat(description);
    response.concat("</td><td>");

    response.concat("<select name=\"");
    response.concat(token);
    response.concat("\">");

    for(const auto& option : options)
    {
        if(option.first == preselectedValue)
        {
            response.concat("<option selected=\"selected\" value=\"");
        }
        else
        {
            response.concat("<option value=\"");
        }
        response.concat(option.first);
        response.concat("\">");
        response.concat(option.second);
        response.concat("</option>");
    }

    response.concat("</select>");
    response.concat("</td></tr>");
}

void WebCfgServer::buildNavigationButton(String &response, const char *caption, const char *targetPath, const char* labelText)
{
    response.concat("<form method=\"get\" action=\"");
    response.concat(targetPath);
    response.concat("\">");
    response.concat("<button type=\"submit\">");
    response.concat(caption);
    response.concat("</button> ");
    response.concat(labelText);
    response.concat("</form>");
}

void WebCfgServer::buildNavigationMenuEntry(String &response, const char *title, const char *targetPath, const char* warningMessage)
{
    response.concat("<a href=\"");
    response.concat(targetPath);
    response.concat("\">");
    response.concat("<li>");
    response.concat(title);
    if(strcmp(warningMessage, "") != 0){
        response.concat("<span>");
        response.concat(warningMessage);
        response.concat("</span>");
    }
    response.concat("</li></a>");
}

void WebCfgServer::printParameter(String& response, const char *description, const char *value, const char *link, const char *id)
{
    response.concat("<tr>");
    response.concat("<td>");
    response.concat(description);
    response.concat("</td>");
    if(strcmp(id, "") == 0) response.concat("<td>");
    else
    {
        response.concat("<td id=\"");
        response.concat(id);
        response.concat("\">");
    }
    if(strcmp(link, "") == 0) response.concat(value);
    else
    {
        response.concat("<a href=\"");
        response.concat(link);
        response.concat("\"> ");
        response.concat(value);
        response.concat("</a>");
    }
    response.concat("</td>");
    response.concat("</tr>");

}


String WebCfgServer::generateConfirmCode()
{
    int code = random(1000,9999);
    return String(code);
}

const std::vector<std::pair<String, String>> WebCfgServer::getNetworkDetectionOptions() const
{
    std::vector<std::pair<String, String>> options;

    options.push_back(std::make_pair("1", "Wi-Fi only"));
    options.push_back(std::make_pair("2", "Generic W5500"));
    options.push_back(std::make_pair("3", "M5Stack Atom POE (W5500)"));
    options.push_back(std::make_pair("4", "Olimex ESP32-POE / ESP-POE-ISO"));
    options.push_back(std::make_pair("5", "WT32-ETH01"));
    options.push_back(std::make_pair("6", "M5STACK PoESP32 Unit"));
    options.push_back(std::make_pair("7", "LilyGO T-ETH-POE"));
    options.push_back(std::make_pair("8", "GL-S10"));

    return options;
}

const std::vector<std::pair<String, String>> WebCfgServer::getGpioOptions() const
{
    std::vector<std::pair<String, String>> options;

    const auto& roles = _gpio->getAllRoles();

    for(const auto& role : roles)
    {
        options.push_back( std::make_pair(String((int)role), _gpio->getRoleDescription(role)));
    }

    return options;
}

String WebCfgServer::getPreselectionForGpio(const uint8_t &pin)
{
    const std::vector<PinEntry>& pinConfiguration = _gpio->pinConfiguration();

    for(const auto& entry : pinConfiguration)
    {
        if(pin == entry.pin)
        {
            return String((int8_t)entry.role);
        }
    }

    return String((int8_t)PinRole::Disabled);
}
#endif
