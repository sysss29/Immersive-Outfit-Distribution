#pragma once

namespace IOD
{
	// Player settings, in Data/SKSE/Plugins/ImmersiveOutfitDistribution.json (written by the
	// in-game settings page; never shipped, so updates keep it).
	struct Settings
	{
		bool arrivalMessage{ true };       // "New arrivals at ..." when talking to a restocked vendor
		bool guaranteeOnePerVendor{ false };  // testing aid: every vendor draws at least one set

		static Settings& Get();
		void             Load();
		bool             Save() const;
	};
}
