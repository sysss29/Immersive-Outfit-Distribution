#include "UI.h"

#include "Catalog.h"
#include "SetEdit.h"
#include "SetFiles.h"
#include "Settings.h"
#include "Shrines.h"
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
		using namespace IOD::Edit;

		Draft g_draft;  // the set open in the editor

		constexpr ImGui::ImVec4 kWarn{ 1.0f, 0.62f, 0.25f, 1.0f };
		constexpr ImGui::ImVec4 kBad{ 1.0f, 0.38f, 0.38f, 1.0f };
		constexpr ImGui::ImVec4 kGood{ 0.55f, 0.85f, 0.55f, 1.0f };
		constexpr ImGui::ImVec4 kDim{ 0.62f, 0.62f, 0.62f, 1.0f };

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

		char        g_setFilter[64]{};
		std::string g_splitStatus;
		Picker g_addPicker;  // "Add pieces" inside the editor

		// "(mod default)" when the field comes from the mod's defaults, or a button to go back to it.
		void InheritTag(Draft& d, Inherit a_field)
		{
			if (d.isDefaults) {
				return;
			}
			ImGui::SameLine();
			if (d.inherit[a_field]) {
				ImGui::TextColored(kDim, "(mod default)");
			} else if (FileHasDefault(d.file, a_field)) {
				ImGui::PushID(3000 + a_field);
				if (ImGui::SmallButton("Use mod default")) {
					UseDefault(d, a_field);
				}
				ImGui::PopID();
			}
		}

		void RenderEditor(Draft& d)
		{
			auto& store = SetFiles::Get();
			auto& file = store.Files()[d.file];
			ImGui::Spacing();
			if (d.isDefaults) {
				ImGui::SeparatorText(fmt::format("Defaults for {}.json", file.stem).c_str());
				ImGui::TextWrapped("Every set in this file that doesn't set a field itself uses these. Change them here and all of the mod's sets follow.");
			} else {
				ImGui::SeparatorText(d.index == SIZE_MAX ? "New set (not saved yet)" : "Edit set");
				ImGui::TextDisabled("File: %s.json", file.stem.c_str());
			}
			if (const auto modStem = d.isDefaults ? file.stem : ModStemOf(d); modStem != file.stem && modStem != "MyOutfits") {
				ImGui::SameLine();
				const auto label = fmt::format("Move to {}.json", modStem);
				if (ImGui::SmallButton(label.c_str())) {
					if (d.index == SIZE_MAX) {
						d.file = store.FileFor(modStem);
					} else {
						MoveDraftToFile(d, store.FileFor(modStem));
					}
				}
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("One file per armour mod: easy to back up, share, or reuse in a new game.");
				}
			}

			const auto resolved = Resolve(d.outfit, d.pieces);

			if (!d.isDefaults) {
				if (ImGui::InputText("Name", d.name, sizeof(d.name))) {
					d.dirty = true;
				}
				if (ImGui::Checkbox("Enabled", &d.enabled)) {
					d.dirty = true;
				}
				ImGui::SameLine();
				if (ImGui::Checkbox("Unique (leaves rotation once you own every piece)", &d.unique)) {
					d.dirty = true;
				}
			}

			const auto sourcesBefore = std::pair{ d.sources, d.gods };
			ImGui::Text("Sold at:");
			InheritTag(d, kInheritSources);
			for (const auto source : kAllSources) {
				ImGui::PushID(static_cast<int>(source));
				const std::string label = fmt::format("{} ({})", SourceName(source), SourceWhere(source));
				if (ImGui::Checkbox(label.c_str(), &d.sources[static_cast<std::size_t>(source)])) {
					d.dirty = true;
				}
				if (const auto warning = SourceWarning(resolved.pieces, source); !warning.empty()) {
					ImGui::SameLine();
					ImGui::TextColored(d.sources[static_cast<std::size_t>(source)] ? kWarn : kDim, "%s", warning.c_str());
				}
				ImGui::PopID();
			}
			int shrineCount = 0;
			for (const bool on : d.gods) {
				shrineCount += on ? 1 : 0;
			}
			const auto shrineLabel = fmt::format("Granted at shrines ({})###shrines", shrineCount);
			if (ImGui::TreeNode(shrineLabel.c_str())) {
				ImGui::TextDisabled("Daedra: the tribute is shown at their statue and you choose. Divines: the set is a gift, given at their shrine once its requirement (e.g. a quest) is met.");
				for (int god = 0; god < static_cast<int>(kGods.size()); ++god) {
					if (god == 0) {
						ImGui::SeparatorText("The Nine Divines");
					} else if (god == kDivineCount) {
						ImGui::SeparatorText("Daedric princes");
					}
					ImGui::PushID(1000 + god);
					auto& on = d.gods[static_cast<std::size_t>(god)];
					if (ImGui::Checkbox(kGods[static_cast<std::size_t>(god)], &on)) {
						d.dirty = true;
					}
					ImGui::SameLine(160.0f);
					if (!Shrines::GetSingleton()->HasShrine(god)) {
						ImGui::TextColored(on ? kWarn : kDim, "no shrine in your game");
					} else {
						ImGui::TextColored(on ? kGood : kDim, "%s", IsDivine(god) ? "gift, when the requirement is met" : OfferingText(d, god).c_str());
					}
					ImGui::PopID();
				}

				// The set's own tribute (Daedra), instead of the god's default for its rarity.
				ImGui::Spacing();
				if (ImGui::Checkbox("Own offering (instead of the god's default for this rarity)", &d.ownOffering)) {
					d.dirty = true;
				}
				if (d.ownOffering) {
					static int  s_item = 0;
					static char s_ref[128]{};
					int         remove = -1;
					for (int i = 0; i < static_cast<int>(d.offering.size()); ++i) {
						auto& line = d.offering[static_cast<std::size_t>(i)];
						ImGui::PushID(2000 + i);
						ImGui::SetNextItemWidth(110.0f);
						if (ImGui::InputInt("##count", &line.count, 1, 10)) {
							line.count = std::max(1, line.count);
							d.dirty = true;
						}
						ImGui::SameLine();
						auto*       form = line.refs.empty() ? nullptr : LookupFormString(line.refs.front());
						const char* name = form ? (form->GetFormID() == 0xF ? "gold" : form->GetName()) : "not loaded";
						ImGui::Text("%s%s", name, line.refs.size() > 1 ? " (or similar)" : "");
						ImGui::SameLine();
						if (ImGui::SmallButton("Remove")) {
							remove = i;
						}
						ImGui::PopID();
					}
					if (remove >= 0) {
						d.offering.erase(d.offering.begin() + remove);
						d.dirty = true;
					}
					const auto               known = KnownOfferingItems();
					std::vector<const char*> labels;
					for (const auto& item : known) {
						labels.push_back(item.first.c_str());
					}
					labels.push_back("Other (enter editor ID or form)");
					ImGui::SetNextItemWidth(240.0f);
					ImGui::Combo("##offeritem", &s_item, labels.data(), static_cast<int>(labels.size()));
					const bool other = s_item >= static_cast<int>(known.size());
					if (other) {
						ImGui::SameLine();
						ImGui::SetNextItemWidth(200.0f);
						ImGui::InputTextWithHint("##offerref", "Skyrim.esm|0x63B45", s_ref, sizeof(s_ref));
					}
					ImGui::SameLine();
					if (ImGui::Button("Add to offering")) {
						Draft::OfferingLine line;
						if (other) {
							if (LookupFormString(s_ref)) {
								line.refs.emplace_back(s_ref);
							}
						} else {
							line.refs = known[static_cast<std::size_t>(s_item)].second;
						}
						if (!line.refs.empty()) {
							d.offering.push_back(std::move(line));
							d.dirty = true;
						}
					}
					if (d.offering.empty()) {
						ImGui::TextColored(kWarn, "Add at least one item, or untick to use the god's default.");
					}
				}
				ImGui::TreePop();
			}

			if (std::pair{ d.sources, d.gods } != sourcesBefore) {
				d.inherit[kInheritSources] = false;
			}

			ImGui::SetNextItemWidth(160.0f);
			if (ImGui::Combo("Rarity", &d.rarity, kRarities.data(), static_cast<int>(kRarities.size()))) {
				d.dirty = true;
				d.inherit[kInheritRarity] = false;
			}
			InheritTag(d, kInheritRarity);
			const auto priceBefore = std::tuple{ d.hasPrice, d.price };
			if (ImGui::Checkbox(d.isDefaults ? "Set prices by rarity" : "Set price", &d.hasPrice)) {
				d.dirty = true;
			}
			InheritTag(d, kInheritPrice);
			if (d.isDefaults && d.hasPrice) {
				for (int r = 0; r < 3; ++r) {
					ImGui::PushID(4000 + r);
					ImGui::SetNextItemWidth(140.0f);
					const auto label = fmt::format("gold for a {} set", kRarities[static_cast<std::size_t>(r)]);
					if (ImGui::InputInt(label.c_str(), &d.priceByRarity[static_cast<std::size_t>(r)], 100, 1000)) {
						d.priceByRarity[static_cast<std::size_t>(r)] = std::max(0, d.priceByRarity[static_cast<std::size_t>(r)]);
						d.dirty = true;
					}
					ImGui::PopID();
				}
			} else if (d.hasPrice) {
				ImGui::SameLine();
				ImGui::SetNextItemWidth(140.0f);
				if (ImGui::InputInt("gold for the whole set", &d.price, 100, 1000)) {
					d.price = std::max(0, d.price);
					d.dirty = true;
				}
			} else {
				ImGui::SameLine();
				ImGui::TextDisabled("(pieces keep their own values)");
			}

			if (std::tuple{ d.hasPrice, d.price } != priceBefore) {
				d.inherit[kInheritPrice] = false;
			}

			const auto reqBefore = std::tuple{ d.reqType, d.reqValue, d.reqSkill, d.reqFaction, d.reqGod, d.reqQuest, d.reqPerk, std::string(d.reqRef) };
			ImGui::SetNextItemWidth(220.0f);
			if (ImGui::Combo("Requirement", &d.reqType, kRequirementTypes.data(), static_cast<int>(kRequirementTypes.size()))) {
				d.dirty = true;
			}
			InheritTag(d, kInheritRequirement);
			switch (d.reqType) {
			case kReqLevel:
				ImGui::SetNextItemWidth(140.0f);
				d.dirty |= ImGui::InputInt("at least", &d.reqValue, 1, 10);
				d.reqValue = std::clamp(d.reqValue, 1, 999);
				break;
			case kReqHealth:
			case kReqMagicka:
			case kReqStamina:
				ImGui::SetNextItemWidth(140.0f);
				if (ImGui::InputInt("at least", &d.reqValue, 50, 100)) {
					d.reqValue = std::max(0, (d.reqValue + 25) / 50 * 50);  // steps of 50
					d.dirty = true;
				}
				break;
			case kReqSkill:
				ImGui::SetNextItemWidth(180.0f);
				d.dirty |= ImGui::Combo("Skill", &d.reqSkill, kSkills.data(), static_cast<int>(kSkills.size()));
				ImGui::SetNextItemWidth(140.0f);
				d.dirty |= ImGui::InputInt("at least", &d.reqValue, 1, 10);
				break;
			case kReqFaction:
				{
					std::array<const char*, kFactions.size()> labels{};
					for (std::size_t i = 0; i < kFactions.size(); ++i) {
						labels[i] = kFactions[i].label;
					}
					ImGui::SetNextItemWidth(220.0f);
					d.dirty |= ImGui::Combo("Faction", &d.reqFaction, labels.data(), static_cast<int>(labels.size()));
					if (d.reqFaction == kFactionOther) {
						d.dirty |= ImGui::InputTextWithHint("Faction form", "Plugin.esp|0x123", d.reqRef, sizeof(d.reqRef));
					}
					const char* ref = d.reqFaction == kFactionOther ? d.reqRef : kFactions[static_cast<std::size_t>(d.reqFaction)].ref;
					auto*       form = LookupFormString(ref);
					auto*       faction = form ? form->As<RE::TESFaction>() : nullptr;
					if (!faction) {
						ImGui::TextColored(kWarn, "No faction found with that form ID.");
						break;
					}
					int ranks = 0;
					for ([[maybe_unused]] auto* rank : faction->rankData) {
						++ranks;
					}
					const int maxRank = std::max(0, ranks - 1);
					ImGui::SetNextItemWidth(140.0f);
					d.dirty |= ImGui::InputInt("rank at least", &d.reqValue, 1, 1);
					d.reqValue = std::clamp(d.reqValue, 0, maxRank);
					if (ranks > 1) {
						ImGui::TextDisabled("%s has ranks 0 (joined) to %d.", faction->GetName(), maxRank);
					} else {
						ImGui::TextDisabled("%s has no rank ladder: rank 0 means you are a member.", faction->GetName());
					}
					ImGui::TextDisabled("Vanilla guilds track progress through quests more than ranks: see Quest completed.");
					break;
				}
			case kReqQuest:
				{
					std::vector<std::string> labels;
					std::vector<const char*> items;
					for (int i = 0; i <= kQuestOther; ++i) {
						labels.push_back(QuestLabel(i));
					}
					for (const auto& l : labels) {
						items.push_back(l.c_str());
					}
					ImGui::SetNextItemWidth(320.0f);
					d.dirty |= ImGui::Combo("Quest", &d.reqQuest, items.data(), static_cast<int>(items.size()));
					if (d.reqQuest != kQuestOther) {
						break;
					}
					d.dirty |= ImGui::InputTextWithHint("Quest form", "MQ202 or Plugin.esp|0x123", d.reqRef, sizeof(d.reqRef));
					auto* form = LookupFormString(d.reqRef);
					auto* quest = form ? form->As<RE::TESQuest>() : nullptr;
					if (quest) {
						ImGui::TextColored(kGood, "Quest: %s", quest->GetName());
					} else {
						ImGui::TextColored(kWarn, "No quest found with that form ID.");
					}
					break;
				}
			case kReqPerk:
				{
					ImGui::SetNextItemWidth(180.0f);
					if (ImGui::Combo("Skill tree", &d.reqSkill, kSkills.data(), static_cast<int>(kSkills.size()))) {
						d.reqPerk = 0;
						d.dirty = true;
					}
					const auto               perks = PerksOfTree(d.reqSkill);
					std::vector<std::string> labels;
					for (auto* perk : perks) {
						labels.push_back(PerkLabel(perk));
					}
					labels.emplace_back("Other (enter editor ID or form)");
					std::vector<const char*> items;
					for (const auto& l : labels) {
						items.push_back(l.c_str());
					}
					int index = d.reqPerk < 0 ? static_cast<int>(perks.size()) : d.reqPerk;
					ImGui::SetNextItemWidth(260.0f);
					if (ImGui::Combo("Perk", &index, items.data(), static_cast<int>(items.size()))) {
						d.reqPerk = index >= static_cast<int>(perks.size()) ? -1 : index;
						d.dirty = true;
					}
					if (d.reqPerk < 0) {
						d.dirty |= ImGui::InputTextWithHint("Perk form", "Plugin.esp|0x123", d.reqRef, sizeof(d.reqRef));
					}
					if (auto* perk = DraftPerk(d)) {
						ImGui::TextColored(kGood, "Perk: %s", PerkLabel(perk).c_str());
					} else {
						ImGui::TextColored(kWarn, "No perk found; the set stays hidden until this is fixed.");
					}
					break;
				}
			case kReqBlessing:
				ImGui::SetNextItemWidth(220.0f);
				d.dirty |= ImGui::Combo("God", &d.reqGod, kGods.data(), static_cast<int>(kGods.size()));
				ImGui::TextColored(kWarn, "Shrines arrive in milestone 4; until then this set stays hidden.");
				break;
			default:
				break;
			}

			if (std::tuple{ d.reqType, d.reqValue, d.reqSkill, d.reqFaction, d.reqGod, d.reqQuest, d.reqPerk, std::string(d.reqRef) } != reqBefore) {
				d.inherit[kInheritRequirement] = false;
			}

			if (!d.isDefaults) {
				// Pieces
				ImGui::Spacing();
				ImGui::Text("Pieces (%zu):", resolved.pieces.size());
				if (resolved.missing > 0) {
					ImGui::SameLine();
					ImGui::TextColored(kBad, "%zu reference(s) not loaded (mod disabled?)", resolved.missing);
				}
				if (!d.outfit.empty()) {
					ImGui::TextDisabled("From outfit record %s", d.outfit.c_str());
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
					if (!d.outfit.empty()) {
						d.pieces.clear();
						for (auto* armor : resolved.pieces) {
							d.pieces.push_back(FormString(armor));
						}
						d.outfit.clear();
					}
					std::erase(d.pieces, FormString(remove));
					d.dirty = true;
				}
				if (ImGui::TreeNode("Add pieces")) {
					ModCombo(g_addPicker);
					PieceList(g_addPicker, 220.0f);
					ImGui::BeginDisabled(g_addPicker.chosen.empty());
					if (ImGui::Button("Add selected pieces to this set")) {
						for (auto* armor : g_addPicker.chosen) {
							const auto ref = FormString(armor);
							if (std::ranges::find(resolved.pieces, armor) == resolved.pieces.end()) {
								d.pieces.push_back(ref);
							}
						}
						g_addPicker.chosen.clear();
						d.dirty = true;
					}
					ImGui::EndDisabled();
					ImGui::TreePop();
				}
			}

			// Actions
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::BeginDisabled(!d.dirty);
			if (ImGui::Button("Save")) {
				SaveDraft(d);
			}
			ImGui::SameLine();
			if (ImGui::Button("Undo changes")) {
				if (d.isDefaults) {
					LoadDefaults(d, d.file);
				} else if (d.index == SIZE_MAX) {
					d = Draft{};
				} else {
					LoadDraft(d, d.file, d.index);
				}
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Close")) {
				d = Draft{};
				return;
			}
			ImGui::SameLine();
			if (d.isDefaults) {
				// defaults can't be deleted
			} else if (!d.confirmDelete) {
				if (ImGui::Button("Delete set")) {
					d.confirmDelete = true;
				}
			} else {
				if (ImGui::Button("Really delete?")) {
					DeleteDraft(d);
					return;
				}
				ImGui::SameLine();
				if (ImGui::Button("Keep it")) {
					d.confirmDelete = false;
				}
			}
			if (d.dirty) {
				ImGui::TextColored(kWarn, "Unsaved changes.");
			}
			if (!d.status.empty()) {
				ImGui::TextWrapped("%s", d.status.c_str());
			}
		}

		void __stdcall RenderSets()
		{
			std::scoped_lock lock(Lock());
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
			ImGui::SameLine();
			if (ImGui::Button("One file per armour mod")) {
				g_draft = Draft{};
				const auto moved = SplitByMod();
				g_splitStatus = moved ? fmt::format("Moved {} set(s) into their mod's file.", moved) : "Every set is already in its mod's file.";
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Moves each set into a file named after its armour mod (e.g. Cute Maid.json). Copy such a file to share or reuse a mod's sets.");
			}
			if (!g_splitStatus.empty()) {
				ImGui::SameLine();
				ImGui::TextDisabled("%s", g_splitStatus.c_str());
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
						const auto  entry = Effective(files[f].root.contains("defaults") ? files[f].root["defaults"] : nlohmann::json{}, sets[i], nullptr);
						const auto  name = entry.value("name", "(no name)");
						if (!Contains(name, g_setFilter)) {
							continue;
						}
						ImGui::PushID(static_cast<int>(f * 10000 + i));
						ImGui::TableNextRow();
						ImGui::TableNextColumn();
						const bool selected = g_draft.active && g_draft.file == f && g_draft.index == i;
						if (ImGui::Selectable(name.c_str(), selected, ImGui::ImGuiSelectableFlags_SpanAllColumns)) {
							LoadDraft(g_draft, f, i);
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
								} else if (const auto god = s.is_string() ? ParseGod(s.get<std::string>()) : std::nullopt) {
									where += (where.empty() ? "" : ", ") + std::string(GodName(*god));
									if (warning.empty() && !Shrines::GetSingleton()->HasShrine(*god)) {
										warning = fmt::format("no shrine of {}", GodName(*god));
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
				RenderEditor(g_draft);
			}
		}

		Picker      g_newPicker;
		std::string g_importStatus;
		Draft       g_defaults;  // the mod defaults being edited
		int         g_defaultsFile{ -1 };

		void __stdcall RenderDefaults()
		{
			std::scoped_lock lock(Lock());
			auto&            files = SetFiles::Get().Files();
			ImGui::TextWrapped("Each armour mod's file can hold defaults: where its sets are sold, their rarity, a price per rarity and a requirement. A set uses a default for every field it doesn't set itself, so you set up a mod once. In a set, \"Use mod default\" undoes an exception.");
			if (files.empty()) {
				ImGui::TextDisabled("No set files yet. Make a set first (New set).");
				return;
			}
			const char* preview = g_defaultsFile >= 0 && g_defaultsFile < static_cast<int>(files.size()) ? files[static_cast<std::size_t>(g_defaultsFile)].stem.c_str() : "Choose a mod file";
			ImGui::SetNextItemWidth(320.0f);
			if (ImGui::BeginCombo("Mod file", preview)) {
				for (int f = 0; f < static_cast<int>(files.size()); ++f) {
					const auto label = fmt::format("{}  ({} set(s){})##{}", files[static_cast<std::size_t>(f)].stem, files[static_cast<std::size_t>(f)].Sets().size(),
						files[static_cast<std::size_t>(f)].root.contains("defaults") ? ", has defaults" : "", f);
					if (ImGui::Selectable(label.c_str(), f == g_defaultsFile)) {
						g_defaultsFile = f;
						LoadDefaults(g_defaults, static_cast<std::size_t>(f));
					}
				}
				ImGui::EndCombo();
			}
			if (g_defaultsFile >= static_cast<int>(files.size())) {
				g_defaultsFile = -1;
				g_defaults = Draft{};
			}
			if (g_defaults.active && g_defaults.isDefaults) {
				RenderEditor(g_defaults);
				if (!g_defaults.active) {  // "Close"
					g_defaultsFile = -1;
				}
			}
		}
		char   g_newName[128]{};

		void __stdcall RenderNewSet()
		{
			std::scoped_lock lock(Lock());
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
					const auto importLabel = fmt::format("Make sets from all {} outfits", mod.outfits.size());
					if (ImGui::Button(importLabel.c_str())) {
						ImportOutfits(mod.outfits, g_importStatus);
					}
					if (ImGui::IsItemHovered()) {
						ImGui::SetTooltip("One set per outfit, in %s.json, using the mod's defaults (where sold, rarity, price, requirement). Outfits that already have a set are skipped.",
							SetFiles::StemForPlugin(mod.file).c_str());
					}
					if (!g_importStatus.empty()) {
						ImGui::TextWrapped("%s", g_importStatus.c_str());
					}
					ImGui::Separator();
					for (auto* outfit : mod.outfits) {
						const auto pieces = OutfitPieces(outfit);
						if (pieces.empty()) {
							continue;
						}
						ImGui::PushID(outfit);
						const auto label = OutfitLabel(outfit);
						if (ImGui::SmallButton("Make set")) {
							NewDraft(g_draft, label, FormString(outfit), {});
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
						NewDraft(g_draft, g_newName, {}, std::move(refs));
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
			std::scoped_lock lock(Lock());
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
			std::scoped_lock lock(Lock());
			auto& settings = Settings::Get();
			bool  changed = false;
			changed |= ImGui::Checkbox("Show \"New arrivals\" when a vendor has new sets", &settings.arrivalMessage);
			changed |= ImGui::Checkbox("Balance the draw between armour mods", &settings.balanceByMod);
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Each mod gets the same chance, however many sets it has; within a mod, rarity decides.");
			}
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
		SKSEMenuFramework::SetSection("Immersive Outfit Distribution");
		SKSEMenuFramework::AddSectionItem("Sets", RenderSets);
		SKSEMenuFramework::AddSectionItem("New set", RenderNewSet);
		SKSEMenuFramework::AddSectionItem("Mod defaults", RenderDefaults);
		SKSEMenuFramework::AddSectionItem("Vendors", RenderVendors);
		SKSEMenuFramework::AddSectionItem("Settings", RenderSettings);
		spdlog::info("Settings page registered with SKSE Menu Framework");
	}
}
