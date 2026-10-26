#include "State.h"

namespace IOD
{
	namespace
	{
		constexpr std::uint32_t kUniqueID = 'IOD1';
		constexpr std::uint32_t kRecord = 'IODS';
		constexpr std::uint32_t kVersion = 4;  // 2: + blessing, offering cooldowns; 3: + noticed statues; 4: + announced gifts

		template <class T>
		bool Write(SKSE::SerializationInterface* a_intfc, const T& a_value)
		{
			return a_intfc->WriteRecordData(&a_value, static_cast<std::uint32_t>(sizeof(T)));
		}

		bool WriteString(SKSE::SerializationInterface* a_intfc, const std::string& a_text)
		{
			const auto size = static_cast<std::uint32_t>(a_text.size());
			return Write(a_intfc, size) && (size == 0 || a_intfc->WriteRecordData(a_text.data(), size));
		}

		template <class T>
		bool Read(SKSE::SerializationInterface* a_intfc, T& a_value)
		{
			return a_intfc->ReadRecordData(&a_value, static_cast<std::uint32_t>(sizeof(T))) == sizeof(T);
		}

		bool ReadString(SKSE::SerializationInterface* a_intfc, std::string& a_text)
		{
			std::uint32_t size = 0;
			if (!Read(a_intfc, size) || size > 4096) {
				return false;
			}
			a_text.resize(size);
			return size == 0 || a_intfc->ReadRecordData(a_text.data(), size) == size;
		}

		std::uint32_t NewSalt()
		{
			std::random_device rd;
			return rd();
		}

		void OnSave(SKSE::SerializationInterface* a_intfc)
		{
			auto& s = State::Get();
			if (!a_intfc->OpenRecord(kRecord, kVersion)) {
				spdlog::error("Could not open save record");
				return;
			}
			bool ok = Write(a_intfc, s.salt) && Write(a_intfc, s.allocPeriod);
			ok = ok && Write(a_intfc, static_cast<std::uint32_t>(s.allocation.size()));
			for (const auto& [faction, sets] : s.allocation) {
				ok = ok && Write(a_intfc, faction) && Write(a_intfc, static_cast<std::uint32_t>(sets.size()));
				for (const auto& id : sets) {
					ok = ok && WriteString(a_intfc, id);
				}
			}
			ok = ok && Write(a_intfc, static_cast<std::uint32_t>(s.stocked.size()));
			for (const auto& [faction, period] : s.stocked) {
				ok = ok && Write(a_intfc, faction) && Write(a_intfc, period);
			}
			ok = ok && Write(a_intfc, static_cast<std::uint32_t>(s.owned.size()));
			for (const auto& id : s.owned) {
				ok = ok && WriteString(a_intfc, id);
			}
			ok = ok && Write(a_intfc, s.blessing) && Write(a_intfc, static_cast<std::uint32_t>(s.lastOffering.size()));
			for (const auto& [god, day] : s.lastOffering) {
				ok = ok && Write(a_intfc, god) && Write(a_intfc, day);
			}
			ok = ok && Write(a_intfc, static_cast<std::uint32_t>(s.noticed.size()));
			for (const auto god : s.noticed) {
				ok = ok && Write(a_intfc, god);
			}
			ok = ok && Write(a_intfc, static_cast<std::uint32_t>(s.announced.size()));
			for (const auto& id : s.announced) {
				ok = ok && WriteString(a_intfc, id);
			}
			if (!ok) {
				spdlog::error("Failed to write save data");
			}
		}

		void OnLoad(SKSE::SerializationInterface* a_intfc)
		{
			auto& s = State::Get();
			s.Reset();
			std::uint32_t type = 0;
			std::uint32_t version = 0;
			std::uint32_t length = 0;
			while (a_intfc->GetNextRecordInfo(type, version, length)) {
				if (type != kRecord || version < 1 || version > kVersion) {
					continue;
				}
				bool ok = Read(a_intfc, s.salt) && Read(a_intfc, s.allocPeriod);
				std::uint32_t count = 0;
				ok = ok && Read(a_intfc, count);
				for (std::uint32_t i = 0; ok && i < count; ++i) {
					RE::FormID faction = 0;
					std::uint32_t sets = 0;
					ok = Read(a_intfc, faction) && Read(a_intfc, sets);
					std::vector<std::string> ids;
					for (std::uint32_t j = 0; ok && j < sets; ++j) {
						std::string id;
						ok = ReadString(a_intfc, id);
						ids.push_back(std::move(id));
					}
					RE::FormID resolved = 0;
					if (ok && a_intfc->ResolveFormID(faction, resolved)) {
						s.allocation[resolved] = std::move(ids);
					}
				}
				ok = ok && Read(a_intfc, count);
				for (std::uint32_t i = 0; ok && i < count; ++i) {
					RE::FormID faction = 0;
					std::int32_t period = 0;
					ok = Read(a_intfc, faction) && Read(a_intfc, period);
					RE::FormID resolved = 0;
					if (ok && a_intfc->ResolveFormID(faction, resolved)) {
						s.stocked[resolved] = period;
					}
				}
				ok = ok && Read(a_intfc, count);
				for (std::uint32_t i = 0; ok && i < count; ++i) {
					std::string id;
					ok = ReadString(a_intfc, id);
					if (ok) {
						s.owned.insert(std::move(id));
					}
				}
				if (version >= 2) {
					ok = ok && Read(a_intfc, s.blessing) && Read(a_intfc, count);
					for (std::uint32_t i = 0; ok && i < count; ++i) {
						std::int32_t god = 0;
						float        day = 0.0f;
						ok = Read(a_intfc, god) && Read(a_intfc, day);
						if (ok) {
							s.lastOffering[god] = day;
						}
					}
				}
				if (version >= 3) {
					ok = ok && Read(a_intfc, count);
					for (std::uint32_t i = 0; ok && i < count; ++i) {
						std::int32_t god = 0;
						ok = Read(a_intfc, god);
						if (ok) {
							s.noticed.insert(god);
						}
					}
				}
				if (version >= 4) {
					ok = ok && Read(a_intfc, count);
					for (std::uint32_t i = 0; ok && i < count; ++i) {
						std::string id;
						ok = ReadString(a_intfc, id);
						if (ok) {
							s.announced.insert(std::move(id));
						}
					}
				}
				if (!ok) {
					spdlog::error("Save data was incomplete; starting fresh");
					s.Reset();
				}
			}
			spdlog::info("Loaded: period {}, {} vendor(s) allocated, {} set(s) owned, blessing {}", s.allocPeriod, s.allocation.size(), s.owned.size(), s.blessing);
		}

		void OnRevert(SKSE::SerializationInterface*)
		{
			State::Get().Reset();
		}
	}

	State& State::Get()
	{
		static State singleton;
		return singleton;
	}

	void State::Reset()
	{
		salt = NewSalt();
		allocPeriod = -1;
		allocation.clear();
		stocked.clear();
		owned.clear();
		blessing = -1;
		lastOffering.clear();
		noticed.clear();
		announced.clear();
	}

	void State::Register()
	{
		auto* serialization = SKSE::GetSerializationInterface();
		serialization->SetUniqueID(kUniqueID);
		serialization->SetSaveCallback(OnSave);
		serialization->SetLoadCallback(OnLoad);
		serialization->SetRevertCallback(OnRevert);
		Get().Reset();
	}
}
