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

		struct SourceDef
		{
			Source                           source;
			std::string_view                 name;
			std::string_view                 where;
			std::vector<std::string_view>    aliases;  // normalised (lowercase letters and digits)
			std::vector<VendorFactionID>     factions;
		};

		// Vendor factions verified against Skyrim.esm, Dawnguard.esm and Dragonborn.esm (see
		// docs/game-data.md). A faction missing from the game (e.g. a DLC) is skipped.
		const std::vector<SourceDef>& Defs()
		{
			static const std::vector<SourceDef> defs{
				{ Source::RadiantRaiment, "Radiant Raiment", "Solitude", { "radiantraiment" },
					{ { 0x0A6C00, "Skyrim.esm", "Endarie & Taarie (Solitude)" } } },
				{ Source::KhajiitCaravans, "Khajiit caravans", "on the roads", { "khajiitcaravans", "khajiitcaravan", "caravans" },
					{ { 0x074351, "Skyrim.esm", "Ri'saad's caravan" }, { 0x074352, "Skyrim.esm", "Ma'dran's caravan" },
						{ 0x074354, "Skyrim.esm", "Ahkari's caravan" } } },
				{ Source::ThievesGuild, "Thieves Guild", "Ragged Flagon and caravan fences", { "thievesguild", "fences" },
					{ { 0x0D54C4, "Skyrim.esm", "Tonilia (Riften)" }, { 0x0D8E53, "Skyrim.esm", "Atahbah (fence)" },
						{ 0x0D8E52, "Skyrim.esm", "Ma'jhad (fence)" }, { 0x0D8E54, "Skyrim.esm", "Zaynabi (fence)" },
						{ 0x0D8845, "Skyrim.esm", "Gulum-Ei (Solitude fence)" }, { 0x0D8842, "Skyrim.esm", "Endon (Markarth fence)" },
						{ 0x0D8846, "Skyrim.esm", "Mallus Maccius (Whiterun fence)" } } },
				{ Source::Eorlund, "Eorlund Gray-Mane", "Skyforge, Whiterun", { "eorlundgraymane", "eorlund", "skyforge" },
					{ { 0x10FDE8, "Skyrim.esm", "Eorlund Gray-Mane (Skyforge)" } } },
				{ Source::DremoraMerchant, "Dremora Merchant", "summoned (Dragonborn)", { "dremoramerchant", "dremora" },
					{ { 0x01EEC4, "Dragonborn.esm", "Dremora Merchant" } } },
				{ Source::Hestla, "Hestla", "Castle Volkihar (Dawnguard)", { "hestla", "volkihar", "castlevolkihar" },
					{ { 0x01047E, "Dawnguard.esm", "Hestla (Castle Volkihar)" } } },
				{ Source::Gunmar, "Gunmar", "Fort Dawnguard (Dawnguard)", { "gunmar", "fortdawnguard", "dawnguard" },
					{ { 0x00F827, "Dawnguard.esm", "Gunmar (Fort Dawnguard)" } } },
				{ Source::Baldor, "Baldor Iron-Shaper", "Skaal Village (Dragonborn)", { "baldorironshaper", "baldor", "skaal" },
					{ { 0x01F899, "Dragonborn.esm", "Baldor Iron-Shaper (Skaal Village)" } } },
				{ Source::TelMithryn, "Tel Mithryn", "Neloth and Talvas (Dragonborn)", { "telmithryn", "neloth", "talvas" },
					{ { 0x0177C7, "Dragonborn.esm", "Neloth (Tel Mithryn)" }, { 0x0177C6, "Dragonborn.esm", "Talvas (Tel Mithryn)" } } },
				{ Source::Sybille, "Sybille Stentor", "Blue Palace, Solitude", { "sybillestentor", "sybille" },
					{ { 0x0B2036, "Skyrim.esm", "Sybille Stentor (Blue Palace)" } } },
				{ Source::EastEmpireCompany, "East Empire Company", "Vittoria Vici, Solitude docks", { "eastempirecompany", "eec" },
					{ { 0x0D6AC5, "Skyrim.esm", "East Empire Company (Vittoria Vici)" } } },
				{ Source::Ghorza, "Ghorza gra-Bagol", "Markarth", { "ghorzagrabagol", "ghorza" },
					{ { 0x094390, "Skyrim.esm", "Ghorza gra-Bagol (Markarth)" } } },
				{ Source::Dushnamub, "Dushnamub", "Narzulbur", { "dushnamub", "narzulbur" },
					{ { 0x0B3FE3, "Skyrim.esm", "Dushnamub (Narzulbur)" } } },
				{ Source::Niranye, "Niranye", "Windhelm market", { "niranye" },
					{ { 0x0A3F07, "Skyrim.esm", "Niranye (Windhelm)" } } },
			};
			return defs;
		}

		const SourceDef* Def(Source a_source)
		{
			for (const auto& def : Defs()) {
				if (def.source == a_source) {
					return &def;
				}
			}
			return nullptr;
		}

		std::map<Source, std::vector<RE::TESFaction*>>              g_factions;
		std::unordered_map<const RE::TESFaction*, std::string_view> g_labels;

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
		const auto* def = Def(a_source);
		return def ? def->name : "?"sv;
	}

	std::string_view SourceWhere(Source a_source)
	{
		const auto* def = Def(a_source);
		return def ? def->where : ""sv;
	}

	std::optional<Source> ParseSource(std::string_view a_text)
	{
		const auto key = Normalise(a_text);
		for (const auto& def : Defs()) {
			if (key == Normalise(def.name) || std::ranges::find(def.aliases, key) != def.aliases.end()) {
				return def.source;
			}
		}
		return std::nullopt;
	}

	void ResolveVendorFactions()
	{
		g_factions.clear();
		g_labels.clear();
		auto* data = RE::TESDataHandler::GetSingleton();
		for (const auto& def : Defs()) {
			auto& list = g_factions[def.source];
			for (const auto& id : def.factions) {
				auto* faction = data ? data->LookupForm<RE::TESFaction>(id.localID, id.plugin) : nullptr;
				if (faction) {
					list.push_back(faction);
					g_labels[faction] = id.label;
				} else {
					spdlog::info("{}: vendor faction {:06X} in {} not loaded", def.name, id.localID, id.plugin);
				}
			}
		}
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
