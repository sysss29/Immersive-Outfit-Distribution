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

	struct OutfitSet
	{
		std::string                      id;    // "<file stem>/<name>", unique, used in the save
		std::string                      name;
		std::vector<RE::TESObjectARMO*>  pieces;
		std::vector<Source>              sources;
		Rarity                           rarity{ Rarity::kCommon };
		std::optional<std::int32_t>      price;  // gold for the whole set
		Requirement                      requirement;
		bool                             unique{ true };

		[[nodiscard]] bool HasSource(Source a_source) const;
		[[nodiscard]] bool PlayerHasAllPieces(RE::PlayerCharacter* a_player) const;
	};

	// Loads every *.json file in Data/SKSE/Plugins/ImmersiveOutfitDistribution/. Sets whose
	// armour mod isn't installed are skipped with a log line, never an error.
	class Catalog
	{
	public:
		static Catalog& GetSingleton();

		void Load();

		[[nodiscard]] const std::vector<OutfitSet>& Sets() const { return _sets; }
		[[nodiscard]] const OutfitSet*              Find(std::string_view a_id) const;

	private:
		void LoadFile(const std::filesystem::path& a_path);
		void ApplyPrices();

		std::vector<OutfitSet> _sets;
	};

	// "Skyrim.esm|0x12EB7" or "0x12EB7~Skyrim.esm" (local form ID, hex). Works with ESL plugins.
	RE::TESForm* LookupFormString(std::string_view a_text);
}
