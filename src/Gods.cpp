#include "Gods.h"

namespace IOD
{
	namespace
	{
		std::string Key(std::string_view a_text)
		{
			std::string out;
			for (char c : a_text) {
				if (std::isalnum(static_cast<unsigned char>(c))) {
					out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
				}
			}
			return out;
		}
	}

	std::optional<int> ParseGod(std::string_view a_text)
	{
		auto key = Key(a_text);
		if (key.starts_with("shrineof")) {
			key.erase(0, 8);
		}
		for (int i = 0; i < static_cast<int>(kGods.size()); ++i) {
			if (Key(kGods[static_cast<std::size_t>(i)]) == key) {
				return i;
			}
		}
		return std::nullopt;
	}

	const char* GodName(int a_god)
	{
		return a_god >= 0 && a_god < static_cast<int>(kGods.size()) ? kGods[static_cast<std::size_t>(a_god)] : "?";
	}
}
