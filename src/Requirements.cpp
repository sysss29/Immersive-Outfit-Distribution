#include "Requirements.h"

#include "Catalog.h"

namespace IOD
{
	namespace
	{
		std::string Lower(std::string_view a_text)
		{
			std::string out(a_text);
			std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		std::optional<RE::ActorValue> ParseSkill(std::string_view a_name)
		{
			static const std::unordered_map<std::string, RE::ActorValue> skills{
				{ "onehanded", RE::ActorValue::kOneHanded },
				{ "twohanded", RE::ActorValue::kTwoHanded },
				{ "archery", RE::ActorValue::kArchery },
				{ "block", RE::ActorValue::kBlock },
				{ "smithing", RE::ActorValue::kSmithing },
				{ "heavyarmor", RE::ActorValue::kHeavyArmor },
				{ "lightarmor", RE::ActorValue::kLightArmor },
				{ "pickpocket", RE::ActorValue::kPickpocket },
				{ "lockpicking", RE::ActorValue::kLockpicking },
				{ "sneak", RE::ActorValue::kSneak },
				{ "alchemy", RE::ActorValue::kAlchemy },
				{ "speech", RE::ActorValue::kSpeech },
				{ "alteration", RE::ActorValue::kAlteration },
				{ "conjuration", RE::ActorValue::kConjuration },
				{ "destruction", RE::ActorValue::kDestruction },
				{ "illusion", RE::ActorValue::kIllusion },
				{ "restoration", RE::ActorValue::kRestoration },
				{ "enchanting", RE::ActorValue::kEnchanting },
			};
			std::string key;
			for (char c : Lower(a_name)) {
				if (std::isalpha(static_cast<unsigned char>(c))) {
					key.push_back(c);
				}
			}
			const auto it = skills.find(key);
			return it != skills.end() ? std::optional{ it->second } : std::nullopt;
		}
	}

	std::optional<Requirement> ParseRequirement(const nlohmann::json& a_json, std::string_view a_context)
	{
		if (!a_json.is_object() || !a_json.contains("type")) {
			spdlog::warn("{}: requirement needs a \"type\"", a_context);
			return std::nullopt;
		}
		Requirement req;
		const auto type = Lower(a_json.value("type", ""));
		req.value = a_json.value("value", a_json.value("rank", 0));

		if (type == "level") {
			req.type = Requirement::Type::kLevel;
			req.label = fmt::format("level {}", req.value);
		} else if (type == "health" || type == "magicka" || type == "stamina") {
			req.type = Requirement::Type::kAttribute;
			req.actorValue = type == "health" ? RE::ActorValue::kHealth : type == "magicka" ? RE::ActorValue::kMagicka : RE::ActorValue::kStamina;
			req.label = fmt::format("{} {}", type, req.value);
		} else if (type == "skill") {
			const auto skillName = a_json.value("skill", "");
			const auto skill = ParseSkill(skillName);
			if (!skill) {
				spdlog::warn("{}: unknown skill \"{}\"", a_context, skillName);
				return std::nullopt;
			}
			req.type = Requirement::Type::kSkill;
			req.actorValue = *skill;
			req.label = fmt::format("{} {}", skillName, req.value);
		} else if (type == "faction") {
			const auto ref = a_json.value("faction", "");
			req.form = LookupFormString(ref);
			if (!req.form || !req.form->As<RE::TESFaction>()) {
				spdlog::warn("{}: faction \"{}\" not found", a_context, ref);
				return std::nullopt;
			}
			req.type = Requirement::Type::kFaction;
			req.label = fmt::format("rank {} in {}", req.value, ref);
		} else if (type == "quest") {
			const auto ref = a_json.value("quest", "");
			req.form = LookupFormString(ref);
			if (!req.form || !req.form->As<RE::TESQuest>()) {
				spdlog::warn("{}: quest \"{}\" not found", a_context, ref);
				return std::nullopt;
			}
			req.type = Requirement::Type::kQuest;
			req.label = fmt::format("quest {} completed", ref);
		} else if (type == "perk") {
			const auto ref = a_json.value("perk", "");
			req.form = LookupFormString(ref);
			auto* perk = req.form ? req.form->As<RE::BGSPerk>() : nullptr;
			if (!perk) {
				spdlog::warn("{}: perk \"{}\" not found", a_context, ref);
				return std::nullopt;
			}
			req.type = Requirement::Type::kPerk;
			req.label = fmt::format("perk {}", perk->GetName());
		} else if (type == "blessing") {
			req.type = Requirement::Type::kBlessing;
			req.label = fmt::format("blessed by {}", a_json.value("god", "?"));
			spdlog::info("{}: blessing requirements arrive with the shrines (milestone 4); hidden until then", a_context);
		} else {
			spdlog::warn("{}: unknown requirement type \"{}\"", a_context, type);
			return std::nullopt;
		}
		return req;
	}

	bool IsMet(const Requirement& a_requirement, RE::PlayerCharacter* a_player)
	{
		if (!a_player) {
			return false;
		}
		switch (a_requirement.type) {
		case Requirement::Type::kNone:
			return true;
		case Requirement::Type::kLevel:
			return a_player->GetLevel() >= a_requirement.value;
		case Requirement::Type::kAttribute:
		case Requirement::Type::kSkill:
			{
				auto* owner = a_player->AsActorValueOwner();
				return owner && owner->GetBaseActorValue(a_requirement.actorValue) >= static_cast<float>(a_requirement.value);
			}
		case Requirement::Type::kFaction:
			{
				auto* faction = a_requirement.form ? a_requirement.form->As<RE::TESFaction>() : nullptr;
				return faction && a_player->GetFactionRank(faction, true) >= a_requirement.value;
			}
		case Requirement::Type::kQuest:
			{
				auto* quest = a_requirement.form ? a_requirement.form->As<RE::TESQuest>() : nullptr;
				return quest && quest->IsCompleted();
			}
		case Requirement::Type::kPerk:
			{
				auto* perk = a_requirement.form ? a_requirement.form->As<RE::BGSPerk>() : nullptr;
				return perk && a_player->HasPerk(perk);
			}
		case Requirement::Type::kBlessing:
		case Requirement::Type::kInvalid:
			return false;
		}
		return false;
	}
}
