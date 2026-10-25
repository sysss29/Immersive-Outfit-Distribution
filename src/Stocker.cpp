#include "Stocker.h"

#include "Catalog.h"
#include "State.h"

namespace IOD
{
	namespace
	{
		// How many sets a vendor shows per period: 0, 1, 2 or 3 (weighted towards 1 and 2).
		constexpr std::array<double, 4> kCountWeights{ 15.0, 35.0, 35.0, 15.0 };

		double RarityWeight(Rarity a_rarity)
		{
			switch (a_rarity) {
			case Rarity::kCommon:
				return 6.0;
			case Rarity::kRare:
				return 3.0;
			case Rarity::kLegendary:
				return 1.0;
			}
			return 1.0;
		}

		// Removes every catalog piece from a merchant chest (the previous period's stock).
		void ClearCatalogItems(RE::TESObjectREFR* a_chest)
		{
			std::unordered_set<RE::TESBoundObject*> pieces;
			for (const auto& set : Catalog::GetSingleton().Sets()) {
				pieces.insert(set.pieces.begin(), set.pieces.end());
			}
			auto counts = a_chest->GetInventoryCounts([&](RE::TESBoundObject& a_object) { return pieces.contains(&a_object); });
			for (const auto& [object, count] : counts) {
				if (count > 0) {
					a_chest->RemoveItem(object, count, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
				}
			}
		}
	}

	Stocker* Stocker::GetSingleton()
	{
		static Stocker singleton;
		return std::addressof(singleton);
	}

	void Stocker::Install()
	{
		if (auto* ui = RE::UI::GetSingleton()) {
			ui->AddEventSink<RE::MenuOpenCloseEvent>(GetSingleton());
		}
		spdlog::info("Vendor stocker installed");
	}

	std::int32_t Stocker::CurrentPeriod()
	{
		auto* calendar = RE::Calendar::GetSingleton();
		const float days = calendar ? calendar->GetDaysPassed() : 0.0f;
		return static_cast<std::int32_t>(std::floor(days / 2.0f));
	}

	RE::BSEventNotifyControl Stocker::ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
	{
		// Stock when the trade window opens. The game restocks a vendor's chest at exactly this
		// moment (wiping anything added earlier), so adding before it is pointless.
		if (a_event && a_event->opening && a_event->menuName == RE::BarterMenu::MENU_NAME) {
			auto ref = RE::TESObjectREFR::LookupByHandle(RE::BarterMenu::GetTargetRefHandle());
			if (auto* actor = ref ? ref->As<RE::Actor>() : nullptr) {
				QueueVendor(actor->GetHandle());
			}
		}
		return RE::BSEventNotifyControl::kContinue;
	}

	void Stocker::QueueVendor(RE::ActorHandle a_actor)
	{
		// Next frame: the menu has finished building its list, so the refresh below sees our items.
		SKSE::GetTaskInterface()->AddTask([a_actor]() {
			if (auto actor = a_actor.get()) {
				if (StockVendor(actor.get())) {
					RefreshBarterMenu();
				}
			}
		});
	}

	// Makes the open trade window rebuild its item list, the way a completed transaction does
	// (SkyUI's BarterMenu.doTransaction with an amount of 0 changes nothing else).
	void Stocker::RefreshBarterMenu()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui || !ui->IsMenuOpen(RE::BarterMenu::MENU_NAME)) {
			return;
		}
		auto menu = ui->GetMenu<RE::BarterMenu>();
		if (menu && menu->uiMovie) {
			RE::GFxValue amount(0.0);
			if (!menu->uiMovie->Invoke("_root.Menu_mc.doTransaction", nullptr, &amount, 1)) {
				spdlog::warn("Could not refresh the trade window; reopen it to see new stock");
			}
		}
	}

	bool Stocker::StockVendor(RE::Actor* a_actor)
	{
		auto* faction = a_actor ? a_actor->GetVendorFaction() : nullptr;
		const auto source = SourceForFaction(faction);
		if (!source) {
			return false;
		}
		auto* chest = faction->vendorData.merchantContainer;
		if (!chest) {
			spdlog::warn("{}: vendor faction {:08X} has no merchant chest", a_actor->GetDisplayFullName(), faction->GetFormID());
			return false;
		}

		const auto period = CurrentPeriod();
		auto& state = State::Get();
		const auto  key = faction->GetFormID();
		const auto  it = state.stocked.find(key);
		const bool  newPeriod = it == state.stocked.end() || it->second != period;
		const auto* name = a_actor->GetDisplayFullName();

		if (newPeriod) {
			EnsureAllocation(period);
			ClearCatalogItems(chest);
			state.stocked[key] = period;
		}

		// Top up: put back any allocated piece that is neither in the chest nor in the player's
		// inventory. This restocks after the game's own chest reset (which wipes added items)
		// without ever duplicating a piece the player has bought.
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto  inChest = chest->GetInventoryCounts();
		auto  inPlayer = player ? player->GetInventoryCounts() : RE::TESObjectREFR::InventoryCountMap{};
		auto  has = [](const RE::TESObjectREFR::InventoryCountMap& a_counts, RE::TESBoundObject* a_object) {
			const auto found = a_counts.find(a_object);
			return found != a_counts.end() && found->second > 0;
		};

		std::size_t setsShown = 0;
		std::size_t piecesAdded = 0;
		for (const auto& id : state.allocation[key]) {
			const auto* set = Catalog::GetSingleton().Find(id);
			if (!set) {
				continue;
			}
			++setsShown;
			for (auto* piece : set->pieces) {
				if (!has(inChest, piece) && !has(inPlayer, piece)) {
					chest->AddObjectToContainer(piece, nullptr, 1, nullptr);
					++piecesAdded;
				}
			}
			if (newPeriod) {
				spdlog::info("Period {}: {} stocks \"{}\"", period, name, set->name);
			}
		}

		if (newPeriod) {
			if (setsShown == 0) {
				spdlog::info("Period {}: {} has no IOD stock this time", period, name);
			} else {
				const auto message = fmt::format("New arrivals at {}.", SourceName(*source));
				RE::SendHUDMessage::ShowHUDMessage(message.c_str());
			}
		}
		if (piecesAdded > 0) {
			spdlog::info("Period {}: {} received {} piece(s)", period, name, piecesAdded);
		}
		return piecesAdded > 0;
	}

	// Decides, once per period, which sets every vendor shows: 0-3 per vendor, weighted by
	// rarity, each set at one vendor at most. Stored in the save so reloading keeps the draw.
	void Stocker::EnsureAllocation(std::int32_t a_period)
	{
		auto& state = State::Get();
		if (state.allocPeriod == a_period) {
			return;
		}
		state.allocation.clear();
		state.allocPeriod = a_period;

		auto* player = RE::PlayerCharacter::GetSingleton();
		const auto& sets = Catalog::GetSingleton().Sets();
		for (const auto& set : sets) {
			if (set.unique && !state.owned.contains(set.id) && set.PlayerHasAllPieces(player)) {
				state.owned.insert(set.id);
				spdlog::info("\"{}\" is now owned and leaves rotation", set.name);
			}
		}

		std::mt19937 rng(state.salt ^ (static_cast<std::uint32_t>(a_period) * 0x9E3779B1u));
		std::unordered_set<std::string> used;
		for (const auto source : kAllSources) {
			for (auto* faction : VendorFactions(source)) {
				std::discrete_distribution<int> howMany(kCountWeights.begin(), kCountWeights.end());
				int wanted = howMany(rng);
				auto& picked = state.allocation[faction->GetFormID()];
				while (wanted-- > 0) {
					std::vector<const OutfitSet*> pool;
					std::vector<double>           weights;
					for (const auto& set : sets) {
						if (!set.HasSource(source) || used.contains(set.id)) {
							continue;
						}
						if (set.unique && state.owned.contains(set.id)) {
							continue;
						}
						if (!IsMet(set.requirement, player)) {
							continue;
						}
						pool.push_back(&set);
						weights.push_back(RarityWeight(set.rarity));
					}
					if (pool.empty()) {
						break;
					}
					std::discrete_distribution<std::size_t> pick(weights.begin(), weights.end());
					const auto* chosen = pool[pick(rng)];
					used.insert(chosen->id);
					picked.push_back(chosen->id);
				}
			}
		}
		spdlog::info("Period {}: allocated {} set(s) across vendors", a_period, used.size());
	}
}
