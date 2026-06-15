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
	Serial.println("Booting");

	NetMgr::setupNetwork();
	Serial.println("Net init");

	BluetoothManager::Instance().SetupBluetooth();
	// Bluetooth::SetupBluetooth();
	Serial.println("BT init");

	Serial.println("booted");

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
