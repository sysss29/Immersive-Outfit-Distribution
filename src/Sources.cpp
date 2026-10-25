#include "Sources.h"

namespace IOD
{
	namespace
	{
		struct VendorFactionID
		{
			RE::FormID       localID;
			std::string_view plugin;
			std::string_view label;
		};

		// Vendor factions verified against Skyrim.esm (see docs/game-data.md).
		constexpr std::array kRadiantRaiment{
			VendorFactionID{ 0x0A6C00, "Skyrim.esm"sv, "Endarie & Taarie (Solitude)"sv },  // ServicesSolitudeRadiantRaiments (Endarie, Taarie)
		};
		constexpr std::array kKhajiitCaravans{
			VendorFactionID{ 0x074351, "Skyrim.esm"sv, "Ri'saad's caravan"sv },  // CaravanALeader (Ri'saad)
			VendorFactionID{ 0x074352, "Skyrim.esm"sv, "Ma'dran's caravan"sv },  // CaravanBLeader (Ma'dran)
			VendorFactionID{ 0x074354, "Skyrim.esm"sv, "Ahkari's caravan"sv },  // CaravanCLeader (Ahkari)
		};
		constexpr std::array kThievesGuild{
			VendorFactionID{ 0x0D54C4, "Skyrim.esm"sv, "Tonilia (Riften)"sv },  // ServicesThievesGuildTonilia
			VendorFactionID{ 0x0D8E53, "Skyrim.esm"sv, "Atahbah (fence)"sv },  // ServicesThievesGuildCaravanAtahba
			VendorFactionID{ 0x0D8E52, "Skyrim.esm"sv, "Ma'jhad (fence)"sv },  // ServicesThievesGuildCaravanMajhad
			VendorFactionID{ 0x0D8E54, "Skyrim.esm"sv, "Zaynabi (fence)"sv },  // ServicesThievesGuildCaravanZaynabi
		};

		std::map<Source, std::vector<RE::TESFaction*>> g_factions;
		std::unordered_map<const RE::TESFaction*, std::string_view> g_labels;

		template <std::size_t N>
		void Resolve(Source a_source, const std::array<VendorFactionID, N>& a_ids)
		{
			auto& list = g_factions[a_source];
			list.clear();
			auto* data = RE::TESDataHandler::GetSingleton();
			for (const auto& id : a_ids) {
				auto* faction = data ? data->LookupForm<RE::TESFaction>(id.localID, id.plugin) : nullptr;
				if (faction) {
					list.push_back(faction);
					g_labels[faction] = id.label;
				} else {
					spdlog::warn("Vendor faction {:06X} in {} not found", id.localID, id.plugin);
				}
			}
		}

		std::string Normalise(std::string_view a_text)
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

	std::string_view VendorLabel(const RE::TESFaction* a_faction)
	{
		const auto it = g_labels.find(a_faction);
		return it != g_labels.end() ? it->second : "?"sv;
	}

	std::string_view SourceName(Source a_source)
	{
		switch (a_source) {
		case Source::RadiantRaiment:
			return "Radiant Raiment"sv;
		case Source::KhajiitCaravans:
			return "Khajiit caravans"sv;
		case Source::ThievesGuild:
			return "Thieves Guild"sv;
		}
		return "?"sv;
	}

	std::optional<Source> ParseSource(std::string_view a_text)
	{
		const auto key = Normalise(a_text);
		if (key == "radiantraiment") {
			return Source::RadiantRaiment;
		}
		if (key == "khajiitcaravans" || key == "khajiitcaravan" || key == "caravans") {
			return Source::KhajiitCaravans;
		}
		if (key == "thievesguild" || key == "fences") {
			return Source::ThievesGuild;
		}
		return std::nullopt;
	}

	void ResolveVendorFactions()
	{
		Resolve(Source::RadiantRaiment, kRadiantRaiment);
		Resolve(Source::KhajiitCaravans, kKhajiitCaravans);
		Resolve(Source::ThievesGuild, kThievesGuild);
	}

	const std::vector<RE::TESFaction*>& VendorFactions(Source a_source)
	{
		return g_factions[a_source];
	}

	std::optional<Source> SourceForFaction(const RE::TESFaction* a_faction)
	{
		if (!a_faction) {
			return std::nullopt;
		}
		for (const auto& [source, factions] : g_factions) {
			if (std::ranges::find(factions, a_faction) != factions.end()) {
				return source;
			}
		}
		return std::nullopt;
	}
}
