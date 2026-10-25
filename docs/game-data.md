# Game data used by IOD

Read from Skyrim SE 1.7.104 master files (Skyrim.esm, Update.esm, Dawnguard.esm, Dragonborn.esm)
on 2026-10-25. Form IDs are local to the plugin named.

## Vendor factions (merchant sources)

| Source | Faction (EditorID) | Form ID | Merchant chest | Trades |
| --- | --- | --- | --- | --- |
| Radiant Raiment | ServicesSolitudeRadiantRaiments (Endarie, Taarie) | Skyrim.esm 0A6C00 | 0A6C0C | 08:00-20:00 |
| Khajiit caravans | CaravanALeader (Ri'saad) | Skyrim.esm 074351 | 07434C | always |
| Khajiit caravans | CaravanBLeader (Ma'dran) | Skyrim.esm 074352 | 074353 | always |
| Khajiit caravans | CaravanCLeader (Ahkari) | Skyrim.esm 074354 | 074355 | always |
| Thieves Guild | ServicesThievesGuildTonilia | Skyrim.esm 0D54C4 | 0D54C7 | always |
| Thieves Guild | ServicesThievesGuildCaravanAtahba | Skyrim.esm 0D8E53 | 0D8E4E | always |
| Thieves Guild | ServicesThievesGuildCaravanMajhad | Skyrim.esm 0D8E52 | 0D8E4F | always |
| Thieves Guild | ServicesThievesGuildCaravanZaynabi | Skyrim.esm 0D8E54 | 0D8E50 | always |

The caravan fences (Atahbah, Ma'jhad, Zaynabi) have no vendor faction on their base record; the
Thieves Guild fence quests add it at runtime, so they only trade (and only receive IOD stock) once
unlocked. Vex (Skyrim.esm 01CD90) has no vendor faction.

## Guild factions (requirement candidates, ranks defined in the record)

| Faction | Form ID | Ranks |
| --- | --- | --- |
| ThievesGuildFaction | Skyrim.esm 029DA9 | 2 |
| CompanionsFaction | Skyrim.esm 048362 | 4 |
| CollegeofWinterholdFaction | Skyrim.esm 01F259 | 7 |
| DarkBrotherhoodFaction | Skyrim.esm 01BDB3 | 0 |
| CWImperialFaction | Skyrim.esm 02BF9A | 0 |
| CWSonsFaction | Skyrim.esm 02BF9B | 0 |
| DLC1DawnguardFaction | Dawnguard.esm 014217 | 0 |
| DLC1VampireFaction | Dawnguard.esm 003376 | 0 |

Factions with 0 ranks don't track progression as a rank; to be checked in game before offering them.

## Divine shrines (activators, milestone 4)

| God | Activator | Form ID |
| --- | --- | --- |
| Akatosh | ShrineofAkatosh | Skyrim.esm 0D9883 |
| Arkay | ShrineofArkay | Skyrim.esm 071854 |
| Dibella | ShrineofDibella | Skyrim.esm 0D9881 |
| Julianos | ShrineofJulianos | Skyrim.esm 0D9885 |
| Kynareth | ShrineOfKynareth | Skyrim.esm 0D987F |
| Mara | ShrineofMara | Skyrim.esm 0D9887 |
| Stendarr | ShrineofStendarr | Skyrim.esm 0D987D |
| Talos | ShrineofTalos | Skyrim.esm 100780 |
| Zenithar | ShrineOfZenithar | Skyrim.esm 0D987B |

Daedric Shrines - All in One (man_DaedricShrines.esp): statues are STAT records (not activatable),
except ShrineOfNocturnal / man_ShrineOfNocturnal (ACTI with a script). Sanguine: aaa_man_saguineStatue
(local 000852), placed twice.
