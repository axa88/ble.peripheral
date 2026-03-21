//network.cpp
#include "network.h"
#include <Arduino.h>     // Serial, IPAddress, millis(), ESP.restart()
#include <WiFi.h>        // WiFi, WiFi.config, WiFi.begin, WiFi.onEvent, WL_CONNECTED
#include <ArduinoOTA.h>  // ArduinoOTA.begin(), ArduinoOTA.handle()

namespace Network
{
	const char *ssid = "TheMatrix";
	const char *password = "Whatever!88";
	IPAddress local_IP(192, 168, 1, 160);
	IPAddress gateway(192, 168, 1, 1);
	IPAddress subnet(255, 255, 255, 0);

	// OTA timers
	unsigned long lastReconnectAttempt = 0;
	const unsigned long reconnectInterval = 10000;
	unsigned long lastRestartAttempt = 0;
	const unsigned long restartInterval = 60000;

	void setupNetwork()
	{
		// WiFi setup
		if (!WiFi.config(local_IP, gateway, subnet))
			Serial.println("STA Failed to configure");

		WiFi.mode(WIFI_STA);
		WiFi.setAutoReconnect(true);
		WiFi.persistent(true);

		// Register WiFi callbacks
		WiFi.onEvent(
			[](arduino_event_id_t event)
			{
				if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED)
				{
					Serial.println("WiFi disconnected. Attempting reconnect...");
					WiFi.begin(ssid, password);
					lastRestartAttempt = millis(); // mark time of disconnect
				}
				else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP)
				{
					Serial.print("WiFi connected! IP: ");
					Serial.println(WiFi.localIP());
				}
			});

		WiFi.begin(ssid, password);
		lastRestartAttempt = millis();

		// OTA setup
		ArduinoOTA.begin();
		Serial.println("OTA init");
	}

	void loopNetwork() // wont work while console input blocks
	{
		ArduinoOTA.handle();

		// Restart if WiFi disconnected too long
		if (WiFi.status() != WL_CONNECTED && millis() - lastRestartAttempt >= restartInterval)
		{
			Serial.println("WiFi wont connect. Reboot...");
			ESP.restart();
		}
	}
}