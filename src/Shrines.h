#pragma once

#include "Catalog.h"

namespace IOD
{
	// Shrines of the Divines and Daedric princes (milestone 4).
	//  - Divine shrines (and the few Daedric shrines that are activators) are noticed through the
	//    game's activation; the vanilla blessing still happens.
	//  - Daedric statues are decoration (not activatable): pressing Activate while facing one within
	//    reach, with nothing else under the crosshair, counts as praying to it.
	// Praying makes that god your blessing. Carrying the offering for one of the god's sets opens
	// "Offer / Leave"; accepting takes the items and gives a full set. One offering per god per week.
	class Shrines :
		public RE::BSTEventSink<RE::TESActivateEvent>,
		public RE::BSTEventSink<RE::InputEvent*>
	{
	public:
		static Shrines* GetSingleton();

		void Load();     // the registry, Data/SKSE/Plugins/ImmersiveOutfitDistribution/Shrines/Shrines.json
		void Install();  // event sinks

		[[nodiscard]] const Offering& DefaultOffering(int a_god, Rarity a_rarity) const;
		[[nodiscard]] const Offering& OfferingFor(const OutfitSet& a_set, int a_god) const;
		[[nodiscard]] bool            HasShrine(int a_god) const;
		[[nodiscard]] float           CooldownDaysLeft(int a_god) const;  // 0: ready
		[[nodiscard]] float           CooldownDays() const { return _cooldownDays; }

		RE::BSEventNotifyControl ProcessEvent(const RE::TESActivateEvent* a_event, RE::BSTEventSource<RE::TESActivateEvent>*) override;
		RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*) override;

		void        Pray(int a_god, bool a_vanillaBlessing);  // game thread
		std::string AcceptLine(int a_god) const;

	private:
		struct God
		{
			std::array<Offering, 3> offerings;  // by rarity
			std::string             accept;
		};

		std::optional<int> StatueInFront() const;

		std::array<God, 25>                      _gods{};
		std::unordered_map<RE::FormID, int>      _byBase;   // shrine/statue base record -> god
		std::unordered_set<RE::FormID>           _statues;  // bases that can't be activated
		float                                    _cooldownDays{ 7.0f };
		float                                    _reach{ 700.0f };
		std::chrono::steady_clock::time_point    _lastPress{};
	};
}
