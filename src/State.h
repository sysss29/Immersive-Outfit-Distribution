#pragma once

namespace IOD
{
	// Per-save state, stored in the SKSE co-save next to the game save.
	struct State
	{
		std::uint32_t salt{ 0 };          // per playthrough, so draws differ between games
		std::int32_t  allocPeriod{ -1 };  // 48-hour period the allocation below belongs to
		std::map<RE::FormID, std::vector<std::string>> allocation;  // vendor faction -> set ids
		std::map<RE::FormID, std::int32_t>             stocked;     // vendor faction -> period stocked
		std::set<std::string>                          owned;       // set ids fully owned once (or granted by a god)
		std::int32_t                                   blessing{ -1 };  // god index (Gods.h), -1: none
		std::map<std::int32_t, float>                  lastOffering;    // unused since 0.4.1 (no cooldown); kept in saves
		std::set<std::int32_t>                         noticed;         // gods whose statue has hinted at its sets
		std::set<std::string>                          announced;       // Divine gifts whose deed has been announced

		static State& Get();
		static void   Register();  // SKSE serialization callbacks

		void Reset();
	};
}
