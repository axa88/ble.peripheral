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

	ProcessMenu::setupProcessMenu();
}

void loop()
{
	NetMgr::loopNetwork();

	#if ARDUINO_USB_CDC_ON_BOOT
	static bool menuPrinted = false;
	if (!menuPrinted && Serial)
	{
		while (Serial.available() > 0) Serial.read(); // drain garbage from CDC enumeration
		ProcessMenu::printConfigMenu();
		menuPrinted = true;
	}
	if (menuPrinted)
	{
	#endif
		ProcessMenu::handleConfigInput();
		ProcessMenu::handleConfigInputSub();
		ProcessInput::handlePairingInput();
	#if ARDUINO_USB_CDC_ON_BOOT
	}
	#endif

	yield();
}
