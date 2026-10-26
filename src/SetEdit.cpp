#include "SetEdit.h"

#include "Catalog.h"
#include "SetFiles.h"
#include "Shrines.h"
#include "Stocker.h"

namespace IOD::Edit
{
	std::recursive_mutex& Lock()
	{
		static std::recursive_mutex lock;
		return lock;
	}

	std::vector<std::pair<std::size_t, std::size_t>> AllSets()
	{
		std::vector<std::pair<std::size_t, std::size_t>> out;
		auto&                                            files = SetFiles::Get().Files();
		for (std::size_t f = 0; f < files.size(); ++f) {
			for (std::size_t i = 0; i < files[f].Sets().size(); ++i) {
				out.emplace_back(f, i);
			}
		}
		return out;
	}

	std::string QuestLabel(int a_index)
	{
		const auto& known = kQuests[static_cast<std::size_t>(std::clamp(a_index, 0, kQuestOther))];
		if (a_index >= kQuestOther) {
			return known.label;
		}
		auto* form = LookupFormString(known.ref);
		auto* quest = form ? form->As<RE::TESQuest>() : nullptr;
		const char* name = quest ? quest->GetName() : nullptr;
		return name && *name ? fmt::format("{}: {}", known.label, name) : fmt::format("{} ({})", known.label, known.ref);
	}

	std::vector<RE::BGSPerk*> PerksOfTree(int a_skill)
	{
		std::vector<RE::BGSPerk*> perks;
		const auto av = static_cast<RE::ActorValue>(static_cast<int>(RE::ActorValue::kOneHanded) + std::clamp(a_skill, 0, 17));
		auto*      info = RE::ActorValueList::GetActorValueInfo(av);
		if (!info || !info->perkTree) {
			return perks;
		}
		std::vector<RE::BGSSkillPerkTreeNode*>        queue{ info->perkTree };
		std::unordered_set<RE::BGSSkillPerkTreeNode*> seen;
		for (std::size_t i = 0; i < queue.size(); ++i) {
			auto* node = queue[i];
			if (!node || !seen.insert(node).second) {
				continue;
			}
			for (auto* perk = node->perk; perk && std::ranges::find(perks, perk) == perks.end(); perk = perk->nextPerk) {
				perks.push_back(perk);
			}
			for (auto* child : node->children) {
				queue.push_back(child);
			}
		}
		return perks;
	}

	std::string PerkLabel(const RE::BGSPerk* a_perk)
	{
		const char* name = a_perk ? a_perk->GetName() : nullptr;
		return name && *name ? std::string(name) : FormString(a_perk);
	}

	RE::BGSPerk* DraftPerk(const Draft& a_draft)
	{
		if (a_draft.reqPerk >= 0) {
			const auto perks = PerksOfTree(a_draft.reqSkill);
			if (static_cast<std::size_t>(a_draft.reqPerk) < perks.size()) {
				return perks[static_cast<std::size_t>(a_draft.reqPerk)];
			}
		}
		auto* form = LookupFormString(a_draft.reqRef);
		return form ? form->As<RE::BGSPerk>() : nullptr;
	}

	std::string OfferingText(const Draft& a_draft, int a_god)
	{
		if (a_draft.ownOffering && !a_draft.offering.empty()) {
			auto lines = nlohmann::json::array();
			for (const auto& line : a_draft.offering) {
				lines.push_back({ { "item", line.refs }, { "count", line.count } });
			}
			if (const auto own = ParseOffering(lines, "editor")) {
				return DescribeOffering(*own) + " (this set's own)";
			}
			return "this set's own offering has an item that isn't loaded";
		}
		const auto& offering = Shrines::GetSingleton()->DefaultOffering(a_god, static_cast<Rarity>(std::clamp(a_draft.rarity, 0, 2)));
		return offering.empty() ? std::string("no offering defined") : DescribeOffering(offering);
	}

	std::string ModStemOf(const Draft& a_draft)
	{
		const auto resolved = Resolve(a_draft.outfit, a_draft.pieces);
		const auto* file = resolved.pieces.empty() ? nullptr : resolved.pieces.front()->GetFile(0);
		return file ? SetFiles::StemForPlugin(file->GetFilename()) : std::string("MyOutfits");
	}

	bool MoveDraftToFile(Draft& a_draft, std::size_t a_file)
	{
		auto& store = SetFiles::Get();
		if (a_draft.index == SIZE_MAX || a_file == a_draft.file || a_file >= store.Files().size()) {
			a_draft.file = a_file < store.Files().size() ? a_file : a_draft.file;
			return true;
		}
		auto&      target = store.Files()[a_file].Sets();
		const auto name = std::string(a_draft.name);
		if (std::ranges::any_of(target, [&](const nlohmann::json& a_set) { return a_set.value("name", "") == name; })) {
			a_draft.status = fmt::format("{}.json already has a set called \"{}\".", store.Files()[a_file].stem, name);
			return false;
		}
		auto& source = store.Files()[a_draft.file].Sets();
		target.push_back(DraftToJson(a_draft));
		source.erase(source.begin() + static_cast<std::ptrdiff_t>(a_draft.index));
		const auto from = a_draft.file;
		store.Save(a_file);
		store.Save(from);
		a_draft.file = a_file;
		a_draft.index = target.size() - 1;
		a_draft.original = target.back();
		a_draft.dirty = false;
		a_draft.status = fmt::format("Moved to {}.json.", store.Files()[a_file].stem);
		ApplyChanges();
		return true;
	}

	std::size_t SplitByMod()
	{
		auto&       store = SetFiles::Get();
		std::size_t moved = 0;
		for (bool again = true; again;) {
			again = false;
			for (const auto& [f, i] : AllSets()) {
				Draft d;
				LoadDraft(d, f, i);
				const auto stem = ModStemOf(d);
				if (stem == store.Files()[f].stem || stem == "MyOutfits") {
					continue;
				}
				if (MoveDraftToFile(d, store.FileFor(stem))) {
					++moved;
					again = true;  // indices changed: start over
					break;
				}
			}
		}
		return moved;
	}

	std::vector<std::pair<std::string, std::vector<std::string>>> KnownOfferingItems()
	{
		std::vector<std::pair<std::string, std::vector<std::string>>> items;
		std::set<std::string>                                         seen;
		for (int god = 0; god < static_cast<int>(kGods.size()); ++god) {
			for (const auto rarity : { Rarity::kCommon, Rarity::kRare, Rarity::kLegendary }) {
				for (const auto& line : Shrines::GetSingleton()->DefaultOffering(god, rarity)) {
					std::vector<std::string> refs;
					for (auto* object : line.any) {
						refs.push_back(FormString(object));
					}
					auto* first = line.any.front();
					std::string label = first->GetFormID() == 0xF ? "Gold" : first->GetName();
					if (line.any.size() > 1 && first->GetFormID() != 0xF) {
						label += " (or similar)";
					}
					if (seen.insert(label).second) {
						items.emplace_back(label, std::move(refs));
					}
				}
			}
		}
		std::ranges::sort(items, {}, &std::pair<std::string, std::vector<std::string>>::first);
		return items;
	}

	int MaxFactionRank(const Draft& a_draft)
	{
		const char* ref = a_draft.reqFaction == kFactionOther ? a_draft.reqRef : kFactions[static_cast<std::size_t>(a_draft.reqFaction)].ref;
		auto*       form = LookupFormString(ref);
		auto*       faction = form ? form->As<RE::TESFaction>() : nullptr;
		if (!faction) {
			return -1;
		}
		int ranks = 0;
		for ([[maybe_unused]] auto* rank : faction->rankData) {
			++ranks;
		}
		return std::max(0, ranks - 1);
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

	bool Contains(std::string_view a_haystack, std::string_view a_needle)
	{
		if (a_needle.empty()) {
			return true;
		}
		const auto lower = [](std::string_view a_s) {
			std::string out(a_s);
			std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		};
		return lower(a_haystack).find(lower(a_needle)) != std::string::npos;
	}

	std::string Slots(const RE::TESObjectARMO* a_armor)
	{
		const auto  mask = static_cast<std::uint32_t>(a_armor->GetSlotMask().underlying());
		std::string out;
		for (int i = 0; i < 32; ++i) {
			if (mask & (1u << i)) {
				out += (out.empty() ? "" : " ") + std::to_string(i + 30);
			}
		}
		return out;
	}

	RE::TESFaction* RadiantRaimentFaction()
	{
		const auto& list = VendorFactions(Source::RadiantRaiment);
		return list.empty() ? nullptr : list.front();
	}

	const char* Kind(RE::TESObjectARMO* a_armor)
	{
		// What Radiant Raiment thinks of it: that's the only vendor with a restrictive list.
		return Stocker::IsClothingLike(a_armor) ? "clothing" : "armour";
	}


	std::vector<RE::TESObjectARMO*> OutfitPieces(const RE::BGSOutfit* a_outfit)
	{
		std::vector<RE::TESObjectARMO*> pieces;
		if (a_outfit) {
			for (auto* item : a_outfit->outfitItems) {
				if (auto* armor = item ? item->As<RE::TESObjectARMO>() : nullptr) {
					if (std::ranges::find(pieces, armor) == pieces.end()) {
						pieces.push_back(armor);
					}
				}
			}
		}
		return pieces;
	}

	std::string OutfitLabel(const RE::BGSOutfit* a_outfit)
	{
		const char* edid = a_outfit->GetFormEditorID();
		if (edid && *edid) {
			return edid;
		}
		return fmt::format("Outfit {}", FormString(a_outfit));
	}


	// --- a set being edited -----------------------------------------------------------

	Resolved Resolve(const std::string& a_outfit, const std::vector<std::string>& a_pieces)
	{
		Resolved out;
		if (!a_outfit.empty()) {
			auto* form = LookupFormString(a_outfit);
			if (auto* outfit = form ? form->As<RE::BGSOutfit>() : nullptr) {
				out.pieces = OutfitPieces(outfit);
			} else {
				++out.missing;
			}
		}
		for (const auto& ref : a_pieces) {
			auto* form = LookupFormString(ref);
			auto* armor = form ? form->As<RE::TESObjectARMO>() : nullptr;
			if (!armor) {
				++out.missing;
			} else if (std::ranges::find(out.pieces, armor) == out.pieces.end()) {
				out.pieces.push_back(armor);
			}
		}
		return out;
	}

	Resolved Resolve(const nlohmann::json& a_set)
	{
		std::vector<std::string> pieces;
		if (a_set.contains("pieces") && a_set["pieces"].is_array()) {
			for (const auto& p : a_set["pieces"]) {
				if (p.is_string()) {
					pieces.push_back(p.get<std::string>());
				}
			}
		}
		return Resolve(a_set.value("outfit", ""), pieces);
	}

	// How many of a source's vendors can trade these pieces.
	std::pair<std::size_t, std::size_t> TradeCount(const std::vector<RE::TESObjectARMO*>& a_pieces, Source a_source)
	{
		const auto& factions = VendorFactions(a_source);
		std::size_t ok = 0;
		for (auto* faction : factions) {
			ok += Stocker::CanTradePieces(a_pieces, faction) ? 1 : 0;
		}
		return { ok, factions.size() };
	}

	std::string SourceWarning(const std::vector<RE::TESObjectARMO*>& a_pieces, Source a_source)
	{
		if (a_pieces.empty()) {
			return {};
		}
		const auto [ok, total] = TradeCount(a_pieces, a_source);
		if (ok == 0) {
			return a_source == Source::RadiantRaiment ?
			           "Radiant Raiment only sells clothing and jewellery: this set will never appear there." :
			           fmt::format("No {} vendor trades this kind of item: it will never appear there.", SourceName(a_source));
		}
		if (ok < total) {
			return fmt::format("Only {} of {} vendors trade this kind of item.", ok, total);
		}
		return {};
	}

	void LoadDraft(Draft& a_draft, std::size_t a_file, std::size_t a_index)
	{
		auto& files = SetFiles::Get().Files();
		const auto& entry = files[a_file].Sets()[a_index];
		a_draft = Draft{};
		a_draft.active = true;
		a_draft.file = a_file;
		a_draft.index = a_index;
		a_draft.original = entry;
		CopyTo(a_draft.name, entry.value("name", ""));
		a_draft.enabled = entry.value("enabled", true);
		a_draft.unique = entry.value("unique", true);
		a_draft.outfit = entry.value("outfit", "");
		if (entry.contains("pieces") && entry["pieces"].is_array()) {
			for (const auto& p : entry["pieces"]) {
				if (p.is_string()) {
					a_draft.pieces.push_back(p.get<std::string>());
				}
			}
		}
		if (entry.contains("sources") && entry["sources"].is_array()) {
			for (const auto& s : entry["sources"]) {
				if (const auto source = s.is_string() ? ParseSource(s.get<std::string>()) : std::nullopt) {
					a_draft.sources[static_cast<std::size_t>(*source)] = true;
				} else if (const auto god = s.is_string() ? ParseGod(s.get<std::string>()) : std::nullopt) {
					a_draft.gods[static_cast<std::size_t>(*god)] = true;
				}
			}
		}
		if (entry.contains("offering") && entry["offering"].is_array()) {
			a_draft.ownOffering = true;
			for (const auto& line : entry["offering"]) {
				Draft::OfferingLine item;
				item.count = std::max(1, line.value("count", 1));
				const auto refs = line.contains("item") ? line["item"] : nlohmann::json{};
				for (const auto& ref : refs.is_array() ? refs : nlohmann::json::array({ refs })) {
					if (ref.is_string()) {
						item.refs.push_back(ref.get<std::string>());
					}
				}
				a_draft.offering.push_back(std::move(item));
			}
		}
		const auto rarity = Normalise(entry.value("rarity", "common"));
		a_draft.rarity = rarity == "rare" ? 1 : rarity == "legendary" ? 2 : 0;
		if (entry.contains("price") && entry["price"].is_number_integer()) {
			a_draft.hasPrice = true;
			a_draft.price = entry["price"].get<int>();
		}
		if (entry.contains("requirement") && entry["requirement"].is_object()) {
			const auto& req = entry["requirement"];
			const auto  type = Normalise(req.value("type", ""));
			for (int i = 1; i < static_cast<int>(kReqJsonTypes.size()); ++i) {
				if (type == kReqJsonTypes[i]) {
					a_draft.reqType = i;
				}
			}
			a_draft.reqValue = req.value("value", req.value("rank", 0));
			if (a_draft.reqType == kReqSkill) {
				const auto skill = Normalise(req.value("skill", ""));
				for (int i = 0; i < static_cast<int>(kSkills.size()); ++i) {
					if (Normalise(kSkills[i]) == skill) {
						a_draft.reqSkill = i;
					}
				}
			} else if (a_draft.reqType == kReqFaction) {
				const auto ref = req.value("faction", "");
				auto*      form = LookupFormString(ref);
				a_draft.reqFaction = kFactionOther;
				for (int i = 0; i < kFactionOther; ++i) {
					if (form && LookupFormString(kFactions[i].ref) == form) {
						a_draft.reqFaction = i;
					}
				}
				CopyTo(a_draft.reqRef, ref);
			} else if (a_draft.reqType == kReqQuest) {
				const auto ref = req.value("quest", "");
				auto*      form = LookupFormString(ref);
				a_draft.reqQuest = kQuestOther;
				for (int i = 0; i < kQuestOther; ++i) {
					if (form && LookupFormString(kQuests[static_cast<std::size_t>(i)].ref) == form) {
						a_draft.reqQuest = i;
					}
				}
				CopyTo(a_draft.reqRef, ref);
			} else if (a_draft.reqType == kReqPerk) {
				const auto ref = req.value("perk", "");
				auto*      form = LookupFormString(ref);
				auto*      perk = form ? form->As<RE::BGSPerk>() : nullptr;
				a_draft.reqPerk = -1;
				CopyTo(a_draft.reqRef, ref);
				for (int skill = 0; perk && skill < static_cast<int>(kSkills.size()) && a_draft.reqPerk < 0; ++skill) {
					const auto perks = PerksOfTree(skill);
					if (const auto it = std::ranges::find(perks, perk); it != perks.end()) {
						a_draft.reqSkill = skill;
						a_draft.reqPerk = static_cast<int>(it - perks.begin());
					}
				}
			} else if (a_draft.reqType == kReqBlessing) {
				const auto god = Normalise(req.value("god", ""));
				for (int i = 0; i < static_cast<int>(kGods.size()); ++i) {
					if (Normalise(kGods[static_cast<std::size_t>(i)]) == god) {
						a_draft.reqGod = i;
					}
				}
			}
		}
	}

	void NewDraft(Draft& a_draft, std::string a_name, std::string a_outfit, std::vector<std::string> a_pieces)
	{
		auto& files = SetFiles::Get();
		a_draft = Draft{};
		a_draft.active = true;
		a_draft.index = SIZE_MAX;
		a_draft.original = nlohmann::json{ { "id", SetFiles::NewId() } };
		CopyTo(a_draft.name, a_name);
		a_draft.outfit = std::move(a_outfit);
		a_draft.pieces = std::move(a_pieces);
		// One file per armour mod, so a mod's sets can be shared or reused as a preset.
		a_draft.file = files.FileFor(ModStemOf(a_draft));
		// Sold where it can be: clothing at Radiant Raiment, anything else at caravans and fences.
		const auto resolved = Resolve(a_draft.outfit, a_draft.pieces).pieces;
		if (TradeCount(resolved, Source::RadiantRaiment).first > 0) {
			a_draft.sources[static_cast<std::size_t>(Source::RadiantRaiment)] = true;
		} else {
			a_draft.sources[static_cast<std::size_t>(Source::KhajiitCaravans)] = true;
			a_draft.sources[static_cast<std::size_t>(Source::ThievesGuild)] = true;
		}
		a_draft.dirty = true;
	}

	nlohmann::json DraftToJson(Draft& a_draft)
	{
		nlohmann::json entry = a_draft.original.is_object() ? a_draft.original : nlohmann::json::object();
		for (const auto* key : { "enabled", "outfit", "pieces", "sources", "rarity", "price", "requirement", "unique", "offering" }) {
			entry.erase(key);
		}
		entry["name"] = std::string(a_draft.name);
		if (!entry.contains("id")) {
			entry["id"] = SetFiles::NewId();
		}
		if (a_draft.ownOffering && !a_draft.offering.empty()) {
			auto lines = nlohmann::json::array();
			for (const auto& line : a_draft.offering) {
				if (line.refs.empty()) {
					continue;
				}
				nlohmann::json item = line.refs.size() == 1 ? nlohmann::json(line.refs.front()) : nlohmann::json(line.refs);
				lines.push_back({ { "item", item }, { "count", std::max(1, line.count) } });
			}
			if (!lines.empty()) {
				entry["offering"] = lines;
			}
		}
		if (!a_draft.enabled) {
			entry["enabled"] = false;
		}
		if (!a_draft.outfit.empty()) {
			entry["outfit"] = a_draft.outfit;
		}
		if (!a_draft.pieces.empty()) {
			entry["pieces"] = a_draft.pieces;
		}
		auto sources = nlohmann::json::array();
		for (const auto source : kAllSources) {
			if (a_draft.sources[static_cast<std::size_t>(source)]) {
				sources.push_back(std::string(SourceName(source)));
			}
		}
		for (std::size_t god = 0; god < kGods.size(); ++god) {
			if (a_draft.gods[god]) {
				sources.push_back(kGods[god]);
			}
		}
		entry["sources"] = sources;
		entry["rarity"] = kRarities[static_cast<std::size_t>(a_draft.rarity)];
		if (a_draft.hasPrice) {
			entry["price"] = std::max(0, a_draft.price);
		}
		if (a_draft.reqType != kReqNone) {
			nlohmann::json req{ { "type", kReqJsonTypes[static_cast<std::size_t>(a_draft.reqType)] } };
			switch (a_draft.reqType) {
			case kReqSkill:
				req["skill"] = kSkills[static_cast<std::size_t>(a_draft.reqSkill)];
				req["value"] = a_draft.reqValue;
				break;
			case kReqFaction:
				req["faction"] = a_draft.reqFaction == kFactionOther ? std::string(a_draft.reqRef) : std::string(kFactions[static_cast<std::size_t>(a_draft.reqFaction)].ref);
				req["rank"] = a_draft.reqValue;
				break;
			case kReqQuest:
				req["quest"] = a_draft.reqQuest == kQuestOther ? std::string(a_draft.reqRef) : std::string(kQuests[static_cast<std::size_t>(a_draft.reqQuest)].ref);
				break;
			case kReqPerk:
				{
					auto* perk = DraftPerk(a_draft);
					req["perk"] = perk ? FormString(perk) : std::string(a_draft.reqRef);
					break;
				}
			case kReqBlessing:
				req["god"] = kGods[static_cast<std::size_t>(std::clamp(a_draft.reqGod, 0, static_cast<int>(kGods.size()) - 1))];
				break;
			default:
				req["value"] = a_draft.reqValue;
				break;
			}
			entry["requirement"] = req;
		}
		if (!a_draft.unique) {
			entry["unique"] = false;
		}
		return entry;
	}

	void ApplyChanges()
	{
		// The game thread re-reads the files and re-syncs the vendors.
		SKSE::GetTaskInterface()->AddTask([]() { Stocker::ReloadCatalog(); });
	}

	bool SaveDraft(Draft& a_draft)
	{
		auto& store = SetFiles::Get();
		auto& file = store.Files()[a_draft.file];
		auto& sets = file.Sets();
		const std::string name = a_draft.name;
		if (name.empty()) {
			a_draft.status = "Give the set a name first.";
			return false;
		}
		for (std::size_t i = 0; i < sets.size(); ++i) {
			if (i != a_draft.index && sets[i].value("name", "") == name) {
				a_draft.status = fmt::format("{}.json already has a set called \"{}\".", file.stem, name);
				return false;
			}
		}
		const auto entry = DraftToJson(a_draft);
		if (a_draft.index == SIZE_MAX) {
			sets.push_back(entry);
			a_draft.index = sets.size() - 1;
		} else {
			sets[a_draft.index] = entry;
		}
		if (!store.Save(a_draft.file)) {
			a_draft.status = "Could not save; see ImmersiveOutfitDistribution.log.";
			return false;
		}
		a_draft.original = entry;
		a_draft.dirty = false;
		a_draft.status = fmt::format("Saved to {}.json. Vendors restock with it the next time you talk to them.", file.stem);
		ApplyChanges();
		return true;
	}

	void DeleteDraft(Draft& a_draft)
	{
		auto& store = SetFiles::Get();
		if (a_draft.index != SIZE_MAX) {
			auto& sets = store.Files()[a_draft.file].Sets();
			sets.erase(sets.begin() + static_cast<std::ptrdiff_t>(a_draft.index));
			store.Save(a_draft.file);
			ApplyChanges();
		}
		a_draft = Draft{};
	}
}
