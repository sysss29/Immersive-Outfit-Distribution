"""Writes ImmersiveOutfitDistribution.esp: an ESL-flagged plugin with one quest that hosts the MCM.

The quest is laid out like the MCM quests SkyUI expects (and like SOES's): start-game enabled,
script IOD_MCM (extends SKI_ConfigBase), and a Player alias carrying SKI_PlayerLoadGameAlias so
the menu re-registers on every load. No Creation Kit needed.

Usage: python make_esp.py <output.esp>
"""
import struct
import sys

PLUGIN_INDEX = 0x01  # one master (Skyrim.esm): the plugin's own records use index 01
QUEST_ID = (PLUGIN_INDEX << 24) | 0x800  # ESL-safe range 0x800-0xFFF
PLAYER_REF = 0x14


def zstring(text):
    return text.encode("ascii") + b"\0"


def wstring(text):  # uint16 length + chars, as used inside VMAD
    data = text.encode("ascii")
    return struct.pack("<H", len(data)) + data


def sub(tag, data):
    assert len(tag) == 4 and len(data) < 0x10000
    return tag + struct.pack("<H", len(data)) + data


def record(tag, form_id, data, flags=0):
    # type, size, flags, form id, version control (0), form version 44, unknown
    return tag + struct.pack("<IIIIHH", len(data), flags, form_id, 0, 44, 0) + data


def group(label, records):
    body = b"".join(records)
    return b"GRUP" + struct.pack("<I", 24 + len(body)) + label + struct.pack("<iHHI", 0, 0, 0, 0) + body


def script(name):
    # name, status 0, no properties
    return wstring(name) + b"\0" + struct.pack("<H", 0)


def quest_vmad():
    data = struct.pack("<hhH", 5, 2, 1) + script("IOD_MCM")
    data += struct.pack("<bH", 2, 0) + wstring("")  # fragments: version 2, none, no file
    data += struct.pack("<H", 1)  # one alias
    data += struct.pack("<HhI", 0, 0, QUEST_ID)  # object (format 2): unused, alias 0, quest
    data += struct.pack("<hhH", 5, 2, 1) + script("SKI_PlayerLoadGameAlias")
    return data


def quest():
    data = sub(b"EDID", zstring("IOD_MCMQuest"))
    data += sub(b"VMAD", quest_vmad())
    data += sub(b"FULL", zstring("Immersive Outfit Distribution MCM"))
    data += sub(b"DNAM", bytes.fromhex("110000ff0000000000000000"))  # start game enabled, run once
    data += sub(b"NEXT", b"")
    data += sub(b"ANAM", struct.pack("<I", 1))  # next alias id
    data += sub(b"ALST", struct.pack("<I", 0))
    data += sub(b"ALID", zstring("PlayerAlias"))
    data += sub(b"FNAM", struct.pack("<I", 0x109A))  # same alias flags as SkyUI-style MCM quests
    data += sub(b"ALFR", struct.pack("<I", PLAYER_REF))  # forced reference: the player
    data += sub(b"VTCK", struct.pack("<I", 0))
    data += sub(b"ALED", b"")
    return record(b"QUST", QUEST_ID, data)


def header(num_records):
    data = sub(b"HEDR", struct.pack("<fII", 1.71, num_records, 0x801))
    data += sub(b"CNAM", zstring("sysss29"))
    data += sub(b"SNAM", zstring("Immersive Outfit Distribution: hosts the Mod Configuration Menu. "
                                  "The outfits and vendors are handled by ImmersiveOutfitDistribution.dll."))
    data += sub(b"MAST", zstring("Skyrim.esm")) + sub(b"DATA", struct.pack("<Q", 0))
    return record(b"TES4", 0, data, flags=0x200)  # 0x200: light (ESL)


def main(path):
    body = group(b"QUST", [quest()])
    with open(path, "wb") as out:
        out.write(header(2) + body)  # 2 = one group + one record


if __name__ == "__main__":
    main(sys.argv[1])
