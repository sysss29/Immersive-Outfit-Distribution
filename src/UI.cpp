#include "UI.h"

#include "Catalog.h"
#include "SetFiles.h"
#include "Settings.h"
#include "Sources.h"
#include "Stocker.h"

#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include "SKSEMenuFramework.h"

namespace IOD::UI
{
	namespace
	{
		namespace ImGui = ImGuiMCP;

		constexpr ImGui::ImVec4 kWarn{ 1.0f, 0.62f, 0.25f, 1.0f };
		constexpr ImGui::ImVec4 kBad{ 1.0f, 0.38f, 0.38f, 1.0f };
		constexpr ImGui::ImVec4 kGood{ 0.55f, 0.85f, 0.55f, 1.0f };
		constexpr ImGui::ImVec4 kDim{ 0.62f, 0.62f, 0.62f, 1.0f };

		// --- reference data ---------------------------------------------------------------

		constexpr std::array kRarities{ "Common", "Rare", "Legendary" };

		constexpr std::array kRequirementTypes{ "None", "Character level", "Base Health", "Base Magicka", "Base Stamina",
			"Skill level", "Faction rank", "Quest completed", "Blessing (milestone 4)" };
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
			kReqBlessing
		};
		constexpr std::array kReqJsonTypes{ "", "level", "health", "magicka", "stamina", "skill", "faction", "quest", "blessing" };

		constexpr std::array kSkills{ "One-Handed", "Two-Handed", "Archery", "Block", "Smithing", "Heavy Armor", "Light Armor",
			"Pickpocket", "Lockpicking", "Sneak", "Alchemy", "Speech", "Alteration", "Conjuration", "Destruction", "Illusion",
			"Restoration", "Enchanting" };

		struct KnownFaction
		{
			const char* label;
			const char* ref;
		};
		constexpr std::array kFactions{
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
		constexpr int kFactionOther = static_cast<int>(kFactions.size()) - 1;

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

		template <std::size_t N>
		void CopyTo(char (&a_buffer)[N], std::string_view a_text)
		{
			const auto n = std::min(a_text.size(), N - 1);
			std::memcpy(a_buffer, a_text.data(), n);
			a_buffer[n] = '\0';
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
			return Stocker::CanTradePieces({ a_armor }, RadiantRaimentFaction()) ? "clothing" : "armour";
		}

		// --- loaded armour by mod (built once, on first use) ---------------------------------

		struct ModContent
		{
			std::string                    file;
			std::vector<RE::TESObjectARMO*> armour;
			std::vector<RE::BGSOutfit*>    outfits;
		};
		std::vector<ModContent> g_mods;

		void BuildModIndex()
		{
			if (!g_mods.empty()) {
				return;
			}
			auto* data = RE::TESDataHandler::GetSingleton();
			if (!data) {
				return;
			}
			std::map<std::string, ModContent> byFile;
			for (auto* armor : data->GetFormArray<RE::TESObjectARMO>()) {
				const auto* file = armor ? armor->GetFile(0) : nullptr;
				const char* name = armor ? armor->GetName() : nullptr;
				if (!file || !name || !*name || !armor->GetPlayable() || armor->templateArmor) {
					continue;
				}
				auto& mod = byFile[std::string(file->GetFilename())];
				mod.file = file->GetFilename();
				mod.armour.push_back(armor);
			}
			for (auto* outfit : data->GetFormArray<RE::BGSOutfit>()) {
				const auto* file = outfit ? outfit->GetFile(0) : nullptr;
				if (!file) {
					continue;
				}
				const auto key = std::string(file->GetFilename());
				if (auto it = byFile.find(key); it != byFile.end()) {
					it->second.outfits.push_back(outfit);
				}
			}
			for (auto& [file, mod] : byFile) {
				std::ranges::sort(mod.armour, [](auto* a, auto* b) { return std::string_view(a->GetName()) < std::string_view(b->GetName()); });
				g_mods.push_back(std::move(mod));
			}
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
			std::array<bool, 3>      sources{};
			int                      rarity{ 0 };
			bool                     hasPrice{ false };
			int                      price{ 1000 };
			int                      reqType{ kReqNone };
			int                      reqValue{ 0 };
			int                      reqSkill{ 0 };
			int                      reqFaction{ 0 };
			char                     reqRef[128]{};
			bool                     dirty{ false };
			bool                     confirmDelete{ false };
			std::string              status;  // last save result
		};
		Draft g_draft;

		struct Resolved
		{
			std::vector<RE::TESObjectARMO*> pieces;
			std::size_t                     missing{ 0 };
		};

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

		void LoadDraft(std::size_t a_file, std::size_t a_index)
		{
			auto& files = SetFiles::Get().Files();
			const auto& entry = files[a_file].Sets()[a_index];
			g_draft = Draft{};
			g_draft.active = true;
			g_draft.file = a_file;
			g_draft.index = a_index;
			g_draft.original = entry;
			CopyTo(g_draft.name, entry.value("name", ""));
			g_draft.enabled = entry.value("enabled", true);
			g_draft.unique = entry.value("unique", true);
			g_draft.outfit = entry.value("outfit", "");
			if (entry.contains("pieces") && entry["pieces"].is_array()) {
				for (const auto& p : entry["pieces"]) {
					if (p.is_string()) {
						g_draft.pieces.push_back(p.get<std::string>());
					}
				}
			}
			if (entry.contains("sources") && entry["sources"].is_array()) {
				for (const auto& s : entry["sources"]) {
					if (const auto source = s.is_string() ? ParseSource(s.get<std::string>()) : std::nullopt) {
						g_draft.sources[static_cast<std::size_t>(*source)] = true;
					}
				}
			}
			const auto rarity = Normalise(entry.value("rarity", "common"));
			g_draft.rarity = rarity == "rare" ? 1 : rarity == "legendary" ? 2 : 0;
			if (entry.contains("price") && entry["price"].is_number_integer()) {
				g_draft.hasPrice = true;
				g_draft.price = entry["price"].get<int>();
			}
			if (entry.contains("requirement") && entry["requirement"].is_object()) {
				const auto& req = entry["requirement"];
				const auto  type = Normalise(req.value("type", ""));
				for (int i = 1; i < static_cast<int>(kReqJsonTypes.size()); ++i) {
					if (type == kReqJsonTypes[i]) {
						g_draft.reqType = i;
					}
				}
				g_draft.reqValue = req.value("value", req.value("rank", 0));
				if (g_draft.reqType == kReqSkill) {
					const auto skill = Normalise(req.value("skill", ""));
					for (int i = 0; i < static_cast<int>(kSkills.size()); ++i) {
						if (Normalise(kSkills[i]) == skill) {
							g_draft.reqSkill = i;
						}
					}
				} else if (g_draft.reqType == kReqFaction) {
					const auto ref = req.value("faction", "");
					auto*      form = LookupFormString(ref);
					g_draft.reqFaction = kFactionOther;
					for (int i = 0; i < kFactionOther; ++i) {
						if (form && LookupFormString(kFactions[i].ref) == form) {
							g_draft.reqFaction = i;
						}
					}
					CopyTo(g_draft.reqRef, ref);
				} else if (g_draft.reqType == kReqQuest) {
					CopyTo(g_draft.reqRef, req.value("quest", ""));
				} else if (g_draft.reqType == kReqBlessing) {
					CopyTo(g_draft.reqRef, req.value("god", ""));
				}
			}
		}

		void NewDraft(std::string a_name, std::string a_outfit, std::vector<std::string> a_pieces)
		{
			auto& files = SetFiles::Get();
			g_draft = Draft{};
			g_draft.active = true;
			g_draft.file = files.DefaultFile();
			g_draft.index = SIZE_MAX;
			CopyTo(g_draft.name, a_name);
			g_draft.outfit = std::move(a_outfit);
			g_draft.pieces = std::move(a_pieces);
			// Sold where it can be: clothing at Radiant Raiment, anything else at caravans and fences.
			const auto resolved = Resolve(g_draft.outfit, g_draft.pieces).pieces;
			if (TradeCount(resolved, Source::RadiantRaiment).first > 0) {
				g_draft.sources[static_cast<std::size_t>(Source::RadiantRaiment)] = true;
			} else {
				g_draft.sources[static_cast<std::size_t>(Source::KhajiitCaravans)] = true;
				g_draft.sources[static_cast<std::size_t>(Source::ThievesGuild)] = true;
			}
			g_draft.dirty = true;
		}

		nlohmann::json DraftToJson()
		{
			nlohmann::json entry = g_draft.original.is_object() ? g_draft.original : nlohmann::json::object();
			for (const auto* key : { "enabled", "outfit", "pieces", "sources", "rarity", "price", "requirement", "unique" }) {
				entry.erase(key);
			}
			entry["name"] = std::string(g_draft.name);
			if (!g_draft.enabled) {
				entry["enabled"] = false;
			}
			if (!g_draft.outfit.empty()) {
				entry["outfit"] = g_draft.outfit;
			}
			if (!g_draft.pieces.empty()) {
				entry["pieces"] = g_draft.pieces;
			}
			auto sources = nlohmann::json::array();
			for (const auto source : kAllSources) {
				if (g_draft.sources[static_cast<std::size_t>(source)]) {
					sources.push_back(std::string(SourceName(source)));
				}
			}
			entry["sources"] = sources;
			entry["rarity"] = kRarities[static_cast<std::size_t>(g_draft.rarity)];
			if (g_draft.hasPrice) {
				entry["price"] = std::max(0, g_draft.price);
			}
			if (g_draft.reqType != kReqNone) {
				nlohmann::json req{ { "type", kReqJsonTypes[static_cast<std::size_t>(g_draft.reqType)] } };
				switch (g_draft.reqType) {
				case kReqSkill:
					req["skill"] = kSkills[static_cast<std::size_t>(g_draft.reqSkill)];
					req["value"] = g_draft.reqValue;
					break;
				case kReqFaction:
					req["faction"] = g_draft.reqFaction == kFactionOther ? std::string(g_draft.reqRef) : std::string(kFactions[static_cast<std::size_t>(g_draft.reqFaction)].ref);
					req["rank"] = g_draft.reqValue;
					break;
				case kReqQuest:
					req["quest"] = std::string(g_draft.reqRef);
					break;
				case kReqBlessing:
					req["god"] = std::string(g_draft.reqRef);
					break;
				default:
					req["value"] = g_draft.reqValue;
					break;
				}
				entry["requirement"] = req;
			}
			if (!g_draft.unique) {
				entry["unique"] = false;
			}
			return entry;
		}

		void ApplyChanges()
		{
			// The game thread re-reads the files and re-syncs the vendors.
			SKSE::GetTaskInterface()->AddTask([]() { Stocker::ReloadCatalog(); });
		}

		bool SaveDraft()
		{
			auto& store = SetFiles::Get();
			auto& file = store.Files()[g_draft.file];
			auto& sets = file.Sets();
			const std::string name = g_draft.name;
			if (name.empty()) {
				g_draft.status = "Give the set a name first.";
				return false;
			}
			for (std::size_t i = 0; i < sets.size(); ++i) {
				if (i != g_draft.index && sets[i].value("name", "") == name) {
					g_draft.status = fmt::format("{}.json already has a set called \"{}\".", file.stem, name);
					return false;
				}
			}
			const auto entry = DraftToJson();
			if (g_draft.index == SIZE_MAX) {
				sets.push_back(entry);
				g_draft.index = sets.size() - 1;
			} else {
				sets[g_draft.index] = entry;
			}
			if (!store.Save(g_draft.file)) {
				g_draft.status = "Could not save; see ImmersiveOutfitDistribution.log.";
				return false;
			}
			g_draft.original = entry;
			g_draft.dirty = false;
			g_draft.status = fmt::format("Saved to {}.json. Vendors restock with it the next time you talk to them.", file.stem);
			ApplyChanges();
			return true;
		}

		void DeleteDraft()
		{
			auto& store = SetFiles::Get();
			if (g_draft.index != SIZE_MAX) {
				auto& sets = store.Files()[g_draft.file].Sets();
				sets.erase(sets.begin() + static_cast<std::ptrdiff_t>(g_draft.index));
				store.Save(g_draft.file);
				ApplyChanges();
			}
			g_draft = Draft{};
		}

		// --- piece picker (shared by the editor and "New set") ------------------------------

		struct Picker
		{
			int                             mod{ -1 };
			char                            modFilter[64]{};
			char                            pieceFilter[64]{};
			std::vector<RE::TESObjectARMO*> chosen;
		};

		// Returns true when the mod changed.
		bool ModCombo(Picker& a_picker)
		{
			BuildModIndex();
			ImGui::SetNextItemWidth(220.0f);
			ImGui::InputTextWithHint("##modfilter", "Filter mods", a_picker.modFilter, sizeof(a_picker.modFilter));
			ImGui::SameLine();
			const char* preview = a_picker.mod >= 0 ? g_mods[static_cast<std::size_t>(a_picker.mod)].file.c_str() : "Choose a mod";
			bool        changed = false;
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::BeginCombo("##mod", preview)) {
				for (int i = 0; i < static_cast<int>(g_mods.size()); ++i) {
					const auto& mod = g_mods[static_cast<std::size_t>(i)];
					if (!Contains(mod.file, a_picker.modFilter)) {
						continue;
					}
					const auto label = fmt::format("{}  ({} pieces, {} outfits)##{}", mod.file, mod.armour.size(), mod.outfits.size(), i);
					if (ImGui::Selectable(label.c_str(), i == a_picker.mod)) {
						a_picker.mod = i;
						a_picker.chosen.clear();
						changed = true;
					}
				}
				ImGui::EndCombo();
			}
			return changed;
		}

		// Lists the chosen mod's pieces with a checkbox each; fills a_picker.chosen.
		void PieceList(Picker& a_picker, float a_height)
		{
			if (a_picker.mod < 0) {
				return;
			}
			const auto& mod = g_mods[static_cast<std::size_t>(a_picker.mod)];
			ImGui::SetNextItemWidth(220.0f);
			ImGui::InputTextWithHint("##piecefilter", "Filter pieces", a_picker.pieceFilter, sizeof(a_picker.pieceFilter));
			ImGui::SameLine();
			ImGui::TextDisabled("%zu selected", a_picker.chosen.size());
			const int flags = ImGui::ImGuiTableFlags_RowBg | ImGui::ImGuiTableFlags_ScrollY | ImGui::ImGuiTableFlags_BordersInnerH;
			if (ImGui::BeginTable("pieces", 3, flags, ImGui::ImVec2{ 0.0f, a_height })) {
				ImGui::TableSetupColumn("Piece", ImGui::ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Slots", ImGui::ImGuiTableColumnFlags_WidthFixed, 110.0f);
				ImGui::TableSetupColumn("Kind", ImGui::ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableHeadersRow();
				for (auto* armor : mod.armour) {
					if (!Contains(armor->GetName(), a_picker.pieceFilter)) {
						continue;
					}
					ImGui::PushID(armor);
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					const auto it = std::ranges::find(a_picker.chosen, armor);
					bool       checked = it != a_picker.chosen.end();
					if (ImGui::Checkbox(armor->GetName(), &checked)) {
						if (checked) {
							a_picker.chosen.push_back(armor);
						} else {
							a_picker.chosen.erase(it);
						}
					}
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(Slots(armor).c_str());
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(Kind(armor));
					ImGui::PopID();
				}
				ImGui::EndTable();
			}
		}

		// --- pages --------------------------------------------------------------------------

		char   g_setFilter[64]{};
		Picker g_addPicker;  // "Add pieces" inside the editor

		void RenderEditor()
		{
			auto& store = SetFiles::Get();
			auto& file = store.Files()[g_draft.file];
			ImGui::Spacing();
			ImGui::SeparatorText(g_draft.index == SIZE_MAX ? "New set (not saved yet)" : "Edit set");
			ImGui::TextDisabled("File: %s.json", file.stem.c_str());

			const auto resolved = Resolve(g_draft.outfit, g_draft.pieces);

			if (ImGui::InputText("Name", g_draft.name, sizeof(g_draft.name))) {
				g_draft.dirty = true;
			}
			if (ImGui::Checkbox("Enabled", &g_draft.enabled)) {
				g_draft.dirty = true;
			}
			ImGui::SameLine();
			if (ImGui::Checkbox("Unique (leaves rotation once you own every piece)", &g_draft.unique)) {
				g_draft.dirty = true;
			}

			ImGui::Text("Sold at:");
			for (const auto source : kAllSources) {
				ImGui::PushID(static_cast<int>(source));
				const std::string label(SourceName(source));
				if (ImGui::Checkbox(label.c_str(), &g_draft.sources[static_cast<std::size_t>(source)])) {
					g_draft.dirty = true;
				}
				if (const auto warning = SourceWarning(resolved.pieces, source); !warning.empty()) {
					ImGui::SameLine();
					ImGui::TextColored(g_draft.sources[static_cast<std::size_t>(source)] ? kWarn : kDim, "%s", warning.c_str());
				}
				ImGui::PopID();
			}

			ImGui::SetNextItemWidth(160.0f);
			if (ImGui::Combo("Rarity", &g_draft.rarity, kRarities.data(), static_cast<int>(kRarities.size()))) {
				g_draft.dirty = true;
			}
			if (ImGui::Checkbox("Set price", &g_draft.hasPrice)) {
				g_draft.dirty = true;
			}
			if (g_draft.hasPrice) {
				ImGui::SameLine();
				ImGui::SetNextItemWidth(140.0f);
				if (ImGui::InputInt("gold for the whole set", &g_draft.price, 100, 1000)) {
					g_draft.price = std::max(0, g_draft.price);
					g_draft.dirty = true;
				}
			} else {
				ImGui::SameLine();
				ImGui::TextDisabled("(pieces keep their own values)");
			}

			ImGui::SetNextItemWidth(220.0f);
			if (ImGui::Combo("Requirement", &g_draft.reqType, kRequirementTypes.data(), static_cast<int>(kRequirementTypes.size()))) {
				g_draft.dirty = true;
			}
			switch (g_draft.reqType) {
			case kReqLevel:
			case kReqHealth:
			case kReqMagicka:
			case kReqStamina:
				ImGui::SetNextItemWidth(140.0f);
				g_draft.dirty |= ImGui::InputInt("at least", &g_draft.reqValue, 1, 10);
				break;
			case kReqSkill:
				ImGui::SetNextItemWidth(180.0f);
				g_draft.dirty |= ImGui::Combo("Skill", &g_draft.reqSkill, kSkills.data(), static_cast<int>(kSkills.size()));
				ImGui::SetNextItemWidth(140.0f);
				g_draft.dirty |= ImGui::InputInt("at least", &g_draft.reqValue, 1, 10);
				break;
			case kReqFaction:
				{
					std::array<const char*, kFactions.size()> labels{};
					for (std::size_t i = 0; i < kFactions.size(); ++i) {
						labels[i] = kFactions[i].label;
					}
					ImGui::SetNextItemWidth(220.0f);
					g_draft.dirty |= ImGui::Combo("Faction", &g_draft.reqFaction, labels.data(), static_cast<int>(labels.size()));
					if (g_draft.reqFaction == kFactionOther) {
						g_draft.dirty |= ImGui::InputTextWithHint("Faction form", "Plugin.esp|0x123", g_draft.reqRef, sizeof(g_draft.reqRef));
					}
					ImGui::SetNextItemWidth(140.0f);
					g_draft.dirty |= ImGui::InputInt("rank at least", &g_draft.reqValue, 1, 1);
					ImGui::TextDisabled("Rank 0 = member. Ranks are faction-specific; untested in game yet.");
					break;
				}
			case kReqQuest:
				{
					g_draft.dirty |= ImGui::InputTextWithHint("Quest form", "Plugin.esp|0x123", g_draft.reqRef, sizeof(g_draft.reqRef));
					auto* form = LookupFormString(g_draft.reqRef);
					auto* quest = form ? form->As<RE::TESQuest>() : nullptr;
					if (quest) {
						ImGui::TextColored(kGood, "Quest: %s", quest->GetName());
					} else {
						ImGui::TextColored(kWarn, "No quest found with that form ID.");
					}
					break;
				}
			case kReqBlessing:
				g_draft.dirty |= ImGui::InputTextWithHint("God", "Mara", g_draft.reqRef, sizeof(g_draft.reqRef));
				ImGui::TextColored(kWarn, "Shrines arrive in milestone 4; until then this set stays hidden.");
				break;
			default:
				break;
			}

			// Pieces
			ImGui::Spacing();
			ImGui::Text("Pieces (%zu):", resolved.pieces.size());
			if (resolved.missing > 0) {
				ImGui::SameLine();
				ImGui::TextColored(kBad, "%zu reference(s) not loaded (mod disabled?)", resolved.missing);
			}
			if (!g_draft.outfit.empty()) {
				ImGui::TextDisabled("From outfit record %s", g_draft.outfit.c_str());
			}
			RE::TESObjectARMO* remove = nullptr;
			if (ImGui::BeginTable("setpieces", 4, ImGui::ImGuiTableFlags_RowBg | ImGui::ImGuiTableFlags_BordersInnerH)) {
				ImGui::TableSetupColumn("Piece", ImGui::ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Slots", ImGui::ImGuiTableColumnFlags_WidthFixed, 110.0f);
				ImGui::TableSetupColumn("Kind", ImGui::ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("", ImGui::ImGuiTableColumnFlags_WidthFixed, 70.0f);
				for (auto* armor : resolved.pieces) {
					ImGui::PushID(armor);
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(armor->GetName());
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(Slots(armor).c_str());
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(Kind(armor));
					ImGui::TableNextColumn();
					if (ImGui::SmallButton("Remove")) {
						remove = armor;
					}
					ImGui::PopID();
				}
				ImGui::EndTable();
			}
			if (remove) {
				// An outfit record can't lose a piece: switch the set to an explicit piece list.
				if (!g_draft.outfit.empty()) {
					g_draft.pieces.clear();
					for (auto* armor : resolved.pieces) {
						g_draft.pieces.push_back(FormString(armor));
					}
					g_draft.outfit.clear();
				}
				std::erase(g_draft.pieces, FormString(remove));
				g_draft.dirty = true;
			}
			if (ImGui::TreeNode("Add pieces")) {
				ModCombo(g_addPicker);
				PieceList(g_addPicker, 220.0f);
				ImGui::BeginDisabled(g_addPicker.chosen.empty());
				if (ImGui::Button("Add selected pieces to this set")) {
					for (auto* armor : g_addPicker.chosen) {
						const auto ref = FormString(armor);
						if (std::ranges::find(resolved.pieces, armor) == resolved.pieces.end()) {
							g_draft.pieces.push_back(ref);
						}
					}
					g_addPicker.chosen.clear();
					g_draft.dirty = true;
				}
				ImGui::EndDisabled();
				ImGui::TreePop();
			}

			// Actions
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::BeginDisabled(!g_draft.dirty);
			if (ImGui::Button("Save")) {
				SaveDraft();
			}
			ImGui::SameLine();
			if (ImGui::Button("Undo changes")) {
				if (g_draft.index == SIZE_MAX) {
					g_draft = Draft{};
				} else {
					LoadDraft(g_draft.file, g_draft.index);
				}
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Close")) {
				g_draft = Draft{};
				return;
			}
			ImGui::SameLine();
			if (!g_draft.confirmDelete) {
				if (ImGui::Button("Delete set")) {
					g_draft.confirmDelete = true;
				}
			} else {
				if (ImGui::Button("Really delete?")) {
					DeleteDraft();
					return;
				}
				ImGui::SameLine();
				if (ImGui::Button("Keep it")) {
					g_draft.confirmDelete = false;
				}
			}
			if (g_draft.dirty) {
				ImGui::TextColored(kWarn, "Unsaved changes.");
			}
			if (!g_draft.status.empty()) {
				ImGui::TextWrapped("%s", g_draft.status.c_str());
			}
		}

		void __stdcall RenderSets()
		{
			auto& store = SetFiles::Get();
			ImGui::TextWrapped("Every set IOD distributes. Click one to edit it. Changes are saved to its file and apply at once; vendors restock with them the next time you talk to them.");
			ImGui::SetNextItemWidth(260.0f);
			ImGui::InputTextWithHint("##setfilter", "Filter sets", g_setFilter, sizeof(g_setFilter));
			ImGui::SameLine();
			if (ImGui::Button("Reload files")) {
				store.Load();
				g_draft = Draft{};
				ApplyChanges();
			}

			const int flags = ImGui::ImGuiTableFlags_RowBg | ImGui::ImGuiTableFlags_ScrollY | ImGui::ImGuiTableFlags_BordersInnerH;
			const float height = g_draft.active ? 200.0f : 0.0f;
			if (ImGui::BeginTable("sets", 5, flags, ImGui::ImVec2{ 0.0f, height })) {
				ImGui::TableSetupColumn("Set", ImGui::ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Sold at", ImGui::ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Rarity", ImGui::ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Price", ImGui::ImGuiTableColumnFlags_WidthFixed, 60.0f);
				ImGui::TableSetupColumn("Status", ImGui::ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableHeadersRow();
				auto& files = store.Files();
				for (std::size_t f = 0; f < files.size(); ++f) {
					auto& sets = files[f].Sets();
					for (std::size_t i = 0; i < sets.size(); ++i) {
						const auto& entry = sets[i];
						const auto  name = entry.value("name", "(no name)");
						if (!Contains(name, g_setFilter)) {
							continue;
						}
						ImGui::PushID(static_cast<int>(f * 10000 + i));
						ImGui::TableNextRow();
						ImGui::TableNextColumn();
						const bool selected = g_draft.active && g_draft.file == f && g_draft.index == i;
						if (ImGui::Selectable(name.c_str(), selected, ImGui::ImGuiSelectableFlags_SpanAllColumns)) {
							LoadDraft(f, i);
						}
						if (files.size() > 1 && ImGui::IsItemHovered()) {
							ImGui::SetTooltip("%s.json", files[f].stem.c_str());
						}

						std::string where;
						std::string warning;
						const auto  resolved = Resolve(entry);
						if (entry.contains("sources") && entry["sources"].is_array()) {
							for (const auto& s : entry["sources"]) {
								if (const auto source = s.is_string() ? ParseSource(s.get<std::string>()) : std::nullopt) {
									where += (where.empty() ? "" : ", ") + std::string(SourceName(*source));
									if (warning.empty() && TradeCount(resolved.pieces, *source).first == 0 && !resolved.pieces.empty()) {
										warning = fmt::format("never at {}", SourceName(*source));
									}
								}
							}
						}
						ImGui::TableNextColumn();
						ImGui::TextUnformatted(where.empty() ? "-" : where.c_str());
						ImGui::TableNextColumn();
						ImGui::TextUnformatted(entry.value("rarity", "Common").c_str());
						ImGui::TableNextColumn();
						if (entry.contains("price") && entry["price"].is_number_integer()) {
							ImGui::Text("%d", entry["price"].get<int>());
						} else {
							ImGui::TextDisabled("-");
						}
						ImGui::TableNextColumn();
						if (!entry.value("enabled", true)) {
							ImGui::TextColored(kDim, "Disabled");
						} else if (resolved.pieces.empty()) {
							ImGui::TextColored(kBad, "Mod not loaded");
						} else if (where.empty()) {
							ImGui::TextColored(kWarn, "Not sold anywhere");
						} else if (!warning.empty()) {
							ImGui::TextColored(kWarn, "%s", warning.c_str());
						} else {
							ImGui::TextColored(kGood, "%zu pieces", resolved.pieces.size());
						}
						ImGui::PopID();
					}
				}
				ImGui::EndTable();
			}
			if (store.Files().empty() || std::ranges::all_of(store.Files(), [](SetFile& a_f) { return a_f.Sets().empty(); })) {
				ImGui::TextDisabled("No sets yet. Use \"New set\" to create one.");
			}
			if (g_draft.active) {
				RenderEditor();
			}
		}

		Picker g_newPicker;
		char   g_newName[128]{};

		void __stdcall RenderNewSet()
		{
			ImGui::TextWrapped("Pick the mod that adds the outfit. If it defines outfit records, one click makes a set; otherwise choose its pieces. New sets go to MyOutfits.json and open in \"Sets\" for the details (where, rarity, price, requirement).");
			if (ModCombo(g_newPicker)) {
				g_newName[0] = '\0';
			}
			if (g_newPicker.mod < 0) {
				return;
			}
			const auto& mod = g_mods[static_cast<std::size_t>(g_newPicker.mod)];

			if (ImGui::BeginTabBar("newset")) {
				if (!mod.outfits.empty() && ImGui::BeginTabItem("Outfit records")) {
					for (auto* outfit : mod.outfits) {
						const auto pieces = OutfitPieces(outfit);
						if (pieces.empty()) {
							continue;
						}
						ImGui::PushID(outfit);
						const auto label = OutfitLabel(outfit);
						if (ImGui::SmallButton("Make set")) {
							NewDraft(label, FormString(outfit), {});
						}
						ImGui::SameLine();
						ImGui::Text("%s  (%zu pieces)", label.c_str(), pieces.size());
						if (ImGui::IsItemHovered()) {
							std::string names;
							for (auto* armor : pieces) {
								names += std::string(armor->GetName()) + "  [" + Slots(armor) + "]\n";
							}
							ImGui::SetTooltip("%s", names.c_str());
						}
						ImGui::PopID();
					}
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Choose pieces")) {
					PieceList(g_newPicker, 300.0f);
					ImGui::InputTextWithHint("Set name", "e.g. Thorn Princess", g_newName, sizeof(g_newName));
					ImGui::BeginDisabled(g_newPicker.chosen.empty() || g_newName[0] == '\0');
					if (ImGui::Button("Make set from the selected pieces")) {
						std::vector<std::string> refs;
						for (auto* armor : g_newPicker.chosen) {
							refs.push_back(FormString(armor));
						}
						NewDraft(g_newName, {}, std::move(refs));
						g_newPicker.chosen.clear();
						g_newName[0] = '\0';
					}
					ImGui::EndDisabled();
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}
			if (g_draft.active && g_draft.index == SIZE_MAX) {
				ImGui::TextColored(kGood, "\"%s\" is ready: open \"Sets\" to finish and save it.", g_draft.name);
			}
		}

		void __stdcall RenderVendors()
		{
			const auto [period, lines] = Stocker::Snapshot();
			if (period < 0) {
				ImGui::TextDisabled("Load a save to see this period's stock.");
				return;
			}
			ImGui::Text("Current 48-hour period: %d (next draw in %.0f hours)", period,
				std::max(0.0f, (static_cast<float>(period + 1) * 2.0f - (RE::Calendar::GetSingleton() ? RE::Calendar::GetSingleton()->GetDaysPassed() : 0.0f)) * 24.0f));
			if (ImGui::BeginTable("vendors", 3, ImGui::ImGuiTableFlags_RowBg | ImGui::ImGuiTableFlags_BordersInnerH)) {
				ImGui::TableSetupColumn("Vendor", ImGui::ImGuiTableColumnFlags_WidthFixed, 220.0f);
				ImGui::TableSetupColumn("Group", ImGui::ImGuiTableColumnFlags_WidthFixed, 140.0f);
				ImGui::TableSetupColumn("Sets this period", ImGui::ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableHeadersRow();
				for (const auto& line : lines) {
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(line.vendor.c_str());
					ImGui::TableNextColumn();
					ImGui::TextDisabled("%s", line.source.c_str());
					ImGui::TableNextColumn();
					if (line.sets.empty()) {
						ImGui::TextDisabled("nothing");
					} else {
						std::string sets;
						for (const auto& s : line.sets) {
							sets += (sets.empty() ? "" : ", ") + s;
						}
						ImGui::TextUnformatted(sets.c_str());
					}
				}
				ImGui::EndTable();
			}
			ImGui::Spacing();
			ImGui::SeparatorText("Testing");
			if (ImGui::Button("Redraw now")) {
				SKSE::GetTaskInterface()->AddTask([]() { Stocker::Redraw(); });
			}
			ImGui::SameLine();
			ImGui::TextWrapped("Draws new stock for every vendor right away; it shows up when you next talk to them.");
			ImGui::TextDisabled("The Thieves Guild fences outside Riften only trade once their quests are done.");
		}

		std::string g_settingsStatus;

		void __stdcall RenderSettings()
		{
			auto& settings = Settings::Get();
			bool  changed = false;
			changed |= ImGui::Checkbox("Show \"New arrivals\" when a vendor has new sets", &settings.arrivalMessage);
			changed |= ImGui::Checkbox("Testing: every vendor gets at least one set per period", &settings.guaranteeOnePerVendor);
			ImGui::TextDisabled("Applies from the next draw (or press \"Redraw now\" under Vendors).");
			if (changed) {
				g_settingsStatus = settings.Save() ? "Saved." : "Could not save; see ImmersiveOutfitDistribution.log.";
			}
			if (!g_settingsStatus.empty()) {
				ImGui::TextDisabled("%s", g_settingsStatus.c_str());
			}
		}
	}

	void Register()
	{
		if (!SKSEMenuFramework::IsInstalled()) {
			spdlog::info("SKSE Menu Framework not installed: no in-game settings page");
			return;
		}
		SetFiles::Get().Load();
		SKSEMenuFramework::SetSection("Immersive Outfit Distribution");
		SKSEMenuFramework::AddSectionItem("Sets", RenderSets);
		SKSEMenuFramework::AddSectionItem("New set", RenderNewSet);
		SKSEMenuFramework::AddSectionItem("Vendors", RenderVendors);
		SKSEMenuFramework::AddSectionItem("Settings", RenderSettings);
		spdlog::info("Settings page registered with SKSE Menu Framework");
	}
}
