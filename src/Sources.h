#pragma once

namespace IOD
{
	// Where a set can be obtained. Merchant sources are identified by the vendor faction of the
	// NPC you talk to, so NPCs that share a stock (Endarie and Taarie) are covered by one faction.
	enum class Source : std::uint8_t
	{
		RadiantRaiment,
		KhajiitCaravans,
		ThievesGuild,
	};

	inline constexpr std::array kAllSources{ Source::RadiantRaiment, Source::KhajiitCaravans, Source::ThievesGuild };

	std::string_view        SourceName(Source a_source);
	std::optional<Source>   ParseSource(std::string_view a_text);

	// Vendor factions per source, resolved after the game data is loaded. Order is fixed so the
	// stock allocation is deterministic.
	void                                    ResolveVendorFactions();
	const std::vector<RE::TESFaction*>&     VendorFactions(Source a_source);
	std::optional<Source>                   SourceForFaction(const RE::TESFaction* a_faction);
	std::string_view                        VendorLabel(const RE::TESFaction* a_faction);  // e.g. "Ri'saad's caravan"
}
