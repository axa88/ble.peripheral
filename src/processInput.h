// processInput.h
#pragma once
#include <cstdint>	// uint32_t
#include <atomic>	// std::atomic

namespace ProcessInput
{
	extern std::atomic<bool> actionInputReady;

	extern std::atomic<bool> passkeyReady;
	extern std::atomic<uint32_t> passkeyValue;
	extern std::atomic<bool> confirmReady;
	extern std::atomic<bool> confirmAccept;

	// Console interface
	void handlePairingInput();
}