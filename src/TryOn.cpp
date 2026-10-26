#include "TryOn.h"

namespace IOD::TryOn
{
	namespace
	{
		std::vector<RE::TESObjectARMO*> g_wornBefore;  // what to put back on
		std::vector<RE::TESObjectARMO*> g_trying;      // pieces equipped for the try-on
		std::vector<RE::TESObjectARMO*> g_added;       // of those, the ones IOD added
		bool                            g_active = false;

		std::vector<RE::TESObjectARMO*> WornArmour(RE::PlayerCharacter* a_player)
		{
			std::vector<RE::TESObjectARMO*> worn;
			for (const auto& [object, data] : a_player->GetInventory()) {
				auto* armor = object ? object->As<RE::TESObjectARMO>() : nullptr;
				if (armor && data.first > 0 && data.second && data.second->IsWorn()) {
					worn.push_back(armor);
				}
			}
			return worn;
		}

		void Unequip(RE::PlayerCharacter* a_player, RE::TESObjectARMO* a_armor)
		{
			RE::ActorEquipManager::GetSingleton()->UnequipObject(a_player, a_armor, nullptr, 1, nullptr, false, true, false, true);
		}

		void Equip(RE::PlayerCharacter* a_player, RE::TESObjectARMO* a_armor)
		{
			RE::ActorEquipManager::GetSingleton()->EquipObject(a_player, a_armor, nullptr, 1, nullptr, false, true, false, true);
		}

		// Removes the current try-on without dressing the player again.
		void Undress(RE::PlayerCharacter* a_player)
		{
			for (auto* armor : g_trying) {
				Unequip(a_player, armor);
			}
			for (auto* armor : g_added) {
				a_player->RemoveItem(armor, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
			}
			g_trying.clear();
			g_added.clear();
		}
	}

	void Wear(const std::vector<RE::TESObjectARMO*>& a_pieces)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		if (!g_active) {
			g_wornBefore = WornArmour(player);
			g_active = true;
			if (auto* camera = RE::PlayerCamera::GetSingleton(); camera && camera->IsInFirstPerson()) {
				camera->ForceThirdPerson();
			}
		}
		Undress(player);
		const auto counts = player->GetInventoryCounts();
		for (auto* armor : a_pieces) {
			if (!armor) {
				continue;
			}
			if (const auto it = counts.find(armor); it == counts.end() || it->second <= 0) {
				player->AddObjectToContainer(armor, nullptr, 1, nullptr);
				g_added.push_back(armor);
			}
			Equip(player, armor);
			g_trying.push_back(armor);
		}
		spdlog::info("Trying on {} piece(s) ({} lent)", g_trying.size(), g_added.size());
	}

	void TakeOff()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || !g_active) {
			return;
		}
		Undress(player);
		for (auto* armor : g_wornBefore) {
			Equip(player, armor);
		}
		g_wornBefore.clear();
		g_active = false;
		spdlog::info("Try-on ended; previous clothes back on");
	}

	bool Active()
	{
		return g_active;
	}

	void TurnPlayer(float a_degrees)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		constexpr float kPi = 3.14159265f;
		player->SetAngle({ player->GetAngleX(), player->GetAngleY(), player->GetAngleZ() + a_degrees * kPi / 180.0f });
	}
}

namespace IOD::Triage
{
	namespace
	{
		constexpr auto kPath = "Data/SKSE/Plugins/ImmersiveOutfitDistribution.triage.json"sv;
	}

	Progress& Progress::Get()
	{
		static Progress progress;
		return progress;
	}

	void Progress::Load()
	{
		std::ifstream in{ std::filesystem::path{ kPath } };
		if (!in) {
			return;
		}
		try {
			const auto root = nlohmann::json::parse(in, nullptr, true, true);
			for (const auto& s : root.value("skipped", nlohmann::json::array())) {
				if (s.is_string()) {
					skipped.insert(s.get<std::string>());
				}
			}
			for (const auto& s : root.value("reviewed", nlohmann::json::array())) {
				if (s.is_string()) {
					reviewed.insert(s.get<std::string>());
				}
			}
		} catch (const std::exception& e) {
			spdlog::warn("Triage file unreadable ({}); starting over", e.what());
		}
	}

	void Progress::Save() const
	{
		nlohmann::json root{ { "skipped", skipped }, { "reviewed", reviewed } };
		std::ofstream  out{ std::filesystem::path{ kPath }, std::ios::trunc };
		if (out) {
			out << root.dump(2) << '\n';
		}
	}
}
