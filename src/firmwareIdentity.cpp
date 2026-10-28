#include "firmwareIdentity.h"

#include "firmware_identity.generated.h"

namespace FirmwareIdentity
{
	uint32_t revision()
	{
		return BLE_FIRMWARE_REVISION;
	}

	const char* sourceId()
	{
		return BLE_FIRMWARE_SOURCE_ID;
	}

	const char* environment()
	{
		return BLE_FIRMWARE_ENVIRONMENT;
	}

	const char* summary()
	{
		return BLE_FIRMWARE_SUMMARY;
	}
}
