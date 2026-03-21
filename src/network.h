// network.h
#pragma once
#include <Arduino.h>

namespace Network
{
	extern const char *ssid;
	extern const char *password;
	extern IPAddress local_IP;
	extern IPAddress gateway;
	extern IPAddress subnet;

	extern unsigned long lastReconnectAttempt;
	extern const unsigned long reconnectInterval;
	extern unsigned long lastRestartAttempt;
	extern const unsigned long restartInterval;

	void setupNetwork();
	void loopNetwork();
}