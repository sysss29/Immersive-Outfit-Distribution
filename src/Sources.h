#pragma once

namespace IOD
{
	// Where a set can be bought. Merchant sources are identified by the vendor faction of the NPC
	// you talk to, so NPCs that share a stock (Endarie and Taarie, Ghorza and Tacitus...) are one
	// source. Values are stored nowhere but in this order: append new sources at the end.
	enum class Source : std::uint8_t
	{
		RadiantRaiment,
		KhajiitCaravans,
		ThievesGuild,
		Eorlund,
		DremoraMerchant,
		Hestla,
		Gunmar,
		Baldor,
		TelMithryn,
		Sybille,
		EastEmpireCompany,
		Ghorza,
		Dushnamub,
		Niranye,
	};

	inline constexpr std::array kAllSources{ Source::RadiantRaiment, Source::KhajiitCaravans, Source::ThievesGuild, Source::Eorlund,
		Source::DremoraMerchant, Source::Hestla, Source::Gunmar, Source::Baldor, Source::TelMithryn, Source::Sybille,
		Source::EastEmpireCompany, Source::Ghorza, Source::Dushnamub, Source::Niranye };

	std::string_view      SourceName(Source a_source);    // as written in set files, e.g. "Eorlund Gray-Mane"
	std::string_view      SourceWhere(Source a_source);   // e.g. "Skyforge, Whiterun"
	std::optional<Source> ParseSource(std::string_view a_text);

	// Vendor factions per source, resolved after the game data is loaded. Order is fixed so the
	// stock allocation is deterministic.
	void                                    ResolveVendorFactions();
	const std::vector<RE::TESFaction*>&     VendorFactions(Source a_source);
	std::optional<Source>                   SourceForFaction(const RE::TESFaction* a_faction);
	std::string_view                        VendorLabel(const RE::TESFaction* a_faction);  // e.g. "Ri'saad's caravan"
}
