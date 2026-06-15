// main.cpp
#include <Arduino.h>
#include "processMenu.h"
#include "processInput.h"
#include "bluetoothManager.h"
// #include "bluetooth.h"
#include "netManager.h"

void setup()
{
	Serial.begin(115200);
	delay(3000);
	Serial.println("[BOOT] Starting...");

	NetMgr::setupNetwork();
	Serial.println("[BOOT] Network initialized");

	BluetoothManager::Instance().SetupBluetooth();
	// Bluetooth::SetupBluetooth();
	Serial.println("[BOOT] Bluetooth initialized");

	Serial.println("[BOOT] Ready");

	ProcessMenu::printConfigMenu();
	ProcessMenu::setupProcessMenu();
}

void loop()
{
	NetMgr::loopNetwork();
	ProcessMenu::handleConfigInput();
	ProcessMenu::handleConfigInputSub();
	ProcessInput::handlePairingInput();

	yield();
}
