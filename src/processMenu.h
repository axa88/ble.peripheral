// processMenu.h
#pragma once
#include <atomic>
#include <optional>
#include <cstdint>

enum class ConsoleMode { Uninitialized, Config, ConfigSub, Passkey, PinConfirm, SetMtu, SetAdvInterval, SetConnParams, SetDataLen };

namespace ProcessMenu
{
	extern std::atomic<ConsoleMode> consoleMode;

	void setupProcessMenu();
	void printConfigMenu();
	void handleConfigInput();
	void handleConfigInputSub();
	std::optional<uint16_t> GetSelectedHandle();
} // ProcessMenu
