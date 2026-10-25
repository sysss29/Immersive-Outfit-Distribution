#pragma once

namespace IOD
{
	// At most one requirement per set (see the design spec).
	struct Requirement
	{
		enum class Type
		{
			kNone,
			kLevel,      // player level >= value
			kAttribute,  // base Health / Magicka / Stamina >= value
			kSkill,      // base skill level >= value
			kFaction,    // rank in faction >= value
			kQuest,      // quest completed
			kBlessing,   // current blessing (milestone 4; never met before then)
		};

		Type            type{ Type::kNone };
		RE::ActorValue  actorValue{ RE::ActorValue::kNone };
		RE::TESForm*    form{ nullptr };
		std::int32_t    value{ 0 };
		std::string     label;  // e.g. "Restoration 20", shown in logs and messages
	};

	// Parses {"type": "...", ...}; returns nullopt (and logs why) when it can't be used.
	std::optional<Requirement> ParseRequirement(const nlohmann::json& a_json, std::string_view a_context);

	bool IsMet(const Requirement& a_requirement, RE::PlayerCharacter* a_player);
}
