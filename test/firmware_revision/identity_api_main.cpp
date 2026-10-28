#include "firmwareIdentity.h"

#include <cstring>

int main()
{
	if (FirmwareIdentity::revision() != 73)
		return 1;
	if (std::strcmp(FirmwareIdentity::sourceId(), "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") != 0)
		return 2;
	if (std::strcmp(FirmwareIdentity::environment(), "unit-test") != 0)
		return 3;
	if (std::strcmp(FirmwareIdentity::summary(), "Firmware revision 73 | source aaaaaaaaaaaa | environment unit-test") != 0)
		return 4;
	return 0;
}
