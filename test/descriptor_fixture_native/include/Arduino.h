#pragma once

#include <cstddef>
#include <cstdio>

struct NativeSerial
{
	template <typename... Args>
	void printf(const char* format, Args... args) const { std::printf(format, args...); }
	void println(const char* text) const { std::puts(text); }
};

inline NativeSerial Serial;
