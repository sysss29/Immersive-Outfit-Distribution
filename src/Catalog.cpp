#include "Catalog.h"

#include "Gods.h"

namespace IOD
{
	namespace
	{
		constexpr auto kConfigDir = "Data/SKSE/Plugins/ImmersiveOutfitDistribution"sv;

		std::string Lower(std::string_view a_text)
		{
			std::string out(a_text);
			std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		std::string Trim(std::string_view a_text)
		{
			const auto begin = a_text.find_first_not_of(" \t");
			if (begin == std::string_view::npos) {
				return {};
			}
			const auto end = a_text.find_last_not_of(" \t");
			return std::string(a_text.substr(begin, end - begin + 1));
		}

		std::optional<Rarity> ParseRarity(std::string_view a_text)
		{
			const auto key = Lower(a_text);
			if (key == "common") {
				return Rarity::kCommon;
			}
			if (key == "rare") {
				return Rarity::kRare;
			}
			if (key == "legendary") {
				return Rarity::kLegendary;
			}
			return std::nullopt;
		}

		// Each piece's value before IOD priced it, so a reload can undo a removed price.
		std::unordered_map<RE::TESObjectARMO*, std::int32_t> g_originalValues;

		void AddPiece(OutfitSet& a_set, RE::TESObjectARMO* a_armor)
		{
			if (a_armor && std::ranges::find(a_set.pieces, a_armor) == a_set.pieces.end()) {
				a_set.pieces.push_back(a_armor);
			}
		}
	}

	RE::TESForm* LookupFormString(std::string_view a_text)
	{
		std::string plugin;
		std::string id;
		if (const auto bar = a_text.find('|'); bar != std::string_view::npos) {
			plugin = Trim(a_text.substr(0, bar));
			id = Trim(a_text.substr(bar + 1));
		} else if (const auto tilde = a_text.find('~'); tilde != std::string_view::npos) {
			id = Trim(a_text.substr(0, tilde));
			plugin = Trim(a_text.substr(tilde + 1));
		} else {
			// No plugin: an editor ID, e.g. "MQ202". Quests and a few other records always keep
			// theirs; other records only with powerofthree's Tweaks.
			const auto editorID = Trim(a_text);
			return editorID.empty() ? nullptr : RE::TESForm::LookupByEditorID(editorID);
		}
		RE::FormID localID = 0;
		try {
			localID = static_cast<RE::FormID>(std::stoul(id, nullptr, 16));
		} catch (...) {
			return nullptr;
		}
		auto* data = RE::TESDataHandler::GetSingleton();
		return data ? data->LookupForm(localID & 0x00FFFFFF, plugin) : nullptr;
	}

	std::string FormString(const RE::TESForm* a_form)
	{
		const auto* file = a_form ? a_form->GetFile(0) : nullptr;
		if (!file) {
			return {};
		}
		const auto local = file->IsLight() ? (a_form->GetFormID() & 0xFFF) : (a_form->GetFormID() & 0xFFFFFF);
		return fmt::format("{}|0x{:X}", file->GetFilename(), local);
	}

	std::optional<Offering> ParseOffering(const nlohmann::json& a_json, std::string_view a_context)
	{
		if (!a_json.is_array()) {
			return std::nullopt;
		}
		Offering offering;
		for (const auto& line : a_json) {
			OfferingItem item;
			item.count = std::max(1, line.value("count", 1));
			const auto refs = line.contains("item") ? line["item"] : nlohmann::json{};
			for (const auto& ref : refs.is_array() ? refs : nlohmann::json::array({ refs })) {
				auto* form = ref.is_string() ? LookupFormString(ref.get<std::string>()) : nullptr;
				if (auto* object = form ? form->As<RE::TESBoundObject>() : nullptr) {
					item.any.push_back(object);
				} else {
					spdlog::warn("{}: offering item {} not found", a_context, ref.dump());
				}
			}
			if (item.any.empty()) {
				return std::nullopt;  // an offering that can't be made is no offering
			}
			offering.push_back(std::move(item));
		}
		return offering.empty() ? std::nullopt : std::optional{ std::move(offering) };
	}

	std::string DescribeOffering(const Offering& a_offering)
	{
		std::string text;
		for (const auto& item : a_offering) {
			auto*       first = item.any.front();
			const bool  gold = first->GetFormID() == 0xF;
			std::string name = gold ? "gold" : first->GetName();
			if (item.any.size() > 1 && !gold) {
				name += " (or similar)";
			}
			const auto line = item.count > 1 || gold ? fmt::format("{} {}", item.count, name) : name;
			text += (text.empty() ? "" : ", ") + line;
		}
		return text;
	}

	bool OutfitSet::HasSource(Source a_source) const
	{
		return std::ranges::find(sources, a_source) != sources.end();
	}

	bool OutfitSet::PlayerHasAllPieces(RE::PlayerCharacter* a_player) const
	{
		if (!a_player || pieces.empty()) {
			return false;
		}
		auto counts = a_player->GetInventoryCounts();
		return std::ranges::all_of(pieces, [&](RE::TESObjectARMO* a_piece) {
			const auto it = counts.find(a_piece);
			return it != counts.end() && it->second > 0;
		});
	}

	Catalog& Catalog::GetSingleton()
	{
		static Catalog singleton;
		return singleton;
	}

	const OutfitSet* Catalog::Find(std::string_view a_id) const
	{
		const auto it = std::ranges::find_if(_sets, [&](const OutfitSet& a_set) { return a_set.id == a_id; });
		return it != _sets.end() ? &*it : nullptr;
	}

	void Catalog::Load()
	{
		_sets.clear();
		const std::filesystem::path dir{ kConfigDir };
		std::error_code ec;
		if (!std::filesystem::is_directory(dir, ec)) {
			spdlog::info("No set folder at {}; nothing to distribute", dir.string());
			return;
		}
		std::vector<std::filesystem::path> files;
		for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
			if (entry.is_regular_file() && Lower(entry.path().extension().string()) == ".json") {
				files.push_back(entry.path());
			}
		}
		std::ranges::sort(files);
		for (const auto& file : files) {
			LoadFile(file);
		}
		ApplyPrices();
		spdlog::info("Catalog: {} set(s) from {} file(s)", _sets.size(), files.size());
	}

	void Catalog::LoadFile(const std::filesystem::path& a_path)
	{
		const auto stem = a_path.stem().string();
		nlohmann::json root;
		try {
			std::ifstream in(a_path);
			root = nlohmann::json::parse(in, nullptr, true, true);  // comments allowed
		} catch (const std::exception& e) {
			spdlog::error("{}: not valid JSON ({})", a_path.filename().string(), e.what());
			return;
		}
		if (!root.contains("sets") || !root["sets"].is_array()) {
			spdlog::warn("{}: no \"sets\" array", a_path.filename().string());
			return;
		}

		for (const auto& entry : root["sets"]) {
			OutfitSet set;
			set.name = entry.value("name", "");
			if (set.name.empty()) {
				spdlog::warn("{}: a set without a name was skipped", stem);
				continue;
			}
			set.id = stem + "/" + set.name;
			const auto context = set.id;

			if (!entry.value("enabled", true)) {
				spdlog::info("{}: disabled", context);
				continue;
			}
			if (Find(set.id)) {
				spdlog::warn("{}: duplicate name, skipped", context);
				continue;
			}

			// Pieces: an outfit record the mod defines, and/or individual armour records.
			if (entry.contains("outfit")) {
				const auto ref = entry.value("outfit", "");
				auto* outfit = LookupFormString(ref) ? LookupFormString(ref)->As<RE::BGSOutfit>() : nullptr;
				if (outfit) {
					for (auto* item : outfit->outfitItems) {
						AddPiece(set, item ? item->As<RE::TESObjectARMO>() : nullptr);
					}
				} else {
					spdlog::info("{}: outfit {} not loaded", context, ref);
				}
			}
			if (entry.contains("pieces") && entry["pieces"].is_array()) {
				for (const auto& piece : entry["pieces"]) {
					const auto ref = piece.is_string() ? piece.get<std::string>() : std::string{};
					auto* form = LookupFormString(ref);
					auto* armor = form ? form->As<RE::TESObjectARMO>() : nullptr;
					if (armor) {
						AddPiece(set, armor);
					} else {
						spdlog::info("{}: piece {} not loaded", context, ref);
					}
				}
			}
			if (set.pieces.empty()) {
				spdlog::info("{}: skipped, none of its pieces are loaded (mod not installed?)", context);
				continue;
			}

			if (entry.contains("sources") && entry["sources"].is_array()) {
				for (const auto& source : entry["sources"]) {
					const auto text = source.is_string() ? source.get<std::string>() : std::string{};
					if (const auto parsed = ParseSource(text)) {
						if (!set.HasSource(*parsed)) {
							set.sources.push_back(*parsed);
						}
					} else if (const auto god = ParseGod(text)) {
						if (!set.HasGod(*god)) {
							set.gods.push_back(*god);
						}
					} else {
						spdlog::info("{}: source \"{}\" not supported yet, ignored", context, text);
					}
				}
			}
			if (entry.contains("offering")) {
				if (auto offering = ParseOffering(entry["offering"], context)) {
					set.offering = std::move(*offering);
				} else {
					spdlog::warn("{}: offering can't be used; the god's default applies", context);
				}
			}
			if (set.sources.empty() && set.gods.empty()) {
				spdlog::info("{}: no usable source, not distributed", context);
				continue;
			}

			if (entry.contains("rarity")) {
				if (const auto rarity = ParseRarity(entry.value("rarity", ""))) {
					set.rarity = *rarity;
				} else {
					spdlog::warn("{}: unknown rarity, using Common", context);
				}
			}
			if (entry.contains("price") && entry["price"].is_number_integer()) {
				set.price = std::max(0, entry["price"].get<std::int32_t>());
			}
			if (entry.contains("requirement")) {
				if (auto req = ParseRequirement(entry["requirement"], context)) {
					set.requirement = std::move(*req);
				} else {
					set.requirement.type = Requirement::Type::kInvalid;
					set.requirement.label = "invalid requirement";
					spdlog::warn("{}: requirement can't be used; the set stays hidden until it's fixed", context);
				}
			}
			set.unique = entry.value("unique", true);

			spdlog::info("{}: {} piece(s), {} vendor source(s), {} shrine(s)", context, set.pieces.size(), set.sources.size(), set.gods.size());
			_sets.push_back(std::move(set));
		}
	}

	// Spreads each set's price over its pieces in proportion to their original values (evenly if
	// they have none). Base values are global, so a piece in two priced sets takes the last one.
	void Catalog::ApplyPrices()
	{
		for (const auto& [piece, value] : g_originalValues) {
			piece->value = value;
		}
		std::unordered_map<RE::TESObjectARMO*, std::string> pricedBy;
		for (const auto& set : _sets) {
			if (!set.price) {
				continue;
			}
			std::int64_t total = 0;
			for (auto* piece : set.pieces) {
				total += std::max(0, piece->value);
			}
			std::int32_t assigned = 0;
			for (std::size_t i = 0; i < set.pieces.size(); ++i) {
				auto* piece = set.pieces[i];
				std::int32_t value = 0;
				if (i + 1 == set.pieces.size()) {
					value = *set.price - assigned;
				} else if (total > 0) {
					value = static_cast<std::int32_t>(std::llround(static_cast<double>(*set.price) * std::max(0, piece->value) / static_cast<double>(total)));
				} else {
					value = *set.price / static_cast<std::int32_t>(set.pieces.size());
				}
				value = std::max(0, value);
				assigned += value;
				if (const auto it = pricedBy.find(piece); it != pricedBy.end() && it->second != set.id) {
					spdlog::warn("{}: piece also priced by {}; the last one wins", set.id, it->second);
				}
				pricedBy[piece] = set.id;
				g_originalValues.try_emplace(piece, piece->value);
				piece->value = value;
			}
			spdlog::info("{}: priced at {} gold", set.id, *set.price);
		}
	}
}
