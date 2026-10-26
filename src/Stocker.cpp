#include "Stocker.h"

#include "Catalog.h"
#include "Settings.h"
#include "Shrines.h"
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

		std::mutex                       g_snapshotLock;
		std::vector<Stocker::VendorLine> g_snapshot;
		std::int32_t                     g_snapshotPeriod{ -1 };
		std::uint32_t                    g_redraws{ 0 };  // varies the draw after "Redraw now"

		// Pieces IOD has put into each merchant chest's base container (not saved: rebuilt on load).
		std::unordered_map<RE::TESObjectCONT*, std::vector<RE::TESBoundObject*>> g_baseEntries;

		RE::TESObjectREFR* ChestOf(RE::TESFaction* a_faction)
		{
			return a_faction ? a_faction->vendorData.merchantContainer : nullptr;
		}

		RE::TESObjectCONT* BaseOf(RE::TESObjectREFR* a_chest)
		{
			auto* base = a_chest ? a_chest->GetBaseObject() : nullptr;
			return base ? base->As<RE::TESObjectCONT>() : nullptr;
		}

		std::vector<RE::TESBoundObject*> PiecesFor(RE::TESFaction* a_faction)
		{
			std::vector<RE::TESBoundObject*> pieces;
			const auto&                      allocation = State::Get().allocation;
			const auto                       it = allocation.find(a_faction->GetFormID());
			if (it == allocation.end()) {
				return pieces;
			}
			for (const auto& id : it->second) {
				if (const auto* set = Catalog::GetSingleton().Find(id)) {
					pieces.insert(pieces.end(), set->pieces.begin(), set->pieces.end());
				}
			}
			return pieces;
		}

		bool Has(const RE::TESObjectREFR::InventoryCountMap& a_counts, RE::TESBoundObject* a_object)
		{
			const auto found = a_counts.find(a_object);
			return found != a_counts.end() && found->second > 0;
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

		// Puts any allocated piece that is neither in the chest nor owned by the player into the
		// chest. Returns how many were added.
		std::size_t TopUp(RE::TESFaction* a_faction)
		{
			auto* chest = ChestOf(a_faction);
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!chest || !player) {
				return 0;
			}
			const auto  inChest = chest->GetInventoryCounts();
			const auto  inPlayer = player->GetInventoryCounts();
			std::size_t added = 0;
			for (auto* piece : PiecesFor(a_faction)) {
				if (!Has(inChest, piece) && !Has(inPlayer, piece)) {
					chest->AddObjectToContainer(piece, nullptr, 1, nullptr);
					++added;
				}
			}
			return added;
		}

		std::size_t CountInChest(RE::TESFaction* a_faction)
		{
			auto* chest = ChestOf(a_faction);
			if (!chest) {
				return 0;
			}
			const auto  inChest = chest->GetInventoryCounts();
			std::size_t count = 0;
			for (auto* piece : PiecesFor(a_faction)) {
				count += Has(inChest, piece) ? 1 : 0;
			}
			return count;
		}

		RE::TESFaction* VendorFactionOf(RE::TESObjectREFR* a_ref)
		{
			auto* actor = a_ref ? a_ref->As<RE::Actor>() : nullptr;
			auto* faction = actor ? actor->GetVendorFaction() : nullptr;
			return SourceForFaction(faction) ? faction : nullptr;
		}
	}

	// A vendor only lists chest items its buy/sell list allows (e.g. Radiant Raiment: items tagged
	// VendorItemClothing or VendorItemJewelry). IOD changes nothing about this: a set is offered
	// at a vendor only if every piece is one that vendor trades.
	bool Stocker::CanTrade(const OutfitSet& a_set, RE::TESFaction* a_faction)
	{
		return CanTradePieces(a_set.pieces, a_faction);
	}

	bool Stocker::CanTradePieces(const std::vector<RE::TESObjectARMO*>& a_pieces, RE::TESFaction* a_faction)
	{
		auto* list = a_faction ? a_faction->vendorData.vendorSellBuyList : nullptr;
		if (!list) {
			return true;  // no list: the vendor trades anything
		}
		const bool excluding = a_faction->vendorData.vendorValues.notBuySell;
		auto*      clothingTag = VendorTagFor(list, true);
		for (auto* piece : a_pieces) {
			auto* form = piece ? piece->As<RE::BGSKeywordForm>() : nullptr;
			bool  listed = form && std::ranges::any_of(list->forms, [&](RE::TESForm* a_form) {
                auto* keyword = a_form ? a_form->As<RE::BGSKeyword>() : nullptr;
                return keyword && form->HasKeyword(keyword);
            });
			// Clothing that its author tagged as armour for vendors (common): it gets the clothing
			// tag at load (TagClothing), so a clothier can sell it.
			if (!listed && !excluding && clothingTag && IsClothingLike(piece)) {
				listed = true;
			}
			if (listed == excluding) {
				return false;
			}
		}
		return true;
	}

	// Clothing by the game's own classification: armour type "Clothing", or the ArmorClothing /
	// ArmorJewelry keyword. Light and heavy armour never counts.
	bool Stocker::IsClothingLike(const RE::TESObjectARMO* a_piece)
	{
		if (!a_piece || a_piece->IsLightArmor() || a_piece->IsHeavyArmor()) {
			return false;
		}
		static auto* armorClothing = RE::TESForm::LookupByID<RE::BGSKeyword>(0x06BBE8);
		static auto* armorJewelry = RE::TESForm::LookupByID<RE::BGSKeyword>(0x06BBE9);
		return a_piece->IsClothing() || (armorClothing && a_piece->HasKeyword(armorClothing)) || (armorJewelry && a_piece->HasKeyword(armorJewelry));
	}

	// The clothing (or jewellery) vendor keyword, if this buy/sell list accepts it.
	RE::BGSKeyword* Stocker::VendorTagFor(const RE::BGSListForm* a_list, bool a_clothing)
	{
		static auto* vendorClothing = RE::TESForm::LookupByID<RE::BGSKeyword>(0x08F95B);
		static auto* vendorJewelry = RE::TESForm::LookupByID<RE::BGSKeyword>(0x08F95A);
		auto*        tag = a_clothing ? vendorClothing : vendorJewelry;
		return a_list && tag && a_list->HasForm(tag) ? tag : nullptr;
	}

	// For sets sold by a clothier: clothing pieces tagged VendorItemArmor by their author get the
	// vendor's clothing (or jewellery) tag, so the trade window lists them. In memory only.
	void Stocker::TagClothing()
	{
		static auto* armorJewelry = RE::TESForm::LookupByID<RE::BGSKeyword>(0x06BBE9);
		for (const auto& set : Catalog::GetSingleton().Sets()) {
			for (const auto source : kAllSources) {
				if (!set.HasSource(source)) {
					continue;
				}
				for (auto* faction : VendorFactions(source)) {
					auto* list = faction->vendorData.vendorSellBuyList;
					if (!list || faction->vendorData.vendorValues.notBuySell || !CanTradePieces(set.pieces, faction)) {
						continue;
					}
					for (auto* piece : set.pieces) {
						const bool listed = std::ranges::any_of(list->forms, [&](RE::TESForm* a_form) {
							auto* keyword = a_form ? a_form->As<RE::BGSKeyword>() : nullptr;
							return keyword && piece->HasKeyword(keyword);
						});
						if (listed) {
							continue;
						}
						const bool jewel = armorJewelry && piece->HasKeyword(armorJewelry);
						auto*      tag = VendorTagFor(list, !jewel);
						if (!tag) {
							tag = VendorTagFor(list, jewel);
						}
						if (tag && piece->AddKeyword(tag)) {
							spdlog::info("\"{}\": {} is clothing; tagged so {} can sell it", set.name, piece->GetName(), VendorLabel(faction));
						}
					}
				}
			}
		}
	}

	void Stocker::CheckTradeable()
	{
		TagClothing();
		for (const auto& set : Catalog::GetSingleton().Sets()) {
			for (const auto source : kAllSources) {
				if (!set.HasSource(source)) {
					continue;
				}
				const auto& factions = VendorFactions(source);
				const auto  ok = std::ranges::count_if(factions, [&](RE::TESFaction* a_f) { return CanTrade(set, a_f); });
				if (ok == 0) {
					spdlog::warn("\"{}\" will never appear at {}: it doesn't trade this kind of item (e.g. armour at a clothier)",
						set.name, SourceName(source));
				} else if (ok < static_cast<std::ptrdiff_t>(factions.size())) {
					spdlog::warn("\"{}\": only {} of {} {} vendor(s) trade this kind of item", set.name, ok, factions.size(), SourceName(source));
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

	void Stocker::OnGameLoaded()
	{
		// Saves from before 0.6.0 name sets "<file>/<name>": switch them to the permanent ids.
		{
			auto& state = State::Get();
			for (const auto& set : Catalog::GetSingleton().Sets()) {
				if (set.id == set.legacyId) {
					continue;
				}
				auto swap = [&](std::set<std::string>& a_ids) {
					if (a_ids.erase(set.legacyId) > 0) {
						a_ids.insert(set.id);
					}
				};
				swap(state.owned);
				swap(state.announced);
				for (auto& [faction, ids] : state.allocation) {
					std::ranges::replace(ids, set.legacyId, set.id);
				}
			}
		}
		// Redraw if the save's allocation puts a set where it can't be traded (e.g. a save from an
		// older version, or a set file changed since).
		auto& state = State::Get();
		for (const auto& [factionID, ids] : state.allocation) {
			auto* faction = RE::TESForm::LookupByID<RE::TESFaction>(factionID);
			const bool bad = faction && std::ranges::any_of(ids, [&](const std::string& a_id) {
				const auto* set = Catalog::GetSingleton().Find(a_id);
				return set && !CanTrade(*set, faction);
			});
			if (bad) {
				spdlog::info("Saved allocation has a set its vendor can't trade; redrawing");
				state.allocPeriod = -1;
				state.stocked.clear();
				break;
			}
		}
		// Base forms are not reloaded with a save, so g_baseEntries stays valid: the sync removes
		// what the previous save's period put in and adds the loaded save's allocation.
		SyncBaseContainers();
		spdlog::info("Base containers synced for period {}", CurrentPeriod());
		UpdateSnapshot();
	}

	// Writes the current period's pieces into each vendor chest's base container. The game
	// rebuilds a merchant chest from its base container whenever it restocks (when a trade window
	// opens after the vendor's reset interval), so the sets appear exactly like vanilla stock.
	// Each vendor chest has its own base container, so nothing leaks to other containers.
	void Stocker::SyncBaseContainers()
	{
		EnsureAllocation(CurrentPeriod());
		for (const auto source : kAllSources) {
			for (auto* faction : VendorFactions(source)) {
				auto* base = BaseOf(ChestOf(faction));
				if (!base) {
					continue;
				}
				auto& entries = g_baseEntries[base];
				for (auto* piece : entries) {
					if (const auto count = base->GetObjectCount(piece); count > 0) {
						base->RemoveObjectFromContainer(piece, count);
					}
				}
				entries.clear();
				for (auto* piece : PiecesFor(faction)) {
					if (base->GetObjectCount(piece) == 0 && base->AddObjectToContainer(piece, 1, nullptr)) {
						entries.push_back(piece);
					}
				}
			}
		}
	}

	RE::BSEventNotifyControl Stocker::ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
	{
		if (!a_event || !a_event->opening) {
			return RE::BSEventNotifyControl::kContinue;
		}
		if (a_event->menuName == RE::DialogueMenu::MENU_NAME) {
			// Talking to someone: this happens before any trade window, so the chest's base
			// container and contents are ready before the game restocks or lists them.
			auto* topics = RE::MenuTopicManager::GetSingleton();
			RE::NiPointer<RE::TESObjectREFR> speaker;
			if (topics) {
				speaker = topics->speaker.get();
			}
			if (auto* faction = VendorFactionOf(speaker.get())) {
				PrepareVendor(faction, speaker->GetDisplayFullName());
			}
		} else if (a_event->menuName == RE::BarterMenu::MENU_NAME) {
			auto ref = RE::TESObjectREFR::LookupByHandle(RE::BarterMenu::GetTargetRefHandle());
			if (auto* faction = VendorFactionOf(ref.get())) {
				const std::string name = ref->GetDisplayFullName();
				// Next frame: the game has restocked and built the list by then.
				SKSE::GetTaskInterface()->AddTask([faction, name]() {
					const auto added = TopUp(faction);
					const auto total = PiecesFor(faction).size();
					spdlog::info("Trade with {}: chest holds {} of {} allocated piece(s){}", name, CountInChest(faction), total,
						added ? fmt::format(" ({} added now)", added) : std::string{});
					if (added > 0) {
						RefreshBarterMenu();
					}
				});
			}
		}
		return RE::BSEventNotifyControl::kContinue;
	}

	void Stocker::PrepareVendor(RE::TESFaction* a_faction, std::string_view a_name)
	{
		const auto period = CurrentPeriod();
		auto&      state = State::Get();
		const auto key = a_faction->GetFormID();
		const auto it = state.stocked.find(key);
		const bool newPeriod = it == state.stocked.end() || it->second != period;

		SyncBaseContainers();
		if (newPeriod) {
			if (auto* chest = ChestOf(a_faction)) {
				ClearCatalogItems(chest);
			}
			state.stocked[key] = period;
		}
		const auto added = TopUp(a_faction);

		if (newPeriod) {
			const auto& ids = state.allocation[key];
			for (const auto& id : ids) {
				if (const auto* set = Catalog::GetSingleton().Find(id)) {
					spdlog::info("Period {}: {} offers \"{}\"", period, a_name, set->name);
				}
			}
			if (ids.empty()) {
				spdlog::info("Period {}: {} has no IOD stock this time", period, a_name);
			} else if (Settings::Get().arrivalMessage) {
				const auto source = SourceForFaction(a_faction);
				const auto message = fmt::format("New arrivals at {}.", source ? SourceName(*source) : "this shop"sv);
				RE::SendHUDMessage::ShowHUDMessage(message.c_str());
			}
		}
		if (added > 0) {
			spdlog::info("Period {}: {} received {} piece(s)", period, a_name, added);
		}
	}

	// Makes an open trade window rebuild its item list, the way a completed transaction does
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

		auto*       player = RE::PlayerCharacter::GetSingleton();
		const auto& sets = Catalog::GetSingleton().Sets();
		for (const auto& set : sets) {
			if (set.unique && !state.owned.contains(set.id) && set.PlayerHasAllPieces(player)) {
				state.owned.insert(set.id);
				spdlog::info("\"{}\" is now owned and leaves rotation", set.name);
			}
		}

		std::mt19937                    rng(state.salt ^ (static_cast<std::uint32_t>(a_period) * 0x9E3779B1u) ^ (g_redraws * 0x85EBCA6Bu));
		std::unordered_set<std::string> used;
		for (const auto source : kAllSources) {
			for (auto* faction : VendorFactions(source)) {
				std::discrete_distribution<int> howMany(kCountWeights.begin(), kCountWeights.end());
				int                             wanted = howMany(rng);
				if (Settings::Get().guaranteeOnePerVendor) {
					wanted = std::max(wanted, 1);
				}
				auto&                           picked = state.allocation[faction->GetFormID()];
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
						if (!IsMet(set.requirement, player) || !CanTrade(set, faction)) {
							continue;
						}
						pool.push_back(&set);
						weights.push_back(RarityWeight(set.rarity));
					}
					if (pool.empty()) {
						break;
					}
					// Balance: each armour mod gets the same share, however many sets it has.
					if (Settings::Get().balanceByMod) {
						auto modOf = [](const OutfitSet* a_set) -> std::string_view {
							const auto* file = a_set->pieces.empty() ? nullptr : a_set->pieces.front()->GetFile(0);
							return file ? file->GetFilename() : std::string_view{};
						};
						std::unordered_map<std::string_view, int> perMod;
						for (const auto* set : pool) {
							++perMod[modOf(set)];
						}
						for (std::size_t i = 0; i < pool.size(); ++i) {
							weights[i] /= static_cast<double>(perMod[modOf(pool[i])]);
						}
					}
					std::discrete_distribution<std::size_t> pick(weights.begin(), weights.end());
					const auto*                             chosen = pool[pick(rng)];
					used.insert(chosen->id);
					picked.push_back(chosen->id);
				}
			}
		}
		spdlog::info("Period {}: allocated {} set(s) across vendors", a_period, used.size());
		for (const auto source : kAllSources) {
			for (auto* faction : VendorFactions(source)) {
				std::string names;
				for (const auto& id : state.allocation[faction->GetFormID()]) {
					if (const auto* set = Catalog::GetSingleton().Find(id)) {
						names += (names.empty() ? "" : ", ") + set->name;
					}
				}
				spdlog::info("  {}: {}", VendorLabel(faction), names.empty() ? "nothing"s : names);
			}
		}
		UpdateSnapshot();
	}

	void Stocker::UpdateSnapshot()
	{
		std::vector<VendorLine> lines;
		const auto&             allocation = State::Get().allocation;
		for (const auto source : kAllSources) {
			for (auto* faction : VendorFactions(source)) {
				VendorLine line{ std::string(VendorLabel(faction)), std::string(SourceName(source)), {} };
				if (const auto it = allocation.find(faction->GetFormID()); it != allocation.end()) {
					for (const auto& id : it->second) {
						if (const auto* set = Catalog::GetSingleton().Find(id)) {
							line.sets.push_back(set->name);
						}
					}
				}
				lines.push_back(std::move(line));
			}
		}
		Shrines::GetSingleton()->UpdateSnapshot();
		std::scoped_lock lock(g_snapshotLock);
		g_snapshot = std::move(lines);
		g_snapshotPeriod = State::Get().allocPeriod;
	}

	std::pair<std::int32_t, std::vector<Stocker::VendorLine>> Stocker::Snapshot()
	{
		std::scoped_lock lock(g_snapshotLock);
		return { g_snapshotPeriod, g_snapshot };
	}

	// Game thread only. Forgets this period's draw; the next sync draws again and every vendor
	// restocks the next time you talk to them.
	void Stocker::Redraw()
	{
		auto& state = State::Get();
		state.allocPeriod = -1;
		state.stocked.clear();
		++g_redraws;
		SyncBaseContainers();
		spdlog::info("Redrawn on request");
	}

	// Game thread only. Re-reads the set files (after the settings page saved them).
	void Stocker::ReloadCatalog()
	{
		Catalog::GetSingleton().Load();
		CheckTradeable();
		auto* ui = RE::UI::GetSingleton();
		if (ui && ui->IsMenuOpen(RE::MainMenu::MENU_NAME)) {
			return;  // no game loaded: loading one syncs everything
		}
		OnGameLoaded();  // drops draws that became impossible, syncs base containers
	}
}
