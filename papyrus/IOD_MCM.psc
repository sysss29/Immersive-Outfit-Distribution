Scriptname IOD_MCM extends SKI_ConfigBase
{Immersive Outfit Distribution: everyday settings in the Mod Configuration Menu.
 Building new sets from a mod's pieces stays in the F1 panel (SKSE Menu Framework).}

; Requirement types, as in IOD_Native.Choices("reqtype")
Int Property REQ_NONE = 0 AutoReadOnly
Int Property REQ_LEVEL = 1 AutoReadOnly
Int Property REQ_HEALTH = 2 AutoReadOnly
Int Property REQ_MAGICKA = 3 AutoReadOnly
Int Property REQ_STAMINA = 4 AutoReadOnly
Int Property REQ_SKILL = 5 AutoReadOnly
Int Property REQ_FACTION = 6 AutoReadOnly
Int Property REQ_QUEST = 7 AutoReadOnly
Int Property REQ_BLESSING = 8 AutoReadOnly
Int Property FACTION_OTHER = 8 AutoReadOnly
Int Property QUEST_OTHER = 6 AutoReadOnly

Int selected = -1
Int[] listIds
Int[] vendorIds
String[] vendorNames
String[] vendorStock

; Options of the set editor (-1 when not on the page)
Int oBack = -1
Int oEnabled = -1
Int oUnique = -1
Int[] oSources
Int[] oWarnings
Int oPieces = -1
Int oRarity = -1
Int oHasPrice = -1
Int oPrice = -1
Int oReqType = -1
Int oReqValue = -1
Int oReqSkill = -1
Int oReqFaction = -1
Int oReqGod = -1
Int oReqQuest = -1
Int oReqRef = -1
Int oReload = -1
Int oRedraw = -1
Int oArrival = -1
Int oGuarantee = -1

Int Function GetVersion()
	Return 1
EndFunction

Event OnConfigInit()
	ModName = "Immersive Outfit Distribution"
	Pages = new String[3]
	Pages[0] = "Sets"
	Pages[1] = "Vendors"
	Pages[2] = "Settings"
EndEvent

Event OnConfigOpen()
	selected = -1
EndEvent

Function ClearIds()
	listIds = new Int[128]
	vendorIds = new Int[16]
	oSources = new Int[3]
	oWarnings = new Int[3]
	Int i = 0
	While i < 128
		listIds[i] = -1
		i += 1
	EndWhile
	i = 0
	While i < 16
		vendorIds[i] = -1
		i += 1
	EndWhile
	i = 0
	While i < 3
		oSources[i] = -1
		oWarnings[i] = -1
		i += 1
	EndWhile
	oBack = -1
	oEnabled = -1
	oUnique = -1
	oPieces = -1
	oRarity = -1
	oHasPrice = -1
	oPrice = -1
	oReqType = -1
	oReqValue = -1
	oReqSkill = -1
	oReqFaction = -1
	oReqGod = -1
	oReqQuest = -1
	oReqRef = -1
	oReload = -1
	oRedraw = -1
	oArrival = -1
	oGuarantee = -1
EndFunction

Event OnPageReset(String asPage)
	ClearIds()
	SetCursorFillMode(TOP_TO_BOTTOM)
	If asPage == "Vendors"
		ShowVendors()
	ElseIf asPage == "Settings"
		ShowSettings()
	ElseIf selected >= 0 && selected < IOD_Native.SetNames().Length
		ShowEditor()
	Else
		selected = -1
		ShowSetList()
	EndIf
EndEvent

; ---- pages --------------------------------------------------------------------------------

Function ShowSetList()
	String[] names = IOD_Native.SetNames()
	String[] statuses = IOD_Native.SetStatuses()
	Int count = names.Length
	If count > 120
		count = 120
	EndIf
	Int half = (count + 1) / 2
	SetCursorPosition(0)
	AddHeaderOption("Sets - select one to edit")
	Int i = 0
	While i < count
		If i == half
			SetCursorPosition(1)
			AddHeaderOption("")
		EndIf
		listIds[i] = AddTextOption(names[i], statuses[i])
		i += 1
	EndWhile
	If names.Length == 0
		AddTextOption("No sets yet", "", OPTION_FLAG_DISABLED)
	EndIf
	If half == count || count == 0
		SetCursorPosition(1)
	EndIf
	AddEmptyOption()
	AddHeaderOption("New sets")
	AddTextOption("Press F1, then Immersive", "", OPTION_FLAG_DISABLED)
	AddTextOption("Outfit Distribution, New set", "", OPTION_FLAG_DISABLED)
	oReload = AddTextOption("Reload set files", "Reload")
EndFunction

Function ShowEditor()
	Int s = selected
	SetCursorPosition(0)
	AddHeaderOption(IOD_Native.GetText(s, "name"))
	oBack = AddTextOption("Back to all sets", "")
	oEnabled = AddToggleOption("Enabled", IOD_Native.GetBool(s, "enabled"))
	oUnique = AddToggleOption("Leaves rotation once owned", IOD_Native.GetBool(s, "unique"))
	AddHeaderOption("Sold at")
	String[] sources = new String[3]
	sources[0] = "Radiant Raiment"
	sources[1] = "Khajiit caravans"
	sources[2] = "Thieves Guild fences"
	Int i = 0
	While i < 3
		oSources[i] = AddToggleOption(sources[i], IOD_Native.GetBool(s, "src" + i))
		If IOD_Native.GetText(s, "warn" + i) != ""
			oWarnings[i] = AddTextOption("   can't sell this kind of item", "?", OPTION_FLAG_DISABLED)
		EndIf
		i += 1
	EndWhile
	AddHeaderOption("Pieces")
	oPieces = AddTextOption("" + IOD_Native.GetInt(s, "pieces") + " pieces", "highlight to list")
	AddTextOption("Add or remove pieces: F1", "", OPTION_FLAG_DISABLED)

	SetCursorPosition(1)
	AddHeaderOption("Rarity and price")
	String[] rarities = ListOf("rarity")
	oRarity = AddMenuOption("Rarity", rarities[IOD_Native.GetInt(s, "rarity")])
	Bool hasPrice = IOD_Native.GetBool(s, "hasprice")
	oHasPrice = AddToggleOption("Set a price", hasPrice)
	Int priceFlags = OPTION_FLAG_NONE
	If !hasPrice
		priceFlags = OPTION_FLAG_DISABLED
	EndIf
	oPrice = AddSliderOption("Whole set", IOD_Native.GetInt(s, "price"), "{0} gold", priceFlags)

	AddHeaderOption("Requirement")
	Int t = IOD_Native.GetInt(s, "reqtype")
	String[] types = ListOf("reqtype")
	oReqType = AddMenuOption("Type", types[t])
	Int v = IOD_Native.GetInt(s, "reqvalue")
	If t == REQ_LEVEL
		oReqValue = AddSliderOption("Character level", v, "{0}")
	ElseIf t == REQ_HEALTH || t == REQ_MAGICKA || t == REQ_STAMINA
		oReqValue = AddSliderOption("At least", v, "{0}")
	ElseIf t == REQ_SKILL
		String[] skills = ListOf("skills")
		oReqSkill = AddMenuOption("Skill", skills[IOD_Native.GetInt(s, "reqskill")])
		oReqValue = AddSliderOption("At least", v, "{0}")
	ElseIf t == REQ_FACTION
		String[] factions = ListOf("factions")
		Int f = IOD_Native.GetInt(s, "reqfaction")
		oReqFaction = AddMenuOption("Faction", factions[f])
		If f == FACTION_OTHER
			oReqRef = AddInputOption("Faction form", IOD_Native.GetText(s, "reqref"))
		EndIf
		Int maxRank = IOD_Native.GetInt(s, "maxrank")
		If maxRank > 0
			oReqValue = AddSliderOption("Rank at least", v, "{0}")
		ElseIf maxRank == 0
			AddTextOption("Membership is enough", "", OPTION_FLAG_DISABLED)
		Else
			AddTextOption("Faction not found", "", OPTION_FLAG_DISABLED)
		EndIf
		AddTextOption("Guild progress: see Quest completed", "", OPTION_FLAG_DISABLED)
	ElseIf t == REQ_QUEST
		String[] quests = ListOf("quests")
		Int q = IOD_Native.GetInt(s, "reqquest")
		oReqQuest = AddMenuOption("Quest", quests[q])
		If q == QUEST_OTHER
			oReqRef = AddInputOption("Editor ID or form", IOD_Native.GetText(s, "reqref"))
		EndIf
		AddTextOption(IOD_Native.GetText(s, "reqrefcheck"), "", OPTION_FLAG_DISABLED)
	ElseIf t == REQ_BLESSING
		String[] gods = ListOf("gods")
		oReqGod = AddMenuOption("God", gods[IOD_Native.GetInt(s, "reqgod")])
		AddTextOption("Shrines come in milestone 4", "", OPTION_FLAG_DISABLED)
	EndIf
	AddEmptyOption()
	AddTextOption("File", IOD_Native.GetText(s, "file"), OPTION_FLAG_DISABLED)
EndFunction

Function ShowVendors()
	vendorNames = IOD_Native.VendorNames()
	vendorStock = IOD_Native.VendorStock()
	SetCursorPosition(0)
	AddHeaderOption("This period's stock")
	If vendorNames.Length == 0
		AddTextOption("Load a save first", "", OPTION_FLAG_DISABLED)
	EndIf
	Int i = 0
	While i < vendorNames.Length && i < 16
		vendorIds[i] = AddTextOption(vendorNames[i], vendorStock[i])
		i += 1
	EndWhile
	SetCursorPosition(1)
	AddHeaderOption("Testing")
	AddTextOption("Next draw in", "" + IOD_Native.HoursToNextDraw() + " h", OPTION_FLAG_DISABLED)
	oRedraw = AddTextOption("Redraw now", "Redraw")
	AddTextOption("Fences outside Riften trade", "", OPTION_FLAG_DISABLED)
	AddTextOption("only after their quests", "", OPTION_FLAG_DISABLED)
EndFunction

Function ShowSettings()
	SetCursorPosition(0)
	AddHeaderOption("Vendors")
	oArrival = AddToggleOption("New arrivals message", IOD_Native.GetSetting("arrival"))
	AddHeaderOption("Testing")
	oGuarantee = AddToggleOption("A set at every vendor", IOD_Native.GetSetting("guarantee"))
EndFunction

; ---- helpers ------------------------------------------------------------------------------

; Lists for the menu dialogs, kept in the script (same order as the plugin uses).
String[] Function ListOf(String asList)
	String[] out
	If asList == "reqtype"
		out = new String[9]
		out[0] = "No requirement"
		out[1] = "Character level"
		out[2] = "Base Health"
		out[3] = "Base Magicka"
		out[4] = "Base Stamina"
		out[5] = "Skill level"
		out[6] = "Faction rank"
		out[7] = "Quest completed"
		out[8] = "Blessing of a god"
		Return out
	EndIf
	out = IOD_Native.Choices(asList)
	If out.Length == 0
		Debug.Trace("IOD_MCM: empty list " + asList)
	EndIf
	Return out
EndFunction

Function Report(String asStatus)
	; Saves report "Saved to ..."; anything else is a problem worth showing.
	If asStatus != "" && StringUtil.Find(asStatus, "Saved") != 0
		ShowMessage(asStatus, False)
	EndIf
EndFunction

Int Function ListIndex(Int aiOption)
	Int i = 0
	While i < 128
		If listIds[i] == aiOption
			Return i
		EndIf
		i += 1
	EndWhile
	Return -1
EndFunction

Int Function SourceIndex(Int aiOption)
	Int i = 0
	While i < 3
		If oSources[i] == aiOption
			Return i
		EndIf
		i += 1
	EndWhile
	Return -1
EndFunction

; ---- events -------------------------------------------------------------------------------

Event OnOptionSelect(Int aiOption)
	Int item = ListIndex(aiOption)
	Int src = SourceIndex(aiOption)
	If item >= 0
		selected = item
		ForcePageReset()
	ElseIf aiOption == oBack
		selected = -1
		ForcePageReset()
	ElseIf aiOption == oReload
		IOD_Native.ReloadFiles()
		ForcePageReset()
	ElseIf aiOption == oEnabled
		Report(IOD_Native.SetBool(selected, "enabled", !IOD_Native.GetBool(selected, "enabled")))
		ForcePageReset()
	ElseIf aiOption == oUnique
		Report(IOD_Native.SetBool(selected, "unique", !IOD_Native.GetBool(selected, "unique")))
		ForcePageReset()
	ElseIf aiOption == oHasPrice
		Report(IOD_Native.SetBool(selected, "hasprice", !IOD_Native.GetBool(selected, "hasprice")))
		ForcePageReset()
	ElseIf src >= 0
		Report(IOD_Native.SetBool(selected, "src" + src, !IOD_Native.GetBool(selected, "src" + src)))
		ForcePageReset()
	ElseIf aiOption == oRedraw
		IOD_Native.Redraw()
		ShowMessage("New stock has been drawn. Vendors show it the next time you talk to them.", False)
		ForcePageReset()
	ElseIf aiOption == oArrival
		IOD_Native.SetSetting("arrival", !IOD_Native.GetSetting("arrival"))
		SetToggleOptionValue(aiOption, IOD_Native.GetSetting("arrival"))
	ElseIf aiOption == oGuarantee
		IOD_Native.SetSetting("guarantee", !IOD_Native.GetSetting("guarantee"))
		SetToggleOptionValue(aiOption, IOD_Native.GetSetting("guarantee"))
	EndIf
EndEvent

Event OnOptionSliderOpen(Int aiOption)
	If aiOption == oPrice
		SetSliderDialogStartValue(IOD_Native.GetInt(selected, "price"))
		SetSliderDialogDefaultValue(1000)
		SetSliderDialogRange(0, 20000)
		SetSliderDialogInterval(50)
	ElseIf aiOption == oReqValue
		Int t = IOD_Native.GetInt(selected, "reqtype")
		SetSliderDialogStartValue(IOD_Native.GetInt(selected, "reqvalue"))
		If t == REQ_LEVEL
			SetSliderDialogDefaultValue(10)
			SetSliderDialogRange(1, 81)
			SetSliderDialogInterval(1)
		ElseIf t == REQ_SKILL
			SetSliderDialogDefaultValue(25)
			SetSliderDialogRange(1, 100)
			SetSliderDialogInterval(1)
		ElseIf t == REQ_FACTION
			SetSliderDialogDefaultValue(0)
			SetSliderDialogRange(0, IOD_Native.GetInt(selected, "maxrank"))
			SetSliderDialogInterval(1)
		Else
			SetSliderDialogDefaultValue(200)
			SetSliderDialogRange(50, 1000)
			SetSliderDialogInterval(50)
		EndIf
	EndIf
EndEvent

Event OnOptionSliderAccept(Int aiOption, Float afValue)
	If aiOption == oPrice
		Report(IOD_Native.SetInt(selected, "price", afValue as Int))
		SetSliderOptionValue(aiOption, IOD_Native.GetInt(selected, "price"), "{0} gold")
	ElseIf aiOption == oReqValue
		Report(IOD_Native.SetInt(selected, "reqvalue", afValue as Int))
		SetSliderOptionValue(aiOption, IOD_Native.GetInt(selected, "reqvalue"), "{0}")
	EndIf
EndEvent

Event OnOptionMenuOpen(Int aiOption)
	If aiOption == oRarity
		SetMenuDialogOptions(ListOf("rarity"))
		SetMenuDialogStartIndex(IOD_Native.GetInt(selected, "rarity"))
		SetMenuDialogDefaultIndex(0)
	ElseIf aiOption == oReqType
		SetMenuDialogOptions(ListOf("reqtype"))
		SetMenuDialogStartIndex(IOD_Native.GetInt(selected, "reqtype"))
		SetMenuDialogDefaultIndex(0)
	ElseIf aiOption == oReqSkill
		SetMenuDialogOptions(ListOf("skills"))
		SetMenuDialogStartIndex(IOD_Native.GetInt(selected, "reqskill"))
		SetMenuDialogDefaultIndex(0)
	ElseIf aiOption == oReqGod
		SetMenuDialogOptions(ListOf("gods"))
		SetMenuDialogStartIndex(IOD_Native.GetInt(selected, "reqgod"))
		SetMenuDialogDefaultIndex(0)
	ElseIf aiOption == oReqQuest
		SetMenuDialogOptions(ListOf("quests"))
		SetMenuDialogStartIndex(IOD_Native.GetInt(selected, "reqquest"))
		SetMenuDialogDefaultIndex(0)
	ElseIf aiOption == oReqFaction
		SetMenuDialogOptions(ListOf("factions"))
		SetMenuDialogStartIndex(IOD_Native.GetInt(selected, "reqfaction"))
		SetMenuDialogDefaultIndex(0)
	EndIf
EndEvent

Event OnOptionMenuAccept(Int aiOption, Int aiIndex)
	If aiIndex < 0
		Return
	EndIf
	If aiOption == oRarity
		Report(IOD_Native.SetInt(selected, "rarity", aiIndex))
	ElseIf aiOption == oReqType
		Report(IOD_Native.SetInt(selected, "reqtype", aiIndex))
	ElseIf aiOption == oReqSkill
		Report(IOD_Native.SetInt(selected, "reqskill", aiIndex))
	ElseIf aiOption == oReqGod
		Report(IOD_Native.SetInt(selected, "reqgod", aiIndex))
	ElseIf aiOption == oReqQuest
		Report(IOD_Native.SetInt(selected, "reqquest", aiIndex))
	ElseIf aiOption == oReqFaction
		Report(IOD_Native.SetInt(selected, "reqfaction", aiIndex))
	EndIf
	ForcePageReset()
EndEvent

Event OnOptionInputOpen(Int aiOption)
	If aiOption == oReqRef
		SetInputDialogStartText(IOD_Native.GetText(selected, "reqref"))
	EndIf
EndEvent

Event OnOptionInputAccept(Int aiOption, String asInput)
	If aiOption == oReqRef
		Report(IOD_Native.SetText(selected, "reqref", asInput))
		ForcePageReset()
	EndIf
EndEvent

Event OnOptionHighlight(Int aiOption)
	Int src = SourceIndex(aiOption)
	Int i = 0
	While i < 16
		If vendorIds[i] == aiOption && aiOption != -1
			SetInfoText(vendorNames[i] + ": " + vendorStock[i])
			Return
		EndIf
		i += 1
	EndWhile
	If src >= 0
		String warning = IOD_Native.GetText(selected, "warn" + src)
		If warning == ""
			SetInfoText("Sell this set here. Each vendor shows 0 to 3 sets every 48 hours.")
		Else
			SetInfoText(warning)
		EndIf
	ElseIf aiOption == oPieces
		SetInfoText(IOD_Native.GetText(selected, "pieces"))
	ElseIf aiOption == oUnique
		SetInfoText("On: once you own every piece, the set stops appearing. Off: it keeps coming back.")
	ElseIf aiOption == oHasPrice || aiOption == oPrice
		SetInfoText("The price is shared out over the pieces, in proportion to their own values. Off: pieces keep the price their mod gave them.")
	ElseIf aiOption == oReqType
		SetInfoText("The set only shows up at vendors once you meet this.")
	ElseIf aiOption == oReqRef
		SetInfoText("An editor ID (e.g. MQ202) or Plugin.esp|0xID.")
	ElseIf aiOption == oRedraw
		SetInfoText("Draws new stock for every vendor now, instead of waiting for the next 48 hours.")
	ElseIf aiOption == oGuarantee
		SetInfoText("For testing: every vendor gets at least one set each period.")
	ElseIf aiOption == oArrival
		SetInfoText("Shows 'New arrivals at ...' when you talk to a vendor that has new sets.")
	EndIf
EndEvent
