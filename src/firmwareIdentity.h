#pragma once

#include <cstdint>

namespace FirmwareIdentity
{
	uint32_t revision();
	const char* sourceId();
	const char* environment();
	const char* summary();
}
