// main.cpp
#include <Arduino.h>
#include "bleConfig.h"
#if BLE_ENABLE_SERIAL_CONSOLE
#include "processMenu.h"
#include "processInput.h"
#endif
#include "bluetoothManager.h"
#include "netManager.h"
#if BLE_ENABLE_WEB_INTERFACE
#include "webInterface.h"
#endif

void setup()
{
	Serial.begin(115200);
	delay(3000);
	Serial.println("[BOOT] Starting...");
	NetMgr::setupNetwork();
	Serial.println("[BOOT] Network initialized");
	BluetoothManager::Instance().SetupBluetooth();
#if BLE_ENABLE_WEB_INTERFACE
	if (!WebInterface::begin()) Serial.println("[ERR] Web interface initialization failed");
#endif
	Serial.println("[BOOT] Bluetooth initialized");
	Serial.println("[BOOT] Ready");
#if BLE_ENABLE_SERIAL_CONSOLE
	ProcessMenu::setupProcessMenu();
#endif
}

void loop()
{
	NetMgr::loopNetwork();
#if BLE_ENABLE_SERIAL_CONSOLE
	ProcessInput::loopConsole();
#endif
	yield();
}
