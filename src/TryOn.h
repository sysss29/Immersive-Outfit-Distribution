#pragma once

namespace IOD::TryOn
{
	// Temporarily dresses the player in some pieces, for the triage window. Pieces the player
	// didn't own are added for the try-on and removed again; what the player wore before comes
	// back on TakeOff. Game thread only.
	void Wear(const std::vector<RE::TESObjectARMO*>& a_pieces);
	void TakeOff();
	bool Active();
	void TurnPlayer(float a_degrees);  // to see the outfit from another side
}

namespace IOD::Triage
{
	// What has been looked at, in Data/SKSE/Plugins/ImmersiveOutfitDistribution.triage.json.
	// "Kept" isn't stored: an outfit is kept when a set uses it.
	struct Progress
	{
		std::set<std::string> skipped;   // outfit refs, or "<plugin>|pieces" for piece-only mods
		std::set<std::string> reviewed;  // plugin file names

		static Progress& Get();
		void             Load();
		void             Save() const;
	};
}
