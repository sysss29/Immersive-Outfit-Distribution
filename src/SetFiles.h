#pragma once

namespace IOD
{
	// The set files as written on disk, for the settings page. Unlike the Catalog (which only
	// keeps what can be distributed), this keeps every entry: disabled sets, sets whose mod is
	// missing, and fields IOD doesn't know. Render thread only.
	struct SetFile
	{
		std::filesystem::path path;
		std::string           stem;
		nlohmann::json        root;  // { "sets": [ ... ] }

		[[nodiscard]] nlohmann::json&       Sets() { return root["sets"]; }
		[[nodiscard]] const nlohmann::json& Sets() const { return root.at("sets"); }
	};

	class SetFiles
	{
	public:
		static SetFiles& Get();

		void                  Load();
		std::vector<SetFile>& Files() { return _files; }
		std::size_t           DefaultFile();  // MyOutfits.json, created in memory if missing
		std::size_t           FileFor(std::string_view a_stem);  // created in memory if missing
		static std::string    NewId();        // permanent set id, e.g. "iod-3fa29c1e"
		static std::string    StemForPlugin(std::string_view a_plugin);  // "Cute Maid.esp" -> "Cute Maid"
		bool                  Save(std::size_t a_file);

	private:
		std::vector<SetFile> _files;
	};

	inline constexpr auto kSetDir = "Data/SKSE/Plugins/ImmersiveOutfitDistribution"sv;
}
