#include "Catalog.h"
#include "Settings.h"
#include "Sources.h"
#include "State.h"
#include "Stocker.h"
#include "UI.h"

namespace
{
	void InitLogging()
	{
		auto dir = SKSE::log::log_directory();
		if (!dir) {
			return;
		}
		const auto path = *dir / "ImmersiveOutfitDistribution.log";
		// Keep the previous session's log (useful after a freeze or crash).
		std::error_code ec;
		std::filesystem::rename(path, *dir / "ImmersiveOutfitDistribution.previous.log", ec);
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true);
		auto log = std::make_shared<spdlog::logger>("IOD", std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		log->set_pattern("[%H:%M:%S.%e] [%l] %v");
		spdlog::set_default_logger(std::move(log));
	}

	void OnMessage(SKSE::MessagingInterface::Message* a_message)
	{
		if (a_message->type == SKSE::MessagingInterface::kDataLoaded) {
			IOD::Settings::Get().Load();
			IOD::ResolveVendorFactions();
			IOD::Catalog::GetSingleton().Load();
			IOD::Stocker::CheckTradeable();
			IOD::Stocker::Install();
			IOD::UI::Register();
		} else if (a_message->type == SKSE::MessagingInterface::kPostLoadGame ||
				   a_message->type == SKSE::MessagingInterface::kNewGame) {
			IOD::Stocker::OnGameLoaded();
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	InitLogging();
	spdlog::info("Immersive Outfit Distribution 0.2.1 on Skyrim {}", REL::Module::get().version().string());

	IOD::State::Register();
	if (!SKSE::GetMessagingInterface()->RegisterListener(OnMessage)) {
		spdlog::critical("Could not register for SKSE messages");
		return false;
	}
	return true;
}
