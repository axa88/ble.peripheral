// processMenu.h
#pragma once
#include <atomic>

enum class ConsoleMode { Uninitialized, Config, ConfigSub, Passkey, PinConfirm };

namespace ProcessMenu
{
	extern std::atomic<ConsoleMode> consoleMode;

	void setupProcessMenu();
	void printConfigMenu();
	void handleConfigInput();
	void handleConfigInputSub();
} // ProcessMenu