#pragma once

namespace IOD
{
	struct OutfitSet;

	// Makes each vendor's current sets part of their stock:
	//  - the pieces are written into the merchant chest's base container, so the game's own
	//    restock (when a trade window opens after the reset interval) adds them like vanilla items;
	//  - when you start talking to a vendor, missing pieces are also put into the chest directly,
	//    for the case where no restock is due.
	class Stocker :
		public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		static Stocker* GetSingleton();
		static void     Install();
		static void     CheckTradeable();  // after the catalog is loaded: warns about impossible sources
		static void     OnGameLoaded();  // after a save is loaded or a new game starts

		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;

		// 48 in-game hours per period, matching the vanilla merchant chest reset.
		static std::int32_t CurrentPeriod();

		// True if the vendor's buy/sell list accepts every piece (e.g. Radiant Raiment: clothing
		// and jewellery only). Read-only, safe from the settings page.
		static bool CanTradePieces(const std::vector<RE::TESObjectARMO*>& a_pieces, RE::TESFaction* a_faction);

		// For the settings page.
		struct VendorLine
		{
			std::string              vendor;
			std::string              source;
			std::vector<std::string> sets;
		};
		static std::pair<std::int32_t, std::vector<VendorLine>> Snapshot();  // any thread
		static void                                          Redraw();         // game thread
		static void                                          ReloadCatalog();  // game thread

	private:
		static bool CanTrade(const OutfitSet& a_set, RE::TESFaction* a_faction);
		static void SyncBaseContainers();
		static void PrepareVendor(RE::TESFaction* a_faction, std::string_view a_name);
		static void RefreshBarterMenu();
		static void EnsureAllocation(std::int32_t a_period);
		static void UpdateSnapshot();
	};
}
