// netManager.h
#pragma once
#include <Arduino.h>

namespace NetMgr
{
	extern const char *ssid;
	extern const char *password;
	extern IPAddress local_IP;
	extern IPAddress gateway;
	extern IPAddress subnet;

	void setupNetwork();
	void loopNetwork();
}
