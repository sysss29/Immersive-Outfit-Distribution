#pragma once

namespace IOD
{
	// The Nine Divines, then the Daedric princes. A god is referred to by its index here, in the
	// save (current blessing, cooldowns) and in requirements; set files use the names.
	inline constexpr std::array kGods{ "Akatosh", "Arkay", "Dibella", "Julianos", "Kynareth", "Mara", "Stendarr", "Talos",
		"Zenithar", "Azura", "Boethiah", "Clavicus Vile", "Hermaeus Mora", "Hircine", "Malacath", "Mehrunes Dagon", "Mephala",
		"Meridia", "Molag Bal", "Namira", "Nocturnal", "Peryite", "Sanguine", "Sheogorath", "Vaermina" };
	inline constexpr int kDivineCount = 9;

	// "Dibella", "dibella", "Shrine of Dibella" -> index
	std::optional<int> ParseGod(std::string_view a_text);
	const char*        GodName(int a_god);
	inline bool        IsDivine(int a_god) { return a_god >= 0 && a_god < kDivineCount; }
}
