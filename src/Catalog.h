#pragma once

#include "Requirements.h"
#include "Sources.h"

namespace IOD
{
	enum class Rarity : std::uint8_t
	{
		kCommon,
		kRare,
		kLegendary,
	};

	// One line of an offering: any mix of the listed items, count in total (e.g. 3 wine, of
	// either kind of wine bottle). Gold is an item too (Skyrim.esm|0xF).
	struct OfferingItem
	{
		std::vector<RE::TESBoundObject*> any;
		std::int32_t                     count{ 1 };
	};
	using Offering = std::vector<OfferingItem>;
	std::optional<Offering> ParseOffering(const nlohmann::json& a_json, std::string_view a_context);
	std::string             DescribeOffering(const Offering& a_offering);  // "Flawless Garnet, 250 gold"

	struct OutfitSet
	{
		std::string                      id;    // "<file stem>/<name>", unique, used in the save
		std::string                      name;
		std::vector<RE::TESObjectARMO*>  pieces;
		std::vector<Source>              sources;
		std::vector<int>                 gods;      // shrines that grant it (Gods.h)
		Offering                         offering;  // empty: the god's default for the rarity
		Rarity                           rarity{ Rarity::kCommon };
		std::optional<std::int32_t>      price;  // gold for the whole set
		Requirement                      requirement;
		bool                             unique{ true };

		[[nodiscard]] bool HasSource(Source a_source) const;
		[[nodiscard]] bool HasGod(int a_god) const { return std::ranges::find(gods, a_god) != gods.end(); }
		[[nodiscard]] bool PlayerHasAllPieces(RE::PlayerCharacter* a_player) const;
	};

	// Loads every *.json file in Data/SKSE/Plugins/ImmersiveOutfitDistribution/. Sets whose
	// armour mod isn't installed are skipped with a log line, never an error.
	class Catalog
	{
	public:
		static Catalog& GetSingleton();

		void Load();  // also used to reload after the settings page saves

		[[nodiscard]] const std::vector<OutfitSet>& Sets() const { return _sets; }
		[[nodiscard]] const OutfitSet*              Find(std::string_view a_id) const;

	private:
		void LoadFile(const std::filesystem::path& a_path);
		void ApplyPrices();

		std::vector<OutfitSet> _sets;
	};

	// "Skyrim.esm|0x12EB7" or "0x12EB7~Skyrim.esm" (local form ID, hex). Works with ESL plugins.
	RE::TESForm* LookupFormString(std::string_view a_text);
	std::string  FormString(const RE::TESForm* a_form);  // the reverse: "Plugin.esp|0xID"
}
