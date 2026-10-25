#include "Shrines.h"

#include "Gods.h"
#include "Settings.h"
#include "State.h"

namespace IOD
{
	namespace
	{
		constexpr auto kRegistryPath = "Data/SKSE/Plugins/ImmersiveOutfitDistribution/Shrines/Shrines.json"sv;
		const Offering kNoOffering{};

		float Today()
		{
			auto* calendar = RE::Calendar::GetSingleton();
			return calendar ? calendar->GetDaysPassed() : 0.0f;
		}

		std::int32_t CountOf(const RE::TESObjectREFR::InventoryCountMap& a_counts, const OfferingItem& a_item)
		{
			std::int32_t total = 0;
			for (auto* object : a_item.any) {
				if (const auto it = a_counts.find(object); it != a_counts.end()) {
					total += std::max(0, it->second);
				}
			}
			return total;
		}

		bool Carries(RE::PlayerCharacter* a_player, const Offering& a_offering)
		{
			if (a_offering.empty()) {
				return false;
			}
			const auto counts = a_player->GetInventoryCounts();
			return std::ranges::all_of(a_offering, [&](const OfferingItem& a_item) { return CountOf(counts, a_item) >= a_item.count; });
		}

		void Take(RE::PlayerCharacter* a_player, const Offering& a_offering)
		{
			auto counts = a_player->GetInventoryCounts();
			for (const auto& item : a_offering) {
				auto left = item.count;
				for (auto* object : item.any) {
					const auto have = counts.contains(object) ? counts[object] : 0;
					const auto take = std::min(have, left);
					if (take > 0) {
						a_player->RemoveItem(object, take, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
						left -= take;
					}
					if (left <= 0) {
						break;
					}
				}
			}
		}

		void Notify(const std::string& a_text)
		{
			RE::SendHUDMessage::ShowHUDMessage(a_text.c_str());
		}

		double RarityWeight(Rarity a_rarity)
		{
			return a_rarity == Rarity::kLegendary ? 1.0 : a_rarity == Rarity::kRare ? 3.0 : 6.0;
		}

		// "Offer" / "Leave" message box; the answer is handled on the game thread.
		class OfferCallback : public RE::IMessageBoxCallback
		{
		public:
			OfferCallback(int a_god, std::string a_set) :
				_god(a_god), _set(std::move(a_set)) {}

			void Run(std::uint8_t a_button) override
			{
				if (a_button != 0) {
					return;  // Leave
				}
				SKSE::GetTaskInterface()->AddTask([god = _god, set = _set]() {
					auto*       player = RE::PlayerCharacter::GetSingleton();
					const auto* chosen = Catalog::GetSingleton().Find(set);
					if (!player || !chosen) {
						return;
					}
					const auto& offering = Shrines::GetSingleton()->OfferingFor(*chosen, god);
					if (!Carries(player, offering)) {
						Notify(fmt::format("{} waits for the offering you promised.", GodName(god)));
						return;
					}
					Take(player, offering);
					for (auto* piece : chosen->pieces) {
						player->AddObjectToContainer(piece, nullptr, 1, nullptr);
					}
					auto& state = State::Get();
					state.owned.insert(chosen->id);
					state.lastOffering[god] = Today();
					state.blessing = god;
					spdlog::info("{} accepted {} and granted \"{}\"", GodName(god), DescribeOffering(offering), chosen->name);
					Notify(Shrines::GetSingleton()->AcceptLine(god));
					Notify(fmt::format("Received: {}", chosen->name));
				});
			}

		private:
			int         _god;
			std::string _set;
		};

		void AskToOffer(int a_god, const OutfitSet& a_set, const Offering& a_offering)
		{
			auto* factory = RE::MessageDataFactoryManager::GetSingleton();
			auto* strings = RE::InterfaceStrings::GetSingleton();
			auto* creator = factory && strings ? factory->GetCreator<RE::MessageBoxData>(strings->messageBoxData) : nullptr;
			auto* box = creator ? creator->Create() : nullptr;
			if (!box) {
				spdlog::error("Could not create the offering message box");
				return;
			}
			box->bodyText = fmt::format("{} is listening.\n\nOffer {}?", GodName(a_god), DescribeOffering(a_offering)).c_str();
			box->buttonText.push_back("Offer");
			box->buttonText.push_back("Leave");
			box->cancelButtonIndex = 1;
			box->isCancellable = true;
			box->callback = RE::BSTSmartPointer<RE::IMessageBoxCallback>(new OfferCallback(a_god, a_set.id));
			RE::MessageBoxMenu::QueueMessage(box);
		}
	}

	Shrines* Shrines::GetSingleton()
	{
		static Shrines singleton;
		return std::addressof(singleton);
	}

	std::string Shrines::AcceptLine(int a_god) const
	{
		const auto& line = a_god >= 0 && a_god < static_cast<int>(_gods.size()) ? _gods[static_cast<std::size_t>(a_god)].accept : std::string{};
		return line.empty() ? fmt::format("{} accepts your offering.", GodName(a_god)) : line;
	}

	void Shrines::Load()
	{
		_byBase.clear();
		_statues.clear();
		_gods = {};
		nlohmann::json root;
		try {
			std::ifstream in{ std::filesystem::path{ kRegistryPath } };
			if (!in) {
				spdlog::warn("No shrine registry at {}: shrines are inactive", kRegistryPath);
				return;
			}
			root = nlohmann::json::parse(in, nullptr, true, true);
		} catch (const std::exception& e) {
			spdlog::error("Shrine registry is not valid JSON ({}): shrines are inactive", e.what());
			return;
		}
		_cooldownDays = root.value("cooldownDays", 7.0f);
		_reach = root.value("statueReach", 700.0f);

		if (root.contains("gods") && root["gods"].is_object()) {
			for (const auto& [name, entry] : root["gods"].items()) {
				const auto god = ParseGod(name);
				if (!god) {
					spdlog::warn("Shrine registry: unknown god \"{}\"", name);
					continue;
				}
				auto& g = _gods[static_cast<std::size_t>(*god)];
				g.accept = entry.value("accept", "");
				if (entry.contains("offerings")) {
					constexpr std::array kTiers{ "Common", "Rare", "Legendary" };
					for (std::size_t t = 0; t < kTiers.size(); ++t) {
						if (entry["offerings"].contains(kTiers[t])) {
							if (auto offering = ParseOffering(entry["offerings"][kTiers[t]], name)) {
								g.offerings[t] = std::move(*offering);
							}
						}
					}
				}
			}
		}
		std::size_t forms = 0;
		if (root.contains("shrines") && root["shrines"].is_array()) {
			for (const auto& entry : root["shrines"]) {
				const auto god = ParseGod(entry.value("god", ""));
				if (!god || !entry.contains("forms")) {
					continue;
				}
				for (const auto& ref : entry["forms"]) {
					auto* form = ref.is_string() ? LookupFormString(ref.get<std::string>()) : nullptr;
					if (!form) {
						continue;  // e.g. Daedric Shrines - All in One isn't installed
					}
					_byBase[form->GetFormID()] = *god;
					if (!form->Is(RE::FormType::Activator)) {
						_statues.insert(form->GetFormID());
					}
					++forms;
				}
			}
		}
		spdlog::info("Shrines: {} shrine/statue record(s), {} of them statues", forms, _statues.size());
	}

	void Shrines::Install()
	{
		if (auto* events = RE::ScriptEventSourceHolder::GetSingleton()) {
			events->AddEventSink<RE::TESActivateEvent>(this);
		}
		if (auto* input = RE::BSInputDeviceManager::GetSingleton()) {
			input->AddEventSink<RE::InputEvent*>(this);
		}
	}

	bool Shrines::HasShrine(int a_god) const
	{
		return std::ranges::any_of(_byBase, [&](const auto& a_entry) { return a_entry.second == a_god; });
	}

	const Offering& Shrines::DefaultOffering(int a_god, Rarity a_rarity) const
	{
		if (a_god < 0 || a_god >= static_cast<int>(_gods.size())) {
			return kNoOffering;
		}
		return _gods[static_cast<std::size_t>(a_god)].offerings[static_cast<std::size_t>(a_rarity)];
	}

	const Offering& Shrines::OfferingFor(const OutfitSet& a_set, int a_god) const
	{
		return a_set.offering.empty() ? DefaultOffering(a_god, a_set.rarity) : a_set.offering;
	}

	float Shrines::CooldownDaysLeft(int a_god) const
	{
		const auto& last = State::Get().lastOffering;
		const auto  it = last.find(a_god);
		if (it == last.end()) {
			return 0.0f;
		}
		return std::max(0.0f, it->second + _cooldownDays - Today());
	}

	RE::BSEventNotifyControl Shrines::ProcessEvent(const RE::TESActivateEvent* a_event, RE::BSTEventSource<RE::TESActivateEvent>*)
	{
		if (!a_event || !a_event->objectActivated || !a_event->actionRef || !a_event->actionRef->IsPlayerRef()) {
			return RE::BSEventNotifyControl::kContinue;
		}
		const auto* base = a_event->objectActivated->GetBaseObject();
		const auto  it = base ? _byBase.find(base->GetFormID()) : _byBase.end();
		if (it != _byBase.end()) {
			const int god = it->second;
			SKSE::GetTaskInterface()->AddTask([god]() { Shrines::GetSingleton()->Pray(god, true); });
		}
		return RE::BSEventNotifyControl::kContinue;
	}

	RE::BSEventNotifyControl Shrines::ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*)
	{
		if (!a_event || _statues.empty()) {
			return RE::BSEventNotifyControl::kContinue;
		}
		static const auto& activate = RE::UserEvents::GetSingleton()->activate;
		for (auto* event = *a_event; event; event = event->next) {
			auto* button = event->AsButtonEvent();
			if (!button || !button->IsDown() || button->QUserEvent() != activate) {
				continue;
			}
			auto* ui = RE::UI::GetSingleton();
			if (!ui || ui->GameIsPaused() || ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME)) {
				continue;
			}
			// Something under the crosshair (door, NPC, an activatable shrine...) takes the press.
			auto* pick = RE::CrosshairPickData::GetSingleton();
			if (pick && pick->GetActiveTarget()) {
				continue;
			}
			const auto now = std::chrono::steady_clock::now();
			if (now - _lastPress < std::chrono::milliseconds(800)) {
				continue;
			}
			_lastPress = now;
			SKSE::GetTaskInterface()->AddTask([]() {
				if (const auto god = Shrines::GetSingleton()->StatueInFront()) {
					Shrines::GetSingleton()->Pray(*god, false);
				}
			});
		}
		return RE::BSEventNotifyControl::kContinue;
	}

	// The nearest known statue within reach that the player is roughly facing.
	std::optional<int> Shrines::StatueInFront() const
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* tes = RE::TES::GetSingleton();
		if (!player || !tes) {
			return std::nullopt;
		}
		const auto  origin = player->GetPosition();
		const float heading = player->GetAngleZ();
		const float fx = std::sin(heading);
		const float fy = std::cos(heading);
		std::optional<int> best;
		float              bestDistance = _reach;
		tes->ForEachReferenceInRange(player, _reach, [&](RE::TESObjectREFR* a_ref) {
			const auto* base = a_ref ? a_ref->GetBaseObject() : nullptr;
			if (!base || !_statues.contains(base->GetFormID()) || a_ref->IsDisabled()) {
				return RE::BSContainer::ForEachResult::kContinue;
			}
			const auto  pos = a_ref->GetPosition();
			const float dx = pos.x - origin.x;
			const float dy = pos.y - origin.y;
			const float distance = std::sqrt(dx * dx + dy * dy);
			// Within about 45 degrees of where the player looks (always fine when very close).
			const bool facing = distance < 120.0f || (dx * fx + dy * fy) / std::max(distance, 1.0f) > 0.7f;
			if (facing && distance < bestDistance) {
				bestDistance = distance;
				best = _byBase.at(base->GetFormID());
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
		return best;
	}

	void Shrines::Pray(int a_god, bool a_vanillaBlessing)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		auto&      state = State::Get();
		const bool changed = state.blessing != a_god;
		state.blessing = a_god;
		if (!a_vanillaBlessing) {
			Notify(fmt::format("You pray to {}.", GodName(a_god)));
		}
		if (changed) {
			spdlog::info("Blessing: {}", GodName(a_god));
			if (Settings::Get().arrivalMessage) {
				Notify(fmt::format("You are now blessed by {}.", GodName(a_god)));
			}
		}

		// Sets this god grants that the player hasn't received yet, and whose offering they carry.
		std::vector<const OutfitSet*> carried;
		for (const auto& set : Catalog::GetSingleton().Sets()) {
			if (set.HasGod(a_god) && !state.owned.contains(set.id) && Carries(player, OfferingFor(set, a_god))) {
				carried.push_back(&set);
			}
		}
		if (carried.empty()) {
			return;  // no offering: nothing more happens
		}
		if (const auto left = CooldownDaysLeft(a_god); left > 0.0f) {
			Notify(fmt::format("{} has received an offering from you recently. Return in {} day(s).", GodName(a_god),
				static_cast<int>(std::ceil(left))));
			return;
		}
		std::vector<const OutfitSet*> worthy;
		for (const auto* set : carried) {
			if (IsMet(set->requirement, player)) {
				worthy.push_back(set);
			}
		}
		if (worthy.empty()) {
			Notify(fmt::format("{}: You are not yet worthy. (Needs {}.)", GodName(a_god), carried.front()->requirement.label));
			return;
		}
		std::vector<double> weights;
		for (const auto* set : worthy) {
			weights.push_back(RarityWeight(set->rarity));
		}
		static std::mt19937                     rng{ std::random_device{}() };
		std::discrete_distribution<std::size_t> pick(weights.begin(), weights.end());
		const auto*                             chosen = worthy[pick(rng)];
		spdlog::info("{} offers \"{}\" for {}", GodName(a_god), chosen->name, DescribeOffering(OfferingFor(*chosen, a_god)));
		AskToOffer(a_god, *chosen, OfferingFor(*chosen, a_god));
	}
}
