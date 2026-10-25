#include "Papyrus.h"

#include "Catalog.h"
#include "SetEdit.h"
#include "SetFiles.h"
#include "Settings.h"
#include "Sources.h"
#include "Stocker.h"

// Global native functions behind the MCM (script IOD_Native). Sets are addressed by their position
// in the list SetNames() returns. Every change is saved to the set file at once.
namespace IOD::Papyrus
{
	namespace
	{
		using namespace IOD::Edit;
		using Tag = RE::StaticFunctionTag;
		using String = RE::BSFixedString;
		constexpr auto kScript = "IOD_Native"sv;

		// SkyUI shows MCM text as HTML: angle brackets would break the line.
		std::string Safe(std::string a_text)
		{
			std::ranges::replace(a_text, '<', '(');
			std::ranges::replace(a_text, '>', ')');
			return a_text;
		}

		std::string Lower(std::string_view a_text)
		{
			std::string out(a_text);
			std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		std::optional<std::pair<std::size_t, std::size_t>> At(std::int32_t a_index)
		{
			const auto all = AllSets();
			if (a_index < 0 || static_cast<std::size_t>(a_index) >= all.size()) {
				return std::nullopt;
			}
			return all[static_cast<std::size_t>(a_index)];
		}

		bool Load(Draft& a_draft, std::int32_t a_index)
		{
			const auto at = At(a_index);
			if (!at) {
				return false;
			}
			LoadDraft(a_draft, at->first, at->second);
			return true;
		}

		Source SourceOf(std::string_view a_field)  // "src0".."src2"
		{
			const auto n = a_field.size() > 3 ? a_field[3] - '0' : 0;
			return kAllSources[static_cast<std::size_t>(std::clamp(n, 0, 2))];
		}

		std::string StatusOf(const nlohmann::json& a_entry)
		{
			const auto resolved = Resolve(a_entry);
			if (!a_entry.value("enabled", true)) {
				return "disabled";
			}
			if (resolved.pieces.empty()) {
				return "mod not loaded";
			}
			bool sold = false;
			if (a_entry.contains("sources") && a_entry["sources"].is_array()) {
				for (const auto& s : a_entry["sources"]) {
					if (const auto source = s.is_string() ? ParseSource(s.get<std::string>()) : std::nullopt) {
						sold = true;
						if (TradeCount(resolved.pieces, *source).first == 0) {
							return fmt::format("never at {}", SourceName(*source));
						}
					}
				}
			}
			return sold ? fmt::format("{} pieces", resolved.pieces.size()) : "not sold anywhere";
		}

		// --- settings ----------------------------------------------------------------------

		bool GetSetting(Tag*, String a_key)
		{
			const auto key = Lower(a_key.c_str());
			auto&      s = Settings::Get();
			return key == "arrival" ? s.arrivalMessage : key == "guarantee" ? s.guaranteeOnePerVendor : false;
		}

		void SetSetting(Tag*, String a_key, bool a_value)
		{
			const auto key = Lower(a_key.c_str());
			auto&      s = Settings::Get();
			if (key == "arrival") {
				s.arrivalMessage = a_value;
			} else if (key == "guarantee") {
				s.guaranteeOnePerVendor = a_value;
			}
			s.Save();
		}

		// --- vendors -----------------------------------------------------------------------

		std::vector<String> VendorNames(Tag*)
		{
			std::vector<String> out;
			for (const auto& line : Stocker::Snapshot().second) {
				out.emplace_back(Safe(line.vendor));
			}
			return out;
		}

		std::vector<String> VendorStock(Tag*)
		{
			std::vector<String> out;
			for (const auto& line : Stocker::Snapshot().second) {
				std::string sets;
				for (const auto& s : line.sets) {
					sets += (sets.empty() ? "" : ", ") + s;
				}
				out.emplace_back(Safe(sets.empty() ? "nothing" : sets));
			}
			return out;
		}

		std::int32_t HoursToNextDraw(Tag*)
		{
			auto* calendar = RE::Calendar::GetSingleton();
			if (!calendar) {
				return 0;
			}
			const float days = calendar->GetDaysPassed();
			const float next = (std::floor(days / 2.0f) + 1.0f) * 2.0f;
			return static_cast<std::int32_t>(std::ceil((next - days) * 24.0f));
		}

		void Redraw(Tag*)
		{
			SKSE::GetTaskInterface()->AddTask([]() { Stocker::Redraw(); });
		}

		// --- sets --------------------------------------------------------------------------

		std::vector<String> SetNames(Tag*)
		{
			std::scoped_lock    lock(Lock());
			std::vector<String> out;
			auto&               files = SetFiles::Get().Files();
			for (const auto& [f, i] : AllSets()) {
				out.emplace_back(Safe(files[f].Sets()[i].value("name", "(no name)")));
			}
			return out;
		}

		std::vector<String> SetStatuses(Tag*)
		{
			std::scoped_lock    lock(Lock());
			std::vector<String> out;
			auto&               files = SetFiles::Get().Files();
			for (const auto& [f, i] : AllSets()) {
				out.emplace_back(Safe(StatusOf(files[f].Sets()[i])));
			}
			return out;
		}

		bool GetBool(Tag*, std::int32_t a_index, String a_field)
		{
			std::scoped_lock lock(Lock());
			Draft            d;
			if (!Load(d, a_index)) {
				return false;
			}
			const auto field = Lower(a_field.c_str());
			if (field == "enabled") {
				return d.enabled;
			}
			if (field == "unique") {
				return d.unique;
			}
			if (field == "hasprice") {
				return d.hasPrice;
			}
			if (field.starts_with("src")) {
				return d.sources[static_cast<std::size_t>(SourceOf(field))];
			}
			return false;
		}

		String SetBool(Tag*, std::int32_t a_index, String a_field, bool a_value)
		{
			std::scoped_lock lock(Lock());
			Draft            d;
			if (!Load(d, a_index)) {
				return "That set no longer exists.";
			}
			const auto field = Lower(a_field.c_str());
			if (field == "enabled") {
				d.enabled = a_value;
			} else if (field == "unique") {
				d.unique = a_value;
			} else if (field == "hasprice") {
				d.hasPrice = a_value;
			} else if (field.starts_with("src")) {
				d.sources[static_cast<std::size_t>(SourceOf(field))] = a_value;
			}
			SaveDraft(d);
			spdlog::info("MCM: set {} {} = {} ({})", a_index, field, a_value, d.status);
			return Safe(d.status);
		}

		std::int32_t GetInt(Tag*, std::int32_t a_index, String a_field)
		{
			std::scoped_lock lock(Lock());
			Draft            d;
			if (!Load(d, a_index)) {
				return 0;
			}
			const auto field = Lower(a_field.c_str());
			if (field == "rarity") {
				return d.rarity;
			}
			if (field == "price") {
				return d.price;
			}
			if (field == "reqtype") {
				return d.reqType;
			}
			if (field == "reqvalue") {
				return d.reqValue;
			}
			if (field == "reqskill") {
				return d.reqSkill;
			}
			if (field == "reqfaction") {
				return d.reqFaction;
			}
			if (field == "reqgod") {
				return d.reqGod;
			}
			if (field == "reqquest") {
				return d.reqQuest;
			}
			if (field == "maxrank") {
				return MaxFactionRank(d);
			}
			if (field == "pieces") {
				return static_cast<std::int32_t>(Resolve(d.outfit, d.pieces).pieces.size());
			}
			return 0;
		}

		String SetInt(Tag*, std::int32_t a_index, String a_field, std::int32_t a_value)
		{
			std::scoped_lock lock(Lock());
			Draft            d;
			if (!Load(d, a_index)) {
				return "That set no longer exists.";
			}
			const auto field = Lower(a_field.c_str());
			if (field == "rarity") {
				d.rarity = std::clamp(a_value, 0, static_cast<int>(kRarities.size()) - 1);
			} else if (field == "price") {
				d.hasPrice = true;
				d.price = std::max(0, a_value);
			} else if (field == "reqtype") {
				const auto type = std::clamp(a_value, 0, static_cast<int>(kRequirementTypes.size()) - 1);
				if (type != d.reqType) {
					d.reqType = type;
					d.reqValue = type == kReqLevel ? 10 : (type == kReqHealth || type == kReqMagicka || type == kReqStamina) ? 200 :
					             type == kReqSkill                                                                      ? 25 :
					                                                                                                          0;
				}
			} else if (field == "reqvalue") {
				d.reqValue = a_value;
				if (d.reqType == kReqFaction) {
					d.reqValue = std::clamp(a_value, 0, std::max(0, MaxFactionRank(d)));
				}
			} else if (field == "reqskill") {
				d.reqSkill = std::clamp(a_value, 0, static_cast<int>(kSkills.size()) - 1);
			} else if (field == "reqgod") {
				d.reqGod = std::clamp(a_value, 0, static_cast<int>(kGods.size()) - 1);
			} else if (field == "reqquest") {
				d.reqQuest = std::clamp(a_value, 0, kQuestOther);
			} else if (field == "reqfaction") {
				d.reqFaction = std::clamp(a_value, 0, kFactionOther);
				d.reqValue = std::clamp(d.reqValue, 0, std::max(0, MaxFactionRank(d)));
			}
			SaveDraft(d);
			spdlog::info("MCM: set {} {} = {} ({})", a_index, field, a_value, d.status);
			return Safe(d.status);
		}

		String GetText(Tag*, std::int32_t a_index, String a_field)
		{
			std::scoped_lock lock(Lock());
			Draft            d;
			if (!Load(d, a_index)) {
				return "";
			}
			const auto field = Lower(a_field.c_str());
			const auto resolved = Resolve(d.outfit, d.pieces);
			if (field == "name") {
				return Safe(d.name);
			}
			if (field == "reqref") {
				return Safe(d.reqRef);
			}
			if (field == "reqrefcheck") {  // what the reference points to
				auto* form = LookupFormString(d.reqQuest == kQuestOther ? std::string_view(d.reqRef) : std::string_view(kQuests[static_cast<std::size_t>(d.reqQuest)].ref));
				if (d.reqType == kReqQuest) {
					auto* quest = form ? form->As<RE::TESQuest>() : nullptr;
					return quest ? Safe(fmt::format("Quest: {}", quest->GetName())) : "No quest found with that ID.";
				}
				return "";
			}
			if (field.starts_with("warn")) {  // warn0..warn2: why a source can't sell this set
				const auto n = std::clamp(field.size() > 4 ? field[4] - '0' : 0, 0, 2);
				return Safe(SourceWarning(resolved.pieces, kAllSources[static_cast<std::size_t>(n)]));
			}
			if (field == "pieces") {
				std::string text;
				for (auto* armor : resolved.pieces) {
					text += (text.empty() ? "" : ", ") + fmt::format("{} [{}]", armor->GetName(), Slots(armor));
				}
				if (resolved.missing > 0) {
					text += fmt::format(" (+{} not loaded)", resolved.missing);
				}
				return Safe(text);
			}
			if (field == "file") {
				return Safe(SetFiles::Get().Files()[d.file].stem + ".json");
			}
			return "";
		}

		String SetText(Tag*, std::int32_t a_index, String a_field, String a_value)
		{
			std::scoped_lock lock(Lock());
			Draft            d;
			if (!Load(d, a_index)) {
				return "That set no longer exists.";
			}
			const auto field = Lower(a_field.c_str());
			if (field == "reqref") {
				CopyTo(d.reqRef, a_value.c_str());
			} else if (field == "name") {
				CopyTo(d.name, a_value.c_str());
			}
			SaveDraft(d);
			return Safe(d.status);
		}

		std::vector<String> Choices(Tag*, String a_list)
		{
			const auto          list = Lower(a_list.c_str());
			std::vector<String> out;
			if (list == "rarity") {
				for (const auto* r : kRarities) {
					out.emplace_back(r);
				}
			} else if (list == "reqtype") {
				for (const auto* r : kRequirementTypes) {
					out.emplace_back(r);
				}
			} else if (list == "skills") {
				for (const auto* s : kSkills) {
					out.emplace_back(s);
				}
			} else if (list == "gods") {
				for (const auto* g : kGods) {
					out.emplace_back(g);
				}
			} else if (list == "quests") {
				for (int i = 0; i <= kQuestOther; ++i) {
					out.emplace_back(Safe(QuestLabel(i)));
				}
			} else if (list == "factions") {
				for (const auto& f : kFactions) {
					out.emplace_back(f.label);
				}
			}
			spdlog::info("MCM asked for list \"{}\": {} entries", list, out.size());
			return out;
		}

		void ReloadFiles(Tag*)
		{
			std::scoped_lock lock(Lock());
			SetFiles::Get().Load();
			ApplyChanges();
		}

		bool Register(RE::BSScript::IVirtualMachine* a_vm)
		{
			a_vm->RegisterFunction("GetSetting", kScript, GetSetting);
			a_vm->RegisterFunction("SetSetting", kScript, SetSetting);
			a_vm->RegisterFunction("VendorNames", kScript, VendorNames);
			a_vm->RegisterFunction("VendorStock", kScript, VendorStock);
			a_vm->RegisterFunction("HoursToNextDraw", kScript, HoursToNextDraw);
			a_vm->RegisterFunction("Redraw", kScript, Redraw);
			a_vm->RegisterFunction("SetNames", kScript, SetNames);
			a_vm->RegisterFunction("SetStatuses", kScript, SetStatuses);
			a_vm->RegisterFunction("GetBool", kScript, GetBool);
			a_vm->RegisterFunction("SetBool", kScript, SetBool);
			a_vm->RegisterFunction("GetInt", kScript, GetInt);
			a_vm->RegisterFunction("SetInt", kScript, SetInt);
			a_vm->RegisterFunction("GetText", kScript, GetText);
			a_vm->RegisterFunction("SetText", kScript, SetText);
			a_vm->RegisterFunction("Choices", kScript, Choices);
			a_vm->RegisterFunction("ReloadFiles", kScript, ReloadFiles);
			spdlog::info("Papyrus functions registered for the MCM");
			return true;
		}
	}

	void Install()
	{
		SKSE::GetPapyrusInterface()->Register(Register);
	}
}
