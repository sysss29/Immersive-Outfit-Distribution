Scriptname IOD_Native Hidden
{Native functions of ImmersiveOutfitDistribution.dll, used by the MCM (IOD_MCM).
 Sets are addressed by their position in SetNames(). Every Set* call saves the set file at once
 and returns a status line ("Saved to ..." or why it couldn't).}

; Settings: "arrival" (New arrivals message), "guarantee" (testing: a set at every vendor)
Bool Function GetSetting(String asKey) Global Native
Function SetSetting(String asKey, Bool abValue) Global Native

; Vendors: this period's stock, one line per vendor
String[] Function VendorNames() Global Native
String[] Function VendorStock() Global Native
Int Function HoursToNextDraw() Global Native
Function Redraw() Global Native

; Sets
String[] Function SetNames() Global Native
String[] Function SetStatuses() Global Native
; bool fields: enabled, unique, hasprice, src0 (Radiant Raiment), src1 (caravans), src2 (Thieves Guild)
Bool Function GetBool(Int aiSet, String asField) Global Native
String Function SetBool(Int aiSet, String asField, Bool abValue) Global Native
; int fields: rarity, price, reqtype, reqvalue, reqskill, reqfaction; read-only: maxrank, pieces
Int Function GetInt(Int aiSet, String asField) Global Native
String Function SetInt(Int aiSet, String asField, Int aiValue) Global Native
; text fields: name, reqref; read-only: reqrefcheck, warn0..warn2, pieces, file
String Function GetText(Int aiSet, String asField) Global Native
String Function SetText(Int aiSet, String asField, String asValue) Global Native
; lists: rarity, reqtype, skills, factions
String[] Function Choices(String asList) Global Native
Function ReloadFiles() Global Native
