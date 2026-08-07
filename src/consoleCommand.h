#pragma once

#include <cstddef>
#include <string_view>

namespace ConsoleCommand
{
	enum class Group
	{
		Advertising,
		Security,
		Connection,
		Other
	};

	using Handler = void (*)(char command);

	struct Descriptor
	{
		char firstKey;
		char lastKey;
		const char* keyText;
		Group group;
		std::string_view description;
		bool extendedOnly;
		Handler handler;

		bool matches(char command) const
		{
			return command >= firstKey && command <= lastKey;
		}
	};

	const Descriptor* Registry(std::size_t& count);
	const Descriptor* Find(char command);
}
