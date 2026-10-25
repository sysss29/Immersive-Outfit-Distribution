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

		// What the player still lacks for an offering, e.g. "Skooma, 2 Wine"; empty when carried.
		std::string Missing(const RE::TESObjectREFR::InventoryCountMap& a_counts, const Offering& a_offering)
		{
			std::string text;
			for (const auto& item : a_offering) {
				const auto lack = item.count - CountOf(a_counts, item);
				if (lack <= 0) {
					continue;
				}
				auto*       first = item.any.front();
				const bool  gold = first->GetFormID() == 0xF;
				std::string name = gold ? "gold" : first->GetName();
				text += (text.empty() ? "" : ", ") + (lack > 1 || gold ? fmt::format("{} {}", lack, name) : name);
			}
			return text;
		}

		void Grant(RE::PlayerCharacter* a_player, const OutfitSet& a_set)
		{
			for (auto* piece : a_set.pieces) {
				a_player->AddObjectToContainer(piece, nullptr, 1, nullptr);
			}
			State::Get().owned.insert(a_set.id);
		}

		// Answer to the tribute list: button i is the i-th set offered, the last one is "Leave".
		class TributeCallback : public RE::IMessageBoxCallback
		{
		public:
			TributeCallback(int a_god, std::vector<std::string> a_sets) :
				_god(a_god), _sets(std::move(a_sets)) {}

			void Run(std::uint8_t a_button) override
			{
				if (a_button >= _sets.size()) {
					return;  // Leave
				}
				SKSE::GetTaskInterface()->AddTask([god = _god, id = _sets[a_button]]() {
					auto*       player = RE::PlayerCharacter::GetSingleton();
					const auto* chosen = Catalog::GetSingleton().Find(id);
					if (!player || !chosen || State::Get().owned.contains(id)) {
						return;
					}
					const auto& offering = Shrines::GetSingleton()->OfferingFor(*chosen, god);
					if (!Carries(player, offering) || !IsMet(chosen->requirement, player)) {
						Notify(fmt::format("{} is not amused.", GodName(god)));
						return;
					}
					Take(player, offering);
					Grant(player, *chosen);
					spdlog::info("{} accepted {} for \"{}\"", GodName(god), DescribeOffering(offering), chosen->name);
					Notify(Shrines::GetSingleton()->AcceptLine(god));
					Notify(fmt::format("Received: {}", chosen->name));
				});
			}

		private:
			int                      _god;
			std::vector<std::string> _sets;
		};

		bool ShowBox(const std::string& a_body, const std::vector<std::string>& a_buttons, RE::IMessageBoxCallback* a_callback)
		{
			auto* factory = RE::MessageDataFactoryManager::GetSingleton();
			auto* strings = RE::InterfaceStrings::GetSingleton();
			auto* creator = factory && strings ? factory->GetCreator<RE::MessageBoxData>(strings->messageBoxData) : nullptr;
			auto* box = creator ? creator->Create() : nullptr;
			if (!box) {
				spdlog::error("Could not create a message box");
				return false;
			}
			box->bodyText = a_body.c_str();
			for (const auto& button : a_buttons) {
				box->buttonText.push_back(button.c_str());
			}
			box->cancelButtonIndex = static_cast<std::int32_t>(a_buttons.size()) - 1;
			box->isCancellable = true;
			box->verticalButtons = a_buttons.size() > 2;
			box->callback = RE::BSTSmartPointer<RE::IMessageBoxCallback>(a_callback);
			RE::MessageBoxMenu::QueueMessage(box);
			return true;
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
			Notify(fmt::format("You are now blessed by {}.", GodName(a_god)));
		}

		std::vector<const OutfitSet*> sets;  // this god's sets the player hasn't received
		for (const auto& set : Catalog::GetSingleton().Sets()) {
			if (set.HasGod(a_god) && !state.owned.contains(set.id)) {
				sets.push_back(&set);
			}
		}
		if (sets.empty()) {
			return;
		}

		if (IsDivine(a_god)) {
			// The Divines don't trade: they reward deeds. A set whose requirement is met (typically
			// a quest completed) is given freely, once.
			for (const auto* set : sets) {
				if (IsMet(set->requirement, player)) {
					Grant(player, *set);
					spdlog::info("{} rewards the player with \"{}\"", GodName(a_god), set->name);
					Notify(AcceptLine(a_god));
					Notify(fmt::format("{} rewards you: {}", GodName(a_god), set->name));
				}
			}
			return;
		}

		// Daedra: show everything the prince would part with, and what it costs.
		const auto               counts = player->GetInventoryCounts();
		std::string              body = fmt::format("{} regards you.\n", GodName(a_god));
		std::vector<std::string> buttons;
		std::vector<std::string> ids;
		constexpr std::size_t    kMaxListed = 8;
		for (std::size_t i = 0; i < sets.size() && i < kMaxListed; ++i) {
			const auto* set = sets[i];
			const auto& offering = OfferingFor(*set, a_god);
			if (offering.empty()) {
				continue;
			}
			body += fmt::format("\n{}: {}", set->name, DescribeOffering(offering));
			if (!IsMet(set->requirement, player)) {
				body += fmt::format("\n   Not yet worthy: needs {}.", set->requirement.label);
			} else if (const auto missing = Missing(counts, offering); !missing.empty()) {
				body += fmt::format("\n   You lack {}.", missing);
			} else {
				body += "\n   You carry the tribute.";
				buttons.push_back(fmt::format("Offer for {}", set->name));
				ids.push_back(set->id);
			}
		}
		if (sets.size() > kMaxListed) {
			body += fmt::format("\n\n...and {} more.", sets.size() - kMaxListed);
		}
		buttons.emplace_back("Leave");
		ShowBox(body, buttons, new TributeCallback(a_god, std::move(ids)));
	}

	// Called every few seconds: the first time the player comes near a prince's statue that has
	// sets to give, a line hints at it.
	void Shrines::CheckNearbyStatues()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* tes = RE::TES::GetSingleton();
		if (!player || !tes || !player->Is3DLoaded() || _statues.empty()) {
			return;
		}
		auto&              state = State::Get();
		std::optional<int> found;
		tes->ForEachReferenceInRange(player, 1200.0f, [&](RE::TESObjectREFR* a_ref) {
			const auto* base = a_ref ? a_ref->GetBaseObject() : nullptr;
			if (base && _statues.contains(base->GetFormID()) && !a_ref->IsDisabled()) {
				const int god = _byBase.at(base->GetFormID());
				if (!state.noticed.contains(god)) {
					found = god;
					return RE::BSContainer::ForEachResult::kStop;
				}
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
		if (!found) {
			return;
		}
		const bool hasSets = std::ranges::any_of(Catalog::GetSingleton().Sets(), [&](const OutfitSet& a_set) { return a_set.HasGod(*found); });
		if (hasSets) {
			state.noticed.insert(*found);
			Notify(fmt::format("The statue of {} seems to watch you.", GodName(*found)));
		}
	}

	void Shrines::StartWatching()
	{
		std::thread([]() {
			for (;;) {
				std::this_thread::sleep_for(std::chrono::seconds(3));
				if (auto* tasks = SKSE::GetTaskInterface()) {
					tasks->AddTask([]() { Shrines::GetSingleton()->CheckNearbyStatues(); });
				}
			}
		}).detach();
	}
}
