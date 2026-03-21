// main.cpp
#include "processMenu.h"
#include "processInput.h"
#include "bluetoothManager.h"
// #include "bluetooth.h"
// #include "network.h"

void setup()
{
	Serial.begin(115200);

	delay(5000);
	
	Serial.println("Booting");

	// Network::setupNetwork();
	// Serial.println("Net init");

	BluetoothManager::Instance().SetupBluetooth();
	// Bluetooth::SetupBluetooth();
	Serial.println("BT init");

	Serial.println("booted");

	ProcessMenu::printConfigMenu();
	ProcessMenu::setupProcessMenu();
}

void loop()
{
	// loopNetwork();
	ProcessMenu::handleConfigInput();
	ProcessMenu::handleConfigInputSub();
	ProcessInput::handlePairingInput();

	yield();
}
