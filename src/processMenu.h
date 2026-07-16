// processMenu.h
#pragma once
#include <atomic>
#include <optional>
#include <cstdint>

enum class ConsoleMode { Uninitialized, Config, ConfigSub, Passkey, PinConfirm, SetMtu, SetAdvInterval, SetConnParams, SetDataLen, AddAdvInstance };

namespace ProcessMenu
{
	extern std::atomic<ConsoleMode> consoleMode;

	void setupProcessMenu();
	void printConfigMenu();
	void handleConfigInput();
	void handleConfigInputSub();
	std::optional<uint16_t> GetSelectedHandle();

	// Currently selected advertising instance (for W/L/V/connectable commands).
	// Always 0 on legacy builds (only instance available).
	uint8_t SelectedAdvInstance();
} // ProcessMenu
