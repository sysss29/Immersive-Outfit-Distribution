#include "Catalog.h"
#include "Sources.h"
#include "State.h"
#include "Stocker.h"

namespace
{
	void InitLogging()
	{
		auto dir = SKSE::log::log_directory();
		if (!dir) {
			return;
		}
		const auto path = *dir / "ImmersiveOutfitDistribution.log";
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
			IOD::ResolveVendorFactions();
			IOD::Catalog::GetSingleton().Load();
			IOD::Stocker::Install();
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	InitLogging();
	spdlog::info("Immersive Outfit Distribution 0.1.0 on Skyrim {}", REL::Module::get().version().string());

	IOD::State::Register();
	if (!SKSE::GetMessagingInterface()->RegisterListener(OnMessage)) {
		spdlog::critical("Could not register for SKSE messages");
		return false;
	}
	return true;
}
