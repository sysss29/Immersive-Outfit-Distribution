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
		std::set<std::string>                          owned;       // set ids fully owned once

		static State& Get();
		static void   Register();  // SKSE serialization callbacks

		void Reset();
	};
}
