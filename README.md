# Immersive Outfit Distribution (IOD)

Makes curated outfits from armour mods obtainable in Skyrim the way the world would offer them:
sold as full sets by vendors who fit their style, on a 48-hour rotation, instead of through
`additem` or crafting. Shrines and offerings follow in a later milestone.

Personal project for Skyrim SE **1.7.104** (SKSE 2.3.1). No support is offered.

## Status: milestone 2 (+ MCM)

- Full sets stocked at **Radiant Raiment** (Endarie and Taarie, Solitude), **Khajiit caravans**
  and **Thieves Guild** fences.
- Vendors keep their vanilla trade rules: Radiant Raiment only sells clothing and jewellery, so an
  armour set placed there never appears (the log warns at startup; the in-game settings page will
  warn too, milestone 2). Caravans and fences trade anything.
- Each vendor shows 0 to 3 sets per 48 in-game hours (weighted towards 1-2), drawn by rarity;
  a set appears at one vendor at a time. Reloading a save keeps the draw.
- Optional per-set price (spread over the pieces) and one requirement: level, base
  Health/Magicka/Stamina, skill, faction rank or completed quest.
- Unique sets leave rotation once you own every piece.

## Install (MO2)

1. Requirements: SKSE 2.3.1, Address Library for SKSE Plugins (v13+).
2. Install the release zip as a mod. It contains `SKSE/Plugins/ImmersiveOutfitDistribution.dll`
   and the set folder `SKSE/Plugins/ImmersiveOutfitDistribution/`.
3. Recommended: SOES-NG in Immersive inventory mode, so outfits must be owned to be worn.

## Configuring sets

In the Mod Configuration Menu (SkyUI), **Immersive Outfit Distribution**: every set's switches
(enabled, where it's sold, rarity, price, requirement), this period's vendor stock with
**Redraw now**, and the settings. Needs `ImmersiveOutfitDistribution.esp` enabled (ESL-flagged, so
it takes no load-order slot).

To build new sets, press F1 (SKSE Menu Framework) and open **Immersive Outfit Distribution**:

- **Sets**: every set, with where it's sold and warnings (e.g. armour placed at Radiant Raiment);
  click one to edit sources, rarity, price, requirement and pieces. Saving applies at once.
- **New set**: pick a mod, then one of its outfit records or a hand-picked list of pieces.
- **Vendors**: what each vendor has this 48-hour period, and **Redraw now** for testing.
- **Settings**: the "New arrivals" message, and a testing option that gives every vendor at
  least one set per period (saved in `SKSE/Plugins/ImmersiveOutfitDistribution.json`).

By hand:

Every `*.json` file in `SKSE/Plugins/ImmersiveOutfitDistribution/` is read at game start. See
`config/MyOutfits.json` for the format and all options. An in-game configuration screen comes in
milestone 2.

Release zips contain only the plugin, never a set file, so installing an update over the
existing mod (MO2: Merge) keeps your sets.

The log is `Documents/My Games/Skyrim Special Edition/SKSE/ImmersiveOutfitDistribution.log`.

## Build

GitHub Actions (`.github/workflows/build.yml`) builds with MSVC against CommonLibSSE-NG v12.1.0
(submodule `extern/CommonLibVR-ng`) and publishes the mod package as a workflow artifact.

Game data references are in `docs/game-data.md`.

## Licence

MIT. CommonLibSSE-NG is used under its own licence.
