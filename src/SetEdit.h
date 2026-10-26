#pragma once

#include "Gods.h"
#include "Sources.h"

// Set editing shared by the F1 page and the MCM: a Draft holds one set's fields, loaded from and
// saved to the set files. Callers hold Edit::Lock() while using SetFiles or a Draft.
namespace IOD::Edit
{
	inline constexpr std::array kRarities{ "Common", "Rare", "Legendary" };

	inline constexpr std::array kRequirementTypes{ "No requirement", "Character level", "Base Health", "Base Magicka", "Base Stamina",
		"Skill level", "Faction rank", "Quest completed", "Blessing of a god", "Has perk" };
	enum Req : int
	{
		kReqNone,
		kReqLevel,
		kReqHealth,
		kReqMagicka,
		kReqStamina,
		kReqSkill,
		kReqFaction,
		kReqQuest,
		kReqBlessing,
		kReqPerk
	};
	inline constexpr std::array kReqJsonTypes{ "", "level", "health", "magicka", "stamina", "skill", "faction", "quest", "blessing", "perk" };

	inline constexpr std::array kSkills{ "One-Handed", "Two-Handed", "Archery", "Block", "Smithing", "Heavy Armor", "Light Armor",
		"Pickpocket", "Lockpicking", "Sneak", "Alchemy", "Speech", "Alteration", "Conjuration", "Destruction", "Illusion",
		"Restoration", "Enchanting" };

	struct KnownFaction
	{
		const char* label;
		const char* ref;
	};
	inline constexpr std::array kFactions{
		KnownFaction{ "Thieves Guild", "Skyrim.esm|0x29DA9" },
		KnownFaction{ "Companions", "Skyrim.esm|0x48362" },
		KnownFaction{ "College of Winterhold", "Skyrim.esm|0x1F259" },
		KnownFaction{ "Dark Brotherhood", "Skyrim.esm|0x1BDB3" },
		KnownFaction{ "Imperial Legion", "Skyrim.esm|0x2BF9A" },
		KnownFaction{ "Stormcloaks", "Skyrim.esm|0x2BF9B" },
		KnownFaction{ "Dawnguard", "Dawnguard.esm|0x14217" },
		KnownFaction{ "Vampires (Volkihar)", "Dawnguard.esm|0x3376" },
		KnownFaction{ "Other (enter form ID)", "" },
	};

	// Quest milestones offered in the Quest completed list (editor IDs checked against Skyrim.esm);
	// the label shown adds the quest's in-game name.
	// Fields a set can take from its mod file's "defaults".
	enum Inherit : int
	{
		kInheritSources,  // where sold and shrines
		kInheritRarity,
		kInheritPrice,
		kInheritRequirement,
		kInheritCount
	};

	struct KnownQuest
	{
		const char* label;
		const char* ref;
	};
	inline constexpr std::array kQuests{
		KnownQuest{ "Main quest finale", "MQ305" },
		KnownQuest{ "Thieves Guild finale", "TG09" },
		KnownQuest{ "Thieves Guild leadership", "TGLeadership" },
		KnownQuest{ "Companions finale", "C06" },
		KnownQuest{ "College of Winterhold finale", "MG08" },
		KnownQuest{ "Dark Brotherhood finale", "DB11" },
		KnownQuest{ "Other (enter editor ID or form)", "" },
	};
	inline constexpr int kQuestOther = static_cast<int>(kQuests.size()) - 1;
	std::string QuestLabel(int a_index);  // "Thieves Guild finale: Darkness Returns"

	// Perks of a skill tree (index into kSkills), in tree order, every rank listed. Read from the
	// game, so perk overhauls (e.g. Paths of Empyrean) show their own perks.
	std::vector<RE::BGSPerk*> PerksOfTree(int a_skill);
	std::string               PerkLabel(const RE::BGSPerk* a_perk);

	inline constexpr int kFactionOther = static_cast<int>(kFactions.size()) - 1;


	template <std::size_t N>
	void CopyTo(char (&a_buffer)[N], std::string_view a_text)
	{
		const auto n = std::min(a_text.size(), N - 1);
		std::memcpy(a_buffer, a_text.data(), n);
		a_buffer[n] = '\0';
	}

	struct Draft
	{
		bool                     active{ false };
		std::size_t              file{ 0 };
		std::size_t              index{ SIZE_MAX };  // SIZE_MAX: new, not yet saved
		nlohmann::json           original;           // unknown fields are kept
		char                     name[128]{};
		bool                     enabled{ true };
		bool                     unique{ true };
		std::string              outfit;
		std::vector<std::string> pieces;
		std::array<bool, kAllSources.size()> sources{};
		std::array<bool, kGods.size()> gods{};  // shrines that grant it
		bool                     ownOffering{ false };  // false: the god's default for the rarity
		struct OfferingLine
		{
			std::vector<std::string> refs;  // alternatives
			int                      count{ 1 };
		};
		std::vector<OfferingLine> offering;
		int                      rarity{ 0 };
		bool                     hasPrice{ false };
		int                      price{ 1000 };
		int                      reqType{ kReqNone };
		int                      reqValue{ 0 };
		int                      reqSkill{ 0 };
		int                      reqFaction{ 0 };
		int                      reqGod{ 0 };
		int                      reqQuest{ 0 };
		int                      reqPerk{ 0 };  // index in PerksOfTree(reqSkill); -1: other (reqRef)
		char                     reqRef[128]{};
		std::array<bool, kInheritCount> inherit{};  // true: the value shown comes from the mod defaults
		bool                     isDefaults{ false };  // editing a file's defaults, not a set
		std::array<int, 3>       priceByRarity{ 1000, 2500, 6000 };  // defaults only
		bool                     dirty{ false };
		bool                     confirmDelete{ false };
		std::string              status;  // last save result
	};

	struct Resolved
	{
		std::vector<RE::TESObjectARMO*> pieces;
		std::size_t                     missing{ 0 };
	};

	std::recursive_mutex& Lock();

	std::string Normalise(std::string_view a_text);
	bool        Contains(std::string_view a_haystack, std::string_view a_needle);
	std::string Slots(const RE::TESObjectARMO* a_armor);
	const char* Kind(RE::TESObjectARMO* a_armor);  // "clothing" or "armour"

	std::vector<RE::TESObjectARMO*> OutfitPieces(const RE::BGSOutfit* a_outfit);
	std::string                     OutfitLabel(const RE::BGSOutfit* a_outfit);

	Resolved                            Resolve(const std::string& a_outfit, const std::vector<std::string>& a_pieces);
	Resolved                            Resolve(const nlohmann::json& a_set);
	std::pair<std::size_t, std::size_t> TradeCount(const std::vector<RE::TESObjectARMO*>& a_pieces, Source a_source);
	std::string                         SourceWarning(const std::vector<RE::TESObjectARMO*>& a_pieces, Source a_source);
	int                                 MaxFactionRank(const Draft& a_draft);  // -1: faction not found

	RE::BGSPerk*   DraftPerk(const Draft& a_draft);  // the chosen perk, or null
	std::string    OfferingText(const Draft& a_draft, int a_god);  // what this god asks for the set
	void           LoadDraft(Draft& a_draft, std::size_t a_file, std::size_t a_index);
	void           LoadDraftFrom(Draft& a_draft, std::size_t a_file, std::size_t a_index, const nlohmann::json& a_raw);
	void           ParseInto(Draft& a_draft, const nlohmann::json& a_entry);
	nlohmann::json Effective(const nlohmann::json& a_defaults, const nlohmann::json& a_entry, std::array<bool, kInheritCount>* a_inherited);
	void           UseDefault(Draft& a_draft, Inherit a_field);  // back to the mod's default for that field
	bool           FileHasDefault(std::size_t a_file, Inherit a_field);

	// A file's defaults, edited like a set (isDefaults).
	void           LoadDefaults(Draft& a_draft, std::size_t a_file);
	bool           SaveDefaults(Draft& a_draft);

	// One set per outfit record of the mod, into the mod's file; sets that exist are skipped.
	// Gives the file defaults if it has none (sold by kind, Common). Returns how many were made.
	std::size_t    ImportOutfits(const std::vector<RE::BGSOutfit*>& a_outfits, std::string& a_status);
	void           NewDraft(Draft& a_draft, std::string a_name, std::string a_outfit, std::vector<std::string> a_pieces);
	nlohmann::json DraftToJson(Draft& a_draft);
	bool           SaveDraft(Draft& a_draft);  // writes the file and reloads the catalog in game
	void           DeleteDraft(Draft& a_draft);
	std::string    ModStemOf(const Draft& a_draft);          // file stem for the set's armour mod
	bool           MoveDraftToFile(Draft& a_draft, std::size_t a_file);
	std::size_t    SplitByMod();                             // moves every set to its mod's file; returns how many moved

	// Items the shrine registry uses in offerings, for the offering editor: (label, refs).
	std::vector<std::pair<std::string, std::vector<std::string>>> KnownOfferingItems();
	void           ApplyChanges();

	// All sets across files, in display order, as (file, index) pairs.
	std::vector<std::pair<std::size_t, std::size_t>> AllSets();
}
