//netManager.cpp
#include "netManager.h"
#include <Arduino.h>     // Serial, IPAddress, millis(), ESP.restart()
#include <WiFi.h>        // WiFi, WiFi.config, WiFi.begin, WiFi.onEvent, WL_CONNECTED
#include <ArduinoOTA.h>  // ArduinoOTA.begin(), ArduinoOTA.handle()

#ifndef WIFI_SSID
#error "WIFI_SSID not defined - set it as a build_flag in platformio.ini"
#endif
#ifndef WIFI_PASSWORD
#error "WIFI_PASSWORD not defined - set it as a build_flag in platformio.ini"
#endif
#ifndef OTA_HOSTNAME
#error "OTA_HOSTNAME not defined - set it as a build_flag in platformio.ini for this environment"
#endif
#ifndef OTA_PASSWORD
#error "OTA_PASSWORD not defined - set it as a build_flag in platformio.ini for this environment"
#endif
#ifndef DEVICE_IP
#error "DEVICE_IP not defined - set it as a build_flag in platformio.ini for this environment, e.g. -DDEVICE_IP=192,168,1,160"
#endif

namespace NetMgr
{
	const char *ssid = WIFI_SSID;
	const char *password = WIFI_PASSWORD;
	IPAddress local_IP(DEVICE_IP);
	IPAddress gateway(192, 168, 1, 1);
	IPAddress subnet(255, 255, 255, 0);

	// Restart timer
	std::atomic<unsigned long> lastRestartAttempt{0};
	const unsigned long restartInterval = 60000;

	void setupNetwork()
	{
		// WiFi setup
		if (!WiFi.config(local_IP, gateway, subnet))
			Serial.println("[ERR] STA failed to configure");

		WiFi.mode(WIFI_STA);
		WiFi.setAutoReconnect(true);
		WiFi.persistent(true);

		// Register WiFi callbacks
		WiFi.onEvent(
			[](arduino_event_id_t event)
			{
				if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED)
				{
					Serial.println("[NET] WiFi disconnected, reconnecting...");
					WiFi.begin(ssid, password);
				}
				else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP)
				{
					Serial.printf("[NET] WiFi connected, IP: %s\n", WiFi.localIP().toString().c_str());
					lastRestartAttempt = millis(); // mark time of last successful connection
				}
			});

		WiFi.begin(ssid, password);
		lastRestartAttempt = millis();

		// OTA setup
		ArduinoOTA.setHostname(OTA_HOSTNAME);
		ArduinoOTA.setPassword(OTA_PASSWORD);

		ArduinoOTA.onStart([]()
		{
			const char *type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
			Serial.printf("[OTA] Update starting: %s\n", type);
		});

		ArduinoOTA.onEnd([]()
		{
			Serial.println("[OTA] Update complete, rebooting");
		});

		ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
		{
			if (total == 0) return;
			Serial.printf("[OTA] Progress: %u%%\r", (progress * 100) / total);
		});

		ArduinoOTA.onError([](ota_error_t error)
		{
			const char *desc = "Unknown Error";
			switch (error)
			{
				case OTA_AUTH_ERROR: desc = "Auth Failed"; break;
				case OTA_BEGIN_ERROR: desc = "Begin Failed"; break;
				case OTA_CONNECT_ERROR: desc = "Connect Failed"; break;
				case OTA_RECEIVE_ERROR: desc = "Receive Failed"; break;
				case OTA_END_ERROR: desc = "End Failed"; break;
				default: break;
			}
			Serial.printf("[ERR] OTA error [%u]: %s\n", error, desc);
		});

		ArduinoOTA.begin();
		Serial.println("[OTA] Initialized");
	}

	void loopNetwork() // wont work while console input blocks
	{
		ArduinoOTA.handle();

		// Restart if WiFi disconnected too long
		if (WiFi.status() != WL_CONNECTED && millis() - lastRestartAttempt >= restartInterval)
		{
			Serial.println("[NET] WiFi unreachable for too long, rebooting...");
			ESP.restart();
		}
	}
}