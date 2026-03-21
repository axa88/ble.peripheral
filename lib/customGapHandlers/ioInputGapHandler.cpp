//ioInputGapHandler.cpp
#include <Arduino.h>
#include "ioInputGapHandler.h"
#include <NimBLEDevice.h>

// Custom GAP handler
int ioInputGapHandler(ble_gap_event *event, void *arg)
{
	Serial.printf("\nCustom GAP EVENT: %s\n", NimBLEUtils::gapEventToString(event->type));
	switch (event->type)
	{
		case BLE_GAP_EVENT_PASSKEY_ACTION:
		{
			Serial.print("  PASSKEY_ACTION: ");
			int action = event->passkey.params.action;
			const char *act_str;
			switch (action)
			{
				case BLE_SM_IOACT_NONE:			act_str = "NONE"; break;
				case BLE_SM_IOACT_OOB:			act_str = "OOB"; break;
				case BLE_SM_IOACT_INPUT:		act_str = "INPUT"; break;
				case BLE_SM_IOACT_DISP:			act_str = "DISPLAY"; break;
				case BLE_SM_IOACT_NUMCMP:		act_str = "NUMERIC_COMPARISON"; break;
				case BLE_SM_IOACT_OOB_SC:		act_str = "OOB_SECURE_CONNECTION"; break;
				default:						act_str = "UNKNOWN"; break;
			}
			Serial.printf("action=%s (%d)\n", act_str, action);

			if (event->passkey.params.action == BLE_SM_IOACT_INPUT)
			{
				NimBLEServer* server = NimBLEDevice::getServer();
				if (!server)
				{
					Serial.print("no server available");
					return 0;
				}

				NimBLEConnInfo peerInfo = server->getPeerInfo(event->connect.conn_handle);
                Serial.printf("Enter the passkey:");
				// server->m_pServerCallbacks->onPassKeyEntry(peerInfo);
				// onPassKeyEntry(peerInfo);
            }

			break;
		}
	}

	return 0;
}
