#pragma once

namespace IOD
{
	// Adds the current period's sets to a vendor's merchant chest when the player talks to them
	// (activation) or opens their trade window.
	class Stocker :
		public RE::BSTEventSink<RE::TESActivateEvent>,
		public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		static Stocker* GetSingleton();
		static void     Install();

		RE::BSEventNotifyControl ProcessEvent(const RE::TESActivateEvent* a_event, RE::BSTEventSource<RE::TESActivateEvent>*) override;
		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;

		// 48 in-game hours per period, matching the vanilla merchant chest reset.
		static std::int32_t CurrentPeriod();

	private:
		static void QueueVendor(RE::ActorHandle a_actor);
		static void StockVendor(RE::Actor* a_actor);
		static void EnsureAllocation(std::int32_t a_period);
	};
}
