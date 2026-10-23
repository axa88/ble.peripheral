// processMenu.h
#pragma once
#include <atomic>
#include <optional>
#include <cstdint>
#include <cstddef>
#include "connectionId.h"

enum class ConsoleMode { Uninitialized, Config, Passkey, PinConfirm, SetMtu, SetAdvInterval, SetConnParams, SetDataLen, AddAdvInstance, SelectBondTarget, SelectAdvInstance, SelectPairingRequest };

namespace ProcessMenu
{
	extern std::atomic<ConsoleMode> consoleMode;

	void setupProcessMenu();
	void printConfigMenu();
	void handleConfigInput();
	std::optional<ConnectionId> GetSelectedConnectionId();
	bool SelectBondTarget(size_t index);
	bool SelectAdvInstance(size_t index);

	// Currently selected advertising instance.
	// Always 0 on legacy builds (only instance available).
	uint8_t SelectedAdvInstance();
} // ProcessMenu
