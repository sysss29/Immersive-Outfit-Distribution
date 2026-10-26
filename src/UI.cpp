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
				const std::string label = fmt::format("{} ({})", SourceName(source), SourceWhere(source));
				if (ImGui::Checkbox(label.c_str(), &g_draft.sources[static_cast<std::size_t>(source)])) {
					g_draft.dirty = true;
				}
				if (const auto warning = SourceWarning(resolved.pieces, source); !warning.empty()) {
					ImGui::SameLine();
					ImGui::TextColored(g_draft.sources[static_cast<std::size_t>(source)] ? kWarn : kDim, "%s", warning.c_str());
				}
				ImGui::PopID();
			}
			int shrineCount = 0;
			for (const bool on : g_draft.gods) {
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
					auto& on = g_draft.gods[static_cast<std::size_t>(god)];
					if (ImGui::Checkbox(kGods[static_cast<std::size_t>(god)], &on)) {
						g_draft.dirty = true;
					}
					ImGui::SameLine(160.0f);
					if (!Shrines::GetSingleton()->HasShrine(god)) {
						ImGui::TextColored(on ? kWarn : kDim, "no shrine in your game");
					} else {
						ImGui::TextColored(on ? kGood : kDim, "%s", IsDivine(god) ? "gift, when the requirement is met" : OfferingText(g_draft, god).c_str());
					}
					ImGui::PopID();
				}
				ImGui::TreePop();
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
				ImGui::SetNextItemWidth(140.0f);
				g_draft.dirty |= ImGui::InputInt("at least", &g_draft.reqValue, 1, 10);
				g_draft.reqValue = std::clamp(g_draft.reqValue, 1, 999);
				break;
			case kReqHealth:
			case kReqMagicka:
			case kReqStamina:
				ImGui::SetNextItemWidth(140.0f);
				if (ImGui::InputInt("at least", &g_draft.reqValue, 50, 100)) {
					g_draft.reqValue = std::max(0, (g_draft.reqValue + 25) / 50 * 50);  // steps of 50
					g_draft.dirty = true;
				}
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
					const char* ref = g_draft.reqFaction == kFactionOther ? g_draft.reqRef : kFactions[static_cast<std::size_t>(g_draft.reqFaction)].ref;
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
					g_draft.dirty |= ImGui::InputInt("rank at least", &g_draft.reqValue, 1, 1);
					g_draft.reqValue = std::clamp(g_draft.reqValue, 0, maxRank);
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
					g_draft.dirty |= ImGui::Combo("Quest", &g_draft.reqQuest, items.data(), static_cast<int>(items.size()));
					if (g_draft.reqQuest != kQuestOther) {
						break;
					}
					g_draft.dirty |= ImGui::InputTextWithHint("Quest form", "MQ202 or Plugin.esp|0x123", g_draft.reqRef, sizeof(g_draft.reqRef));
					auto* form = LookupFormString(g_draft.reqRef);
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
					if (ImGui::Combo("Skill tree", &g_draft.reqSkill, kSkills.data(), static_cast<int>(kSkills.size()))) {
						g_draft.reqPerk = 0;
						g_draft.dirty = true;
					}
					const auto               perks = PerksOfTree(g_draft.reqSkill);
					std::vector<std::string> labels;
					for (auto* perk : perks) {
						labels.push_back(PerkLabel(perk));
					}
					labels.emplace_back("Other (enter editor ID or form)");
					std::vector<const char*> items;
					for (const auto& l : labels) {
						items.push_back(l.c_str());
					}
					int index = g_draft.reqPerk < 0 ? static_cast<int>(perks.size()) : g_draft.reqPerk;
					ImGui::SetNextItemWidth(260.0f);
					if (ImGui::Combo("Perk", &index, items.data(), static_cast<int>(items.size()))) {
						g_draft.reqPerk = index >= static_cast<int>(perks.size()) ? -1 : index;
						g_draft.dirty = true;
					}
					if (g_draft.reqPerk < 0) {
						g_draft.dirty |= ImGui::InputTextWithHint("Perk form", "Plugin.esp|0x123", g_draft.reqRef, sizeof(g_draft.reqRef));
					}
					if (auto* perk = DraftPerk(g_draft)) {
						ImGui::TextColored(kGood, "Perk: %s", PerkLabel(perk).c_str());
					} else {
						ImGui::TextColored(kWarn, "No perk found; the set stays hidden until this is fixed.");
					}
					break;
				}
			case kReqBlessing:
				ImGui::SetNextItemWidth(220.0f);
				g_draft.dirty |= ImGui::Combo("God", &g_draft.reqGod, kGods.data(), static_cast<int>(kGods.size()));
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
				SaveDraft(g_draft);
			}
			ImGui::SameLine();
			if (ImGui::Button("Undo changes")) {
				if (g_draft.index == SIZE_MAX) {
					g_draft = Draft{};
				} else {
					LoadDraft(g_draft, g_draft.file, g_draft.index);
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
					DeleteDraft(g_draft);
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
				RenderEditor();
			}
		}

		Picker g_newPicker;
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
		SKSEMenuFramework::AddSectionItem("Vendors", RenderVendors);
		SKSEMenuFramework::AddSectionItem("Settings", RenderSettings);
		spdlog::info("Settings page registered with SKSE Menu Framework");
	}
}
