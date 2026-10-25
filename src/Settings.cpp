#include "Settings.h"

namespace IOD
{
	namespace
	{
		constexpr auto kPath = "Data/SKSE/Plugins/ImmersiveOutfitDistribution.json"sv;
	}

	Settings& Settings::Get()
	{
		static Settings settings;
		return settings;
	}

	void Settings::Load()
	{
		std::ifstream in{ std::filesystem::path{ kPath } };
		if (!in) {
			spdlog::info("No settings file; using defaults");
			return;
		}
		try {
			const auto root = nlohmann::json::parse(in, nullptr, true, true);
			arrivalMessage = root.value("arrivalMessage", arrivalMessage);
			guaranteeOnePerVendor = root.value("guaranteeOnePerVendor", guaranteeOnePerVendor);
			spdlog::info("Settings: arrival message {}, at least one set per vendor {}", arrivalMessage, guaranteeOnePerVendor);
		} catch (const std::exception& e) {
			spdlog::error("Settings file is not valid JSON ({}); using defaults", e.what());
		}
	}

	bool Settings::Save() const
	{
		nlohmann::json root{
			{ "arrivalMessage", arrivalMessage },
			{ "guaranteeOnePerVendor", guaranteeOnePerVendor },
		};
		std::ofstream out{ std::filesystem::path{ kPath }, std::ios::trunc };
		if (!out) {
			spdlog::error("Could not write {}", kPath);
			return false;
		}
		out << root.dump(2) << '\n';
		spdlog::info("Settings saved");
		return true;
	}
}
