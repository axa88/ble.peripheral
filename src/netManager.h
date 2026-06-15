// netManager.h
#pragma once
#include <Arduino.h>
#include <atomic>

namespace NetMgr
{
	extern const char *ssid;
	extern const char *password;
	extern IPAddress local_IP;
	extern IPAddress gateway;
	extern IPAddress subnet;

	extern std::atomic<unsigned long> lastRestartAttempt;
	extern const unsigned long restartInterval;

	void setupNetwork();
	void loopNetwork();
}