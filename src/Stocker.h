#pragma once

namespace IOD
{
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
		static void     MakeTradeable();  // after the catalog is loaded
		static void     OnGameLoaded();  // after a save is loaded or a new game starts

		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;

		// 48 in-game hours per period, matching the vanilla merchant chest reset.
		static std::int32_t CurrentPeriod();

	private:
		static void SyncBaseContainers();
		static void PrepareVendor(RE::TESFaction* a_faction, std::string_view a_name);
		static void RefreshBarterMenu();
		static void EnsureAllocation(std::int32_t a_period);
	};
}
