#include "SetFiles.h"

#include "Catalog.h"

namespace IOD
{
	namespace
	{
		constexpr auto kHeader = R"(// Immersive Outfit Distribution - set file.
// Every *.json file in Data/SKSE/Plugins/ImmersiveOutfitDistribution/ is loaded at game start.
// The in-game settings page (F1) edits these files; editing by hand works too.
// Form references are "Plugin.esp|0xLocalID" (ESL plugins work too).
//
// Per set:
//   name         required, unique within this file
//   outfit       an outfit record the mod defines (all its armour pieces are used), and/or
//   pieces       a list of armour records
//   sources      "Radiant Raiment", "Khajiit caravans", "Thieves Guild"
//                Radiant Raiment only trades clothing and jewellery: armour sets placed there never
//                appear (the log and the settings page warn). The caravans and fences trade anything.
//   rarity       "Common" (default), "Rare", "Legendary"
//   price        gold for the whole set (optional; spread over the pieces)
//   requirement  optional, one of:
//                  {"type": "level", "value": 10}
//                  {"type": "health" | "magicka" | "stamina", "value": 200}
//                  {"type": "skill", "skill": "Restoration", "value": 20}
//                  {"type": "faction", "faction": "Skyrim.esm|0x29DA9", "rank": 1}
//                  {"type": "quest", "quest": "Plugin.esp|0x...."}
//   unique       true (default): leaves rotation once you own every piece
//   enabled      true (default)
)";

		constexpr std::array kFieldOrder{ "name"sv, "enabled"sv, "outfit"sv, "pieces"sv, "sources"sv, "rarity"sv, "price"sv, "requirement"sv, "unique"sv };

		std::string Dump(const nlohmann::json& a_value)
		{
			return a_value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
		}

		std::string CommentFor(const nlohmann::json& a_ref)
		{
			if (!a_ref.is_string()) {
				return {};
			}
			auto*       form = LookupFormString(a_ref.get<std::string>());
			std::string name = form ? form->GetName() : "not loaded";
			std::ranges::replace(name, '\n', ' ');
			std::ranges::replace(name, '\r', ' ');
			return name.empty() ? std::string{} : "  // " + name;
		}

		std::string WriteSet(const nlohmann::json& a_set)
		{
			std::vector<std::string> lines;
			auto                     field = [&](std::string_view a_key, const nlohmann::json& a_value) {
                if (a_key == "pieces" && a_value.is_array()) {
                    std::string text = fmt::format("      \"pieces\": [");
                    if (a_value.empty()) {
                        lines.push_back(text + "]");
                        return;
                    }
                    lines.push_back(text);
                    for (std::size_t i = 0; i < a_value.size(); ++i) {
                        const bool last = i + 1 == a_value.size();
                        lines.push_back(fmt::format("        {}{}{}", Dump(a_value[i]), last ? "" : ",", CommentFor(a_value[i])));
                    }
                    lines.push_back("      ]");
                    return;
                }
                std::string line = fmt::format("      {}: {}", Dump(std::string(a_key)), Dump(a_value));
                if (a_key == "outfit") {
                    line += "\x01" + CommentFor(a_value);  // comment goes after the comma
                }
                lines.push_back(std::move(line));
			};
			for (const auto key : kFieldOrder) {
				if (a_set.contains(key)) {
					field(key, a_set.at(std::string(key)));
				}
			}
			for (const auto& [key, value] : a_set.items()) {
				if (std::ranges::find(kFieldOrder, std::string_view(key)) == kFieldOrder.end()) {
					field(key, value);
				}
			}
			// Commas between fields: after the value, before any trailing comment.
			std::string out = "    {\n";
			for (std::size_t i = 0; i < lines.size(); ++i) {
				auto&      line = lines[i];
				const bool last = i + 1 == lines.size();
				const bool opensArray = line.ends_with("[");
				const bool insideArray = line.starts_with("        ");
				std::string comment;
				if (const auto mark = line.find('\x01'); mark != std::string::npos) {
					comment = line.substr(mark + 1);
					line.erase(mark);
				}
				const bool needsComma = !last && !opensArray && !insideArray;
				out += line + (needsComma ? "," : "") + comment + "\n";
			}
			return out + "    }";
		}
	}

	SetFiles& SetFiles::Get()
	{
		static SetFiles files;
		return files;
	}

	void SetFiles::Load()
	{
		_files.clear();
		const std::filesystem::path dir{ kSetDir };
		std::error_code             ec;
		if (!std::filesystem::is_directory(dir, ec)) {
			return;
		}
		for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
			auto ext = entry.path().extension().string();
			std::ranges::transform(ext, ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (!entry.is_regular_file() || ext != ".json") {
				continue;
			}
			SetFile file{ entry.path(), entry.path().stem().string(), {} };
			try {
				std::ifstream in(file.path);
				file.root = nlohmann::json::parse(in, nullptr, true, true);
			} catch (const std::exception&) {
				continue;  // the Catalog already logged it; never overwrite a file we can't read
			}
			if (!file.root.is_object() || !file.root.contains("sets") || !file.root["sets"].is_array()) {
				continue;
			}
			_files.push_back(std::move(file));
		}
		std::ranges::sort(_files, {}, &SetFile::stem);
	}

	std::size_t SetFiles::DefaultFile()
	{
		for (std::size_t i = 0; i < _files.size(); ++i) {
			if (_files[i].stem == "MyOutfits") {
				return i;
			}
		}
		std::filesystem::path path{ kSetDir };
		path /= "MyOutfits.json";
		_files.push_back({ path, "MyOutfits", nlohmann::json{ { "sets", nlohmann::json::array() } } });
		return _files.size() - 1;
	}

	bool SetFiles::Save(std::size_t a_file)
	{
		if (a_file >= _files.size()) {
			return false;
		}
		const auto& file = _files[a_file];
		std::string text{ kHeader };
		text += "{\n  \"sets\": [";
		const auto& sets = file.Sets();
		for (std::size_t i = 0; i < sets.size(); ++i) {
			text += (i == 0 ? "\n" : ",\n") + WriteSet(sets[i]);
		}
		text += sets.empty() ? "]\n}\n" : "\n  ]\n}\n";

		// Check it reads back before replacing the file.
		try {
			(void)nlohmann::json::parse(text, nullptr, true, true);
		} catch (const std::exception& e) {
			spdlog::error("{}: not saved, the written text would not parse ({})", file.path.filename().string(), e.what());
			return false;
		}
		std::error_code ec;
		std::filesystem::create_directories(file.path.parent_path(), ec);
		std::ofstream out(file.path, std::ios::binary | std::ios::trunc);
		if (!out) {
			spdlog::error("Could not write {}", file.path.string());
			return false;
		}
		out << text;
		spdlog::info("{} saved ({} set(s))", file.path.filename().string(), sets.size());
		return static_cast<bool>(out);
	}
}
