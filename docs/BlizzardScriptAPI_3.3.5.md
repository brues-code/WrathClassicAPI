# Blizzard Script API — `C:\WoW\ChromieCraft_3.3.5a\Wow.exe` (3.3.5a / WotLK)

Catalogue of every Lua function and frame-method exposed to addons in WotLK
3.3.5a, recovered the same way as the vanilla doc — by walking the engine's
own registration tables.

ImageBase `0x00400000`. Sections (VA / file):
- `.text`  VA `0x00401000` file `0x000400`  size `0x5DD3B3`
- `.rdata` VA `0x009DF000` file `0x5DD800`  size `0x0D6679`
- `.data`  VA `0x00AB6000` file `0x6B4000`  size `0x31A508`

## How Blizzard wires Lua

Engine moved to **Lua 5.1** for WotLK (`LUA_GLOBALSINDEX = -10002 = 0xFFFFD8EE`,
visible inside the registrar's `lua_setfield` call).

### 1. Global functions — `FrameScript_RegisterFunction` at `0x00817F90`

Calling convention is **cdecl(name, func)** (vanilla used fastcall). The body
pushes the C closure, then `lua_setfield(L, LUA_GLOBALSINDEX, name)`.

Canonical loop pattern at every batch site (73 total):

```asm
56                  push esi
33 F6               xor  esi, esi
.loop:
8B 86 <T+4>         mov  eax, [esi + table+4]      ; func ptr
8B 8E <T+0>         mov  ecx, [esi + table+0]      ; name ptr
50                  push eax
51                  push ecx
E8 <reg>            call FrameScript_RegisterFunction
83 C6 08            add  esi, 8
83 C4 08            add  esp, 8                    ; cdecl cleanup
[81 FE / 83 FE] N   cmp  esi, count*8
72 <-back>          jb   .loop
```

A few sites use minor variants — register pair `edx/eax` instead of
`ecx/eax` (e.g. the 169-entry `UnitExists` table at `0x0060A1A1`), and one
singleton site (`PlayDance` at `0x0057582D`, loaded from `[0x00ACE834]` /
`[0x00ACE838]` rather than via `esi`). Same effect.

### 2. Frame methods — iterator at `0x008167E0`

Calling convention: **cdecl(context, table, count)**. The iterator walks
`{name, func}` pairs and registers each in the per-frame-type method context.
39 call sites, **34 unique tables** — some tables (notably the 31-method
Animation base) are registered from multiple init paths.

```asm
6A <count>          push count                     ; or 68 imm32 for big counts
68 <table>          push table VA
56                  push esi                       ; context
E8 <iter>           call 0x008167E0
83 C4 0C            add  esp, 12
```

## Scope

- **Glue**: 4 tables / 168 functions, registered on the login screen.
- **In-game**: 67 tables / 2022 functions plus the `PlayDance` singleton.
- **Frame methods**: 34 tables / 679 methods.

Total: **2870 entries across 106 tables**.

## Glue (login screen) global functions

Available only before `EnterWorld`. Listed for completeness.

### Glue: main (login screen) `[0x00AC3E00]` — 113
```
IsShiftKeyDown  GetBuildInfo  GetLocale  GetSavedAccountName
SetSavedAccountName  GetUsesToken  SetUsesToken  GetSavedAccountList
SetSavedAccountList  SetCurrentScreen  QuitGame  QuitGameAndRunLauncher
PlayGlueMusic  PlayCreditsMusic  StopGlueMusic  GetMovieResolution
GetScreenWidth  GetScreenHeight  LaunchURL  ShowTOSNotice  TOSAccepted
AcceptTOS  ShowEULANotice  EULAAccepted  AcceptEULA
ShowTerminationWithoutNoticeNotice  TerminationWithoutNoticeAccepted
AcceptTerminationWithoutNotice  ShowScanningNotice  ScanningAccepted
AcceptScanning  ShowContestNotice  ContestAccepted  AcceptContest
DefaultServerLogin  StatusDialogClick  GetServerName  DisconnectFromServer
IsConnectedToServer  EnterWorld  Screenshot  PatchDownloadProgress
PatchDownloadCancel  PatchDownloadApply  GetNumAddOns  GetAddOnInfo
LaunchAddOnURL  GetAddOnDependencies  GetAddOnEnableState  EnableAddOn
EnableAllAddOns  DisableAddOn  DisableAllAddOns  SaveAddOns  ResetAddOns
IsAddonVersionCheckEnabled  SetAddonVersionCheck  GetCursorPosition
ShowCursor  HideCursor  GetBillingTimeRemaining  GetBillingPlan
GetBillingTimeRested  SurveyNotificationDone  PINEntered  PlayGlueAmbience
StopGlueAmbience  GetCreditsText  GetAccountExpansionLevel
GetClientExpansionLevel  MatrixEntered  MatrixRevert  MatrixCommit
GetMatrixCoordinates  ScanDLLStart  ScanDLLContinueAnyway  IsScanDLLFinished
IsWindowsClient  IsMacClient  IsLinuxClient  SetRealmSplitState
RequestRealmSplitInfo  CancelLogin  GetCVar  GetCVarBool  SetCVar
GetCVarDefault  GetCVarMin  GetCVarMax  GetCVarAbsoluteMin
GetCVarAbsoluteMax  GetChangedOptionWarnings  AcceptChangedOptionWarnings
ShowChangedOptionWarnings  TokenEntered  GetNumDeclensionSets  DeclineName
GetNumGameAccounts  GetGameAccountInfo  SetGameAccount  StopAllSFX
SetClearConfigData  RestartGx  RestoreVideoResolutionDefaults
RestoreVideoEffectsDefaults  RestoreVideoStereoDefaults  IsStreamingMode
IsStreamingTrial  IsConsoleActive  RunScript  ReadyForAccountDataTimes
IsTrialAccount  IsSystemSupported
```

### Glue: realm list `[0x00AC4190]` — 14
```
RequestRealmList  RealmListUpdateRate  CancelRealmListQuery  GetNumRealms
GetRealmInfo  ChangeRealm  GetRealmCategories  SetPreferredInfo  SortRealms
GetSelectedCategory  RealmListDialogCancelled
IsInvalidTournamentRealmCategory  IsTournamentRealmCategory  IsInvalidLocale
```

### Glue: character creation `[0x00AC4228]` — 32
```
SetCharCustomizeFrame  SetCharCustomizeBackground  ResetCharCustomize
GetNameForRace  GetFactionForRace  GetAvailableRaces  GetAvailableClasses
GetClassesForRace  GetHairCustomization  GetFacialHairCustomization
GetSelectedRace  GetSelectedSex  GetSelectedClass  SetSelectedRace
SetSelectedSex  SetSelectedClass  UpdateCustomizationBackground
UpdateCustomizationScene  CycleCharCustomization  RandomizeCharCustomization
GetCharacterCreateFacing  SetCharacterCreateFacing  GetRandomName
CreateCharacter  CustomizeExistingCharacter  PaidChange_GetPreviousRaceIndex
PaidChange_GetCurrentRaceIndex  PaidChange_GetCurrentClassIndex
PaidChange_GetName  IsRaceClassValid  IsRaceClassRestricted
GetCreateBackgroundModel
```

### Glue: character select `[0x00AC4378]` — 13
```
SetCharSelectModelFrame  SetCharSelectBackground  GetCharacterListUpdate
GetNumCharacters  GetCharacterInfo  SelectCharacter  DeleteCharacter
RenameCharacter  DeclineCharacter  UpdateSelectionCustomizationScene
GetCharacterSelectFacing  SetCharacterSelectFacing  GetSelectBackgroundModel
```

## In-game global functions

Categorised by function set; the table VA shows where each batch lives in `.data`.

### Chat & channels `[0x00AC7A58]` — 89
```
SendChatMessage  SendAddonMessage  SendSystemMessage  GetNumLanguages
GetLanguageByIndex  GetDefaultLanguage  DoEmote  LoggingChat  LoggingCombat
JoinChannelByName  JoinTemporaryChannel  JoinPermanentChannel
LeaveChannelByName  ListChannelByName  ListChannels  GetChannelList
SetChannelPassword  SetChannelOwner  DisplayChannelOwner  GetChannelName
ChannelModerator  ChannelUnmoderator  ChannelMute  ChannelUnmute
ChannelInvite  ChannelKick  ChannelBan  ChannelUnban
ChannelToggleAnnouncements  ChannelSilenceVoice  ChannelSilenceAll
ChannelUnSilenceVoice  ChannelUnSilenceAll  ChangeChatColor  ResetChatColors
GetChatTypeIndex  GetChatWindowInfo  GetChatWindowSavedPosition
GetChatWindowSavedDimensions  GetChatWindowMessages  GetChatWindowChannels
AddChatWindowMessages  RemoveChatWindowMessages  AddChatWindowChannel
RemoveChatWindowChannel  SetChatWindowName  SetChatWindowSize
SetChatWindowColor  SetChatWindowAlpha  SetChatWindowLocked
SetChatWindowDocked  SetChatWindowUninteractable  SetChatWindowShown
SetChatWindowSavedPosition  SetChatWindowSavedDimensions
EnumerateServerChannels  RequestRaidInfo  GetNumSavedInstances
GetSavedInstanceInfo  SetSavedInstanceExtend  ResetChatWindows
CanComplainChat  ComplainChat  GetNumDisplayChannels  GetChannelDisplayInfo
GetSelectedDisplayChannel  SetSelectedDisplayChannel  GetChannelRosterInfo
GetNumChannelMembers  SetActiveVoiceChannel  GetActiveVoiceChannel
CollapseChannelHeader  ExpandChannelHeader  ChannelVoiceOn  ChannelVoiceOff
DisplayChannelVoiceOn  DisplayChannelVoiceOff  IsDisplayChannelOwner
IsDisplayChannelModerator  IsVoiceChatEnabled  IsVoiceChatAllowed
IsVoiceChatAllowedByServer  IsSilenced  GetMuteStatus  UnitIsSilenced
SetChannelWatch  ClearChannelWatch  DeclineInvite  SetChatColorNameByClass
```

### Main misc (FrameXML core) `[0x00AC80B0]` — 310
```
FrameXML_Debug  GetBuildInfo  ReloadUI  RegisterForSave
RegisterForSavePerCharacter  SetLayoutMode  IsModifierKeyDown
IsLeftShiftKeyDown  IsRightShiftKeyDown  IsShiftKeyDown
IsLeftControlKeyDown  IsRightControlKeyDown  IsControlKeyDown
IsLeftAltKeyDown  IsRightAltKeyDown  IsAltKeyDown  IsMouseButtonDown
GetMouseButtonName  GetMouseButtonClicked  SetConsoleKey  Screenshot
GetFramerate  TogglePerformanceDisplay  TogglePerformancePause
TogglePerformanceValues  ResetPerformanceValues  GetDebugStats  IsDebugBuild
RegisterCVar  GetCVarInfo  SetCVar  GetCVar  GetCVarBool  GetCVarDefault
GetCVarMin  GetCVarMax  GetCVarAbsoluteMin  GetCVarAbsoluteMax
GetWaterDetail  SetWaterDetail  GetFarclip  SetFarclip  GetTexLodBias
SetTexLodBias  SetBaseMip  GetBaseMip  ToggleTris  TogglePortals
ToggleCollision  ToggleCollisionDisplay  TogglePlayerBounds  Stuck  Logout
Quit  SetCursor  ResetCursor  ClearCursor  CursorHasItem  CursorHasSpell
CursorHasMacro  CursorHasMoney  GetCursorInfo  EquipCursorItem
DeleteCursorItem  EquipPendingItem  CancelPendingEquip  TargetUnit
TargetNearest  TargetNearestEnemy  TargetNearestEnemyPlayer
TargetNearestFriend  TargetNearestFriendPlayer  TargetNearestPartyMember
TargetNearestRaidMember  TargetDirectionEnemy  TargetDirectionFriend
TargetDirectionFinished  TargetLastTarget  TargetLastEnemy  TargetLastFriend
AttackTarget  AssistUnit  FocusUnit  FollowUnit  InteractUnit  ClearTarget
ClearFocus  AutoEquipCursorItem  ToggleSheath  GetZoneText  GetRealZoneText
GetSubZoneText  GetMinimapZoneText  InitiateTrade  CanInspect  NotifyInspect
InviteUnit  UninviteUnit  RequestTimePlayed  RepopMe  AcceptResurrect
DeclineResurrect  ResurrectGetOfferer  ResurrectHasSickness
ResurrectHasTimer  BeginTrade  CancelTrade  AcceptGroup  DeclineGroup
AcceptGuild  DeclineGuild  AcceptArenaTeam  DeclineArenaTeam  CancelLogout
ForceLogout  ForceQuit  GetCursorMoney  DropCursorMoney  PickupPlayerMoney
HasSoulstone  UseSoulstone  HasKey  GuildInvite  GuildUninvite  GuildPromote
GuildDemote  GuildSetLeader  GuildSetMOTD  GuildLeave  GuildDisband
GuildInfo  ArenaTeamInviteByName  ArenaTeamLeave  ArenaTeamUninviteByName
ArenaTeamSetLeaderByName  ArenaTeamDisband  GetScreenWidth  GetScreenHeight
GetDamageBonusStat  GetReleaseTimeRemaining  GetCorpseRecoveryDelay
GetInstanceBootTimeRemaining  GetInstanceLockTimeRemaining
GetInstanceLockTimeRemainingEncounter  GetSummonConfirmTimeLeft
GetSummonConfirmSummoner  GetSummonConfirmAreaName  ConfirmSummon
CancelSummon  GetCursorPosition  GetNetStats  SitStandOrDescendStart
StopCinematic  RunScript  CheckInteractDistance  RandomRoll
OpeningCinematic  InCinematic  IsWindowsClient  IsMacClient  IsLinuxClient
AcceptXPLoss  CheckSpiritHealerDist  CheckTalentMasterDist  CheckBinderDist
RetrieveCorpse  BindEnchant  ReplaceEnchant  ReplaceTradeEnchant
NotWhileDeadError  GetRestState  GetXPExhaustion  GetTimeToWellRested
GMRequestPlayerInfo  GetCoinIcon  GetCoinText  GetCoinTextureString
IsSubZonePVPPOI  GetZonePVPInfo  TogglePVP  SetPVP  GetPVPDesired
GetPVPTimer  IsPVPTimerRunning  ConfirmBindOnUse  SetPortraitToTexture
GetLocale  GetGMTicketCategories  DropItemOnUnit  RestartGx
RestoreVideoResolutionDefaults  RestoreVideoEffectsDefaults
RestoreVideoStereoDefaults  GetBindLocation  ConfirmTalentWipe
ConfirmBinder  ShowingHelm  ShowingCloak  ShowHelm  ShowCloak
SetEuropeanNumbers  GetAreaSpiritHealerTime  AcceptAreaSpiritHeal
CancelAreaSpiritHeal  GetMouseFocus  GetRealmName  GetItemQualityColor
GetItemInfo  GetItemGem  GetExtendedItemInfo  GetItemIcon  GetItemFamily
GetItemCount  GetItemSpell  GetItemCooldown  PickupItem  IsCurrentItem
IsUsableItem  IsHelpfulItem  IsHarmfulItem  IsConsumableItem
IsEquippableItem  IsEquippedItem  IsEquippedItemType  IsDressableItem
ItemHasRange  IsItemInRange  GetNumAddOns  GetAddOnInfo  GetAddOnMetadata
UpdateAddOnMemoryUsage  GetAddOnMemoryUsage  GetScriptCPUUsage
UpdateAddOnCPUUsage  GetAddOnCPUUsage  GetFunctionCPUUsage  GetFrameCPUUsage
GetEventCPUUsage  ResetCPUUsage  GetAddOnDependencies  EnableAddOn
EnableAllAddOns  DisableAddOn  DisableAllAddOns  ResetDisabledAddOns
IsAddOnLoadOnDemand  IsAddOnLoaded  LoadAddOn  PartialPlayTime  NoPlayTime
GetBillingTimeRested  CanShowResetInstances  ResetInstances  IsInInstance
GetInstanceDifficulty  GetInstanceInfo  GetDungeonDifficulty
SetDungeonDifficulty  GetRaidDifficulty  SetRaidDifficulty  ReportBug
ReportSuggestion  GetMirrorTimerInfo  GetMirrorTimerProgress  GetNumTitles
GetCurrentTitle  SetCurrentTitle  IsTitleKnown  GetTitleName  UseItemByName
EquipItemByName  GetExistingLocales  InCombatLockdown  StartAttack
StopAttack  SetTaxiBenchmarkMode  GetTaxiBenchmarkMode  Dismount
VoicePushToTalkStart  VoicePushToTalkStop  SetUIVisibility
IsReferAFriendLinked  CanGrantLevel  GrantLevel  CanSummonFriend
SummonFriend  GetSummonFriendCooldown  GetTotemInfo  GetTotemTimeLeft
TargetTotem  DestroyTotem  GetNumDeclensionSets  DeclineName
AcceptLevelGrant  DeclineLevelGrant  UploadSettings  DownloadSettings
GetMovieResolution  GameMovieFinished  IsDesaturateSupported
GetThreatStatusColor  IsThreatWarningEnabled  ConsoleAddMessage
GetItemUniqueness  EndRefund  EndBoundTradeable  CanMapChangeDifficulty
GetExpansionLevel  GetAllowLowLevelRaid  SetAllowLowLevelRaid
```

### Party / group `[0x00ACC6D0]` — 22
```
GetNumPartyMembers  GetRealNumPartyMembers  GetPartyMember
GetPartyLeaderIndex  IsPartyLeader  IsRealPartyLeader  LeaveParty
GetLootMethod  SetLootMethod  GetLootThreshold  SetLootThreshold
SetPartyAssignment  ClearPartyAssignment  GetPartyAssignment  SilenceMember
UnSilenceMember  SetOptOutOfLoot  GetOptOutOfLoot  CanChangePlayerDifficulty
ChangePlayerDifficulty  IsPartyLFG  HasLFGRestrictions
```

### Barber Shop `[0x00ACC790]` — 9
```
GetBarberShopStyleInfo  SetNextBarberShopStyle  GetBarberShopTotalCost
ApplyBarberShopStyle  CancelBarberShop  GetHairCustomization
GetFacialHairCustomization  BarberShopReset  CanAlterSkin
```

### Tutorials `[0x00ACC868]` — 8
```
CanResetTutorials  FlagTutorial  IsTutorialFlagged  TriggerTutorial
ClearTutorials  ResetTutorials  GetNextCompleatedTutorial
GetPrevCompleatedTutorial
```

### Battle.net `[0x00ACCAE8]` — 57
```
BNGetInfo  BNGetNumFriends  BNGetFriendInfo  BNGetFriendInfoByID
BNGetNumFriendToons  BNGetFriendToonInfo  BNGetToonInfo  BNRemoveFriend
BNSetFriendNote  BNSetSelectedFriend  BNGetSelectedFriend
BNGetNumFriendInvites  BNGetFriendInviteInfo  BNSendFriendInvite
BNSendFriendInviteByID  BNAcceptFriendInvite  BNDeclineFriendInvite
BNReportFriendInvite  BNSetAFK  BNSetDND  BNSetCustomMessage
BNGetCustomMessageTable  BNSetFocus  BNSendWhisper  BNCreateConversation
BNInviteToConversation  BNLeaveConversation  BNSendConversationMessage
BNGetNumConversationMembers  BNGetConversationMemberInfo
BNGetConversationInfo  BNListConversation  BNGetNumBlocked  BNGetBlockedInfo
BNIsBlocked  BNSetBlocked  BNSetSelectedBlock  BNGetSelectedBlock
BNGetNumBlockedToons  BNGetBlockedToonInfo  BNIsToonBlocked
BNSetToonBlocked  BNSetSelectedToonBlock  BNGetSelectedToonBlock
BNReportPlayer  BNConnected  BNFeaturesEnabledAndConnected  IsBNLogin
BNFeaturesEnabled  BNRequestFOFInfo  BNGetNumFOF  BNGetFOFInfo
BNSetMatureLanguageFilter  BNGetMatureLanguageFilter  BNIsSelf  BNIsFriend
BNGetMaxPlayersInConversation
```

### Spells / spellbook `[0x00ACCCE0]` — 45
```
GetNumSpellTabs  GetSpellTabInfo  GetSpellName  GetSpellLink  GetSpellInfo
GetSpellTexture  GetSpellCount  GetSpellCooldown  GetSpellAutocast
ToggleSpellAutocast  EnableSpellAutocast  DisableSpellAutocast  PickupSpell
CastSpell  IsSelectedSpell  IsPassiveSpell  IsAttackSpell  IsCurrentSpell
IsAutoRepeatSpell  IsUsableSpell  IsHelpfulSpell  IsHarmfulSpell
IsConsumableSpell  SpellHasRange  IsSpellInRange  UpdateSpells  HasPetSpells
GetNumShapeshiftForms  GetShapeshiftForm  CancelShapeshiftForm
GetShapeshiftFormInfo  CastShapeshiftForm  GetShapeshiftFormCooldown
CastSpellByName  CastSpellByID  GetNumCompanions  GetCompanionInfo
GetCompanionCooldown  PickupCompanion  CallCompanion  DismissCompanion
GetKnownSlotFromHighestRankSlot  IsSpellKnown  FindSpellBookSlotByID
SummonRandomCritter
```

### World map `[0x00ACCF18]` — 40
```
GetMapContinents  GetMapZones  SetMapZoom  ZoomOut  SetDungeonMapLevel
GetNumDungeonMapLevels  DungeonUsesTerrainMap  SetMapToCurrentZone
GetMapInfo  GetCurrentMapContinent  GetCurrentMapAreaID  GetCurrentMapZone
GetCurrentMapDungeonLevel  SetMapByID  IsZoomOutAvailable  ProcessMapClick
UpdateMapHighlight  GetPlayerMapPosition  GetCorpseMapPosition
GetDeathReleasePosition  GetNumMapLandmarks  GetMapLandmarkInfo
GetNumMapOverlays  GetMapOverlayInfo  CreateWorldMapArrowFrame
InitWorldMapPing  CreateMiniWorldMapArrowFrame  UpdateWorldMapArrowFrames
PositionWorldMapArrowFrame  PositionMiniWorldMapArrowFrame
ShowWorldMapArrowFrame  ShowMiniWorldMapArrowFrame  ClickLandmark
GetNumMapDebugObjects  GetMapDebugObjectInfo  TeleportToDebugObject
HasDebugZoneMap  GetDebugZoneMap  GetWintergraspWaitTime
CanQueueForWintergrasp
```

### World state UI `[0x00ACD0D0]` — 2
```
GetNumWorldStateUI  GetWorldStateUIInfo
```

### Battlefields / PvP queue `[0x00ACD178]` — 51
```
GetNumBattlefields  GetBattlefieldInfo  GetBattlefieldInstanceInfo
IsBattlefieldArena  IsActiveBattlefieldArena  JoinBattlefield
SetSelectedBattlefield  GetSelectedBattlefield  AcceptBattlefieldPort
GetBattlefieldStatus  GetBattlefieldPortExpiration
GetBattlefieldInstanceExpiration  GetBattlefieldInstanceRunTime
GetBattlefieldEstimatedWaitTime  GetBattlefieldTimeWaited  CloseBattlefield
RequestBattlefieldScoreData  GetNumBattlefieldScores  GetBattlefieldScore
GetBattlefieldWinner  SetBattlefieldScoreFaction  LeaveBattlefield
GetNumBattlefieldStats  GetBattlefieldStatInfo  GetBattlefieldStatData
RequestBattlefieldPositions  GetNumBattlefieldPositions
GetBattlefieldPosition  GetNumBattlefieldFlagPositions
GetBattlefieldFlagPosition  GetNumBattlefieldVehicles
GetBattlefieldVehicleInfo  CanJoinBattlefieldAsGroup
GetBattlefieldMapIconScale  GetBattlefieldTeamInfo
GetBattlefieldArenaFaction  SortBattlefieldScoreData
HearthAndResurrectFromArea  CanHearthAndResurrectFromArea
GetNumBattlegroundTypes  GetBattlegroundInfo
RequestBattlegroundInstanceInfo  GetNumArenaOpponents
BattlefieldMgrEntryInviteResponse  BattlefieldMgrQueueRequest
BattlefieldMgrQueueInviteResponse  BattlefieldMgrExitRequest
GetWorldPVPQueueStatus  GetHolidayBGHonorCurrencyBonuses
GetRandomBGHonorCurrencyBonuses  SortBGList
```

### Video / screen resolution `[0x00ACD320]` — 15
```
GetScreenResolutions  GetCurrentResolution  SetScreenResolution
GetRefreshRates  SetupFullscreenScale  GetMultisampleFormats
GetCurrentMultisampleFormat  SetMultisampleFormat  GetVideoCaps  GetGamma
SetGamma  GetTerrainMip  SetTerrainMip  IsStereoVideoAvailable
IsPlayerResolutionAvailable
```

### Account messages `[0x00ACD418]` — 11
```
AccountMsg_LoadHeaders  AccountMsg_GetNumTotalMsgs
AccountMsg_GetNumUnreadMsgs  AccountMsg_GetNumUnreadUrgentMsgs
AccountMsg_GetIndexHighestPriorityUnreadMsg
AccountMsg_GetIndexNextUnreadMsg  AccountMsg_GetHeaderSubject
AccountMsg_GetHeaderPriority  AccountMsg_LoadBody  AccountMsg_GetBody
AccountMsg_SetMsgRead
```

### Knowledge Base `[0x00ACD488]` — 22
```
KBSetup_BeginLoading  KBSetup_IsLoaded  KBSetup_GetLanguageCount
KBSetup_GetLanguageData  KBSetup_GetCategoryCount  KBSetup_GetCategoryData
KBSetup_GetSubCategoryCount  KBSetup_GetSubCategoryData
KBSetup_GetArticleHeaderCount  KBSetup_GetArticleHeaderData
KBSetup_GetTotalArticleCount  KBQuery_BeginLoading  KBQuery_IsLoaded
KBQuery_GetArticleHeaderCount  KBQuery_GetArticleHeaderData
KBQuery_GetTotalArticleCount  KBArticle_BeginLoading  KBArticle_IsLoaded
KBArticle_GetData  KBSystem_GetMOTD  KBSystem_GetServerStatus
KBSystem_GetServerNotice
```

### LFG / Dungeon Finder `[0x00ACD668]` — 67
```
SetLFGDungeon  ClearLFGDungeon  ClearAllLFGDungeons  GetLFGInfoLocal
GetLFGInfoServer  GetLFGQueuedList  SetLFGComment  LFGTeleport  JoinLFG
LeaveLFG  RefreshLFGList  SearchLFGJoin  SearchLFGGetJoinedID
SearchLFGLeave  SearchLFGGetNumResults  SearchLFGGetResults
SearchLFGGetEncounterResults  SearchLFGGetPartyResults  SearchLFGSort
GetLFGTypes  GetLFGRoleUpdate  GetLFGRoleUpdateSlot  GetLFGRoleUpdateMember
GetLFGRoles  SetLFGRoles  CompleteLFGRoleCheck  GetLFGProposal
GetLFGProposalMember  GetLFGProposalEncounter  AcceptProposal
RejectProposal  GetLFGBootProposal  SetLFGBootVote  GetLFGQueueStats
GetLastQueueStatusIndex  GetLFGDungeonInfo  GetLFGRandomDungeonInfo
IsLFGDungeonJoinable  SetLFGHeaderCollapsed  SetLFGDungeonEnabled
GetLFGCompletionReward  GetLFGCompletionRewardItem  IsInLFGDungeon
CanPartyLFGBackfill  GetPartyLFGBackfillInfo  PartyLFGStartBackfill
GetLFGRandomCooldownExpiration  UnitHasLFGRandomCooldown
GetLFGDeserterExpiration  UnitHasLFGDeserter  GetAvailableRoles
GetLFDChoiceOrder  GetLFDChoiceInfo  GetLFDChoiceCollapseState
GetLFDChoiceEnabledState  GetLFDChoiceLockedState  GetLFDLockPlayerCount
GetLFDLockInfo  RequestLFDPlayerLockInfo  RequestLFDPartyLockInfo
GetNumRandomDungeons  GetLFGDungeonRewards  GetLFGDungeonRewardInfo
GetLFGDungeonRewardLink  GetRandomDungeonBestChoice  GetLFRChoiceOrder
IsListedInLFR
```

### Key bindings `[0x00ACDE38]` — 26
```
GetNumBindings  GetBinding  SetBinding  SetBindingSpell  SetBindingItem
SetBindingMacro  SetBindingClick  SetOverrideBinding
SetOverrideBindingSpell  SetOverrideBindingItem  SetOverrideBindingMacro
SetOverrideBindingClick  ClearOverrideBindings  GetBindingKey
GetBindingAction  GetBindingByKey  RunBinding  GetCurrentBindingSet
LoadBindings  SaveBindings  GetNumModifiedClickActions
GetModifiedClickAction  SetModifiedClick  GetModifiedClick  IsModifiedClick
GetClickFrame
```

### Secure templates / SecureCmd `[0x00ACE1F0]` — 22
```
SecureCmdOptionParse  RunMacro  RunMacroText  StopMacro  CreateMacro
GetNumMacros  GetMacroInfo  GetMacroBody  DeleteMacro  EditMacro
SetMacroItem  GetMacroItem  SetMacroSpell  GetMacroSpell  GetNumMacroIcons
GetNumMacroItemIcons  GetMacroIconInfo  GetMacroItemIconInfo  PickupMacro
GetMacroIndexByName  GetRunningMacro  GetRunningMacroButton
```

### Commentator / spectator `[0x00ACE370]` — 35
```
CommentatorSetMode  CommentatorToggleMode  CommentatorGetMode
CommentatorSetMapAndInstanceIndex  CommentatorSetPlayerIndex
CommentatorUpdatePlayerInfo  CommentatorUpdateMapInfo  CommentatorGetNumMaps
CommentatorGetMapInfo  CommentatorGetInstanceInfo  CommentatorEnterInstance
CommentatorExitInstance  CommentatorGetNumPlayers  CommentatorGetPlayerInfo
CommentatorFollowPlayer  CommentatorLookatPlayer  CommentatorZoomIn
CommentatorZoomOut  CommentatorSetCamera  CommentatorGetCamera
CommentatorGetCurrentMapID  CommentatorStartInstance  CommentatorAddPlayer
CommentatorRemovePlayer  CommentatorSetBattlemaster  CommentatorSetMoveSpeed
CommentatorSetCameraCollision  CommentatorSetTargetHeightOffset
CommentatorSetSkirmishMatchmakingMode  CommentatorRequestSkirmishQueueData
CommentatorGetSkirmishQueueCount  CommentatorGetSkirmishQueuePlayerInfo
CommentatorStartSkirmishMatch  CommentatorRequestSkirmishMode
CommentatorGetSkirmishMode
```

### Mail `[0x00ACE610]` — 38
```
CloseMail  ClearSendMail  ClickSendMailItemButton  SetSendMailMoney
GetSendMailMoney  SetSendMailCOD  GetSendMailCOD  GetNumStationeries
GetStationeryInfo  SelectStationery  GetSelectedStationeryTexture
GetNumPackages  GetPackageInfo  SelectPackage  GetSendMailItem
GetSendMailItemLink  GetSendMailPrice  SendMail  CheckInbox
GetInboxNumItems  GetInboxHeaderInfo  GetInboxText  GetInboxInvoiceInfo
GetInboxItem  GetInboxItemLink  TakeInboxMoney  TakeInboxItem
TakeInboxTextItem  ReturnInboxItem  DeleteInboxItem  InboxItemCanDelete
HasNewMail  ComplainInboxItem  CanComplainInboxItem  GetLatestThreeSenders
SetSendMailShowing  AutoLootMailItem  RespondMailLockSendItem
```

### Raid `[0x00ACE790]` — 20
```
GetNumRaidMembers  GetRealNumRaidMembers  GetRaidRosterInfo
SetRaidRosterSelection  GetRaidRosterSelection  IsRaidLeader
IsRealRaidLeader  IsRaidOfficer  SetRaidSubgroup  SwapRaidSubgroup
ConvertToRaid  PromoteToLeader  PromoteToAssistant  DemoteAssistant
SetRaidTarget  GetRaidTargetIndex  DoReadyCheck  ConfirmReadyCheck
GetReadyCheckTimeLeft  GetReadyCheckStatus
```

### Singleton: PlayDance `[0x00ACE834]` — 1
```
PlayDance
```

### Auto-complete `[0x00ACEB50]` — 2
```
GetAutoCompleteResults  GetAutoCompletePresenceID
```

### Bank `[0x00ACEB7C]` — 5
```
BankButtonIDToInvSlotID  GetNumBankSlots  GetBankSlotCost  PurchaseSlot
CloseBankFrame
```

### Companion / mount system `[0x00ACEC30]` — 4
```
GetNumTrackingTypes  GetTrackingInfo  SetTracking  GetTrackingTexture
```

### Merchant `[0x00ACED00]` — 21
```
CloseMerchant  GetMerchantNumItems  GetMerchantItemInfo
GetMerchantItemCostInfo  GetMerchantItemCostItem  GetBuybackItemInfo
GetBuybackItemLink  GetMerchantItemLink  GetMerchantItemMaxStack
PickupMerchantItem  BuyMerchantItem  BuybackItem  CanMerchantRepair
ShowMerchantSellCursor  ShowBuybackSellCursor  ShowRepairCursor
HideRepairCursor  InRepairMode  GetRepairAllCost  RepairAllItems
GetNumBuybackItems
```

### Trade window `[0x00ACEDB0]` — 14
```
CloseTrade  ClickTradeButton  ClickTargetTradeButton  GetTradeTargetItemInfo
GetTradeTargetItemLink  GetTradePlayerItemInfo  GetTradePlayerItemLink
AcceptTrade  CancelTradeAccept  GetPlayerTradeMoney  GetTargetTradeMoney
PickupTradeMoney  AddTradeMoney  SetTradeMoney
```

### Loot `[0x00ACEE28]` — 17
```
SetLootPortrait  GetNumLootItems  GetLootSlotInfo  GetLootSlotLink
LootSlotIsItem  LootSlotIsCoin  LootSlot  ConfirmLootSlot  CloseLoot
IsFishingLoot  GetMasterLootCandidate  GiveMasterLoot  GetLootRollItemInfo
GetLootRollItemLink  GetLootRollTimeLeft  RollOnLoot  ConfirmLootRoll
```

### Item text (books) `[0x00ACEEC0]` — 9
```
ItemTextGetItem  ItemTextGetCreator  ItemTextGetMaterial  ItemTextGetPage
ItemTextGetText  ItemTextHasNextPage  ItemTextPrevPage  ItemTextNextPage
CloseItemText
```

### Gossip `[0x00ACEF78]` — 12
```
GetGossipText  GetNumGossipOptions  GetNumGossipAvailableQuests
GetNumGossipActiveQuests  GetGossipOptions  GetGossipAvailableQuests
GetGossipActiveQuests  SelectGossipOption  SelectGossipAvailableQuest
SelectGossipActiveQuest  CloseGossip  ForceGossip
```

### Action / spell auras (player buffs etc.) `[0x00ACEFD8]` — 47
```
CloseQuest  GetTitleText  GetGreetingText  GetQuestText  GetObjectiveText
GetProgressText  GetRewardText  GetNumAvailableQuests  GetNumActiveQuests
GetAvailableTitle  GetActiveTitle  GetAvailableLevel  GetActiveLevel
IsAvailableQuestTrivial  IsActiveQuestTrivial  SelectAvailableQuest
SelectActiveQuest  AcceptQuest  DeclineQuest  IsQuestCompletable
CompleteQuest  GetQuestReward  GetRewardMoney  GetRewardXP  GetRewardHonor
GetRewardSpell  GetQuestMoneyToGet  GetNumQuestRewards  GetNumQuestChoices
GetNumQuestItems  GetQuestItemInfo  GetQuestItemLink  GetQuestSpellLink
QuestChooseRewardError  ConfirmAcceptQuest  GetQuestBackgroundMaterial
GetSuggestedGroupNum  QuestFlagsPVP  QuestGetAutoAccept
GetDailyQuestsCompleted  GetMaxDailyQuests  GetRewardTitle  GetRewardTalents
GetRewardArenaPoints  GetAvailableQuestInfo  QuestIsDaily  QuestIsWeekly
```

### Taxi `[0x00ACF258]` — 14
```
SetTaxiMap  NumTaxiNodes  TaxiNodeName  TaxiNodePosition  TaxiNodeCost
TakeTaxiNode  CloseTaxiMap  TaxiNodeGetType  TaxiNodeSetCurrent  TaxiGetSrcX
TaxiGetSrcY  TaxiGetDestX  TaxiGetDestY  GetNumRoutes
```

### Trainer `[0x00ACF3E8]` — 28
```
OpenTrainer  CloseTrainer  GetNumTrainerServices  GetTrainerServiceInfo
SelectTrainerService  IsTradeskillTrainer  GetTrainerSelectionIndex
GetTrainerGreetingText  GetTrainerServiceIcon  GetTrainerServiceSkillLine
GetTrainerServiceCost  GetTrainerServiceLevelReq  GetTrainerServiceSkillReq
GetTrainerServiceNumAbilityReq  GetTrainerServiceAbilityReq
GetTrainerServiceStepReq  GetTrainerServiceDescription
IsTrainerServiceSkillStep  GetTrainerServiceStepIncrease  BuyTrainerService
SetTrainerServiceTypeFilter  SetTrainerSkillLineFilter
GetTrainerServiceTypeFilter  GetTrainerSkillLineFilter  GetTrainerSkillLines
CollapseTrainerSkillLine  ExpandTrainerSkillLine  GetTrainerServiceItemLink
```

### Tabard creation `[0x00ACF56C]` — 2
```
CloseTabardCreation  GetTabardCreationCost
```

### Guild registrar `[0x00ACF5FC]` — 5
```
CloseGuildRegistrar  GetGuildCharterCost  BuyGuildCharter
TurnInGuildCharter  GetTabardInfo
```

### Auction house `[0x00ACF638]` — 30
```
CloseAuctionHouse  GetAuctionHouseDepositRate  CalculateAuctionDeposit
ClickAuctionSellItemButton  GetAuctionSellItemInfo  StartAuction
QueryAuctionItems  GetOwnerAuctionItems  GetBidderAuctionItems
GetNumAuctionItems  GetAuctionItemInfo  GetAuctionItemLink
GetAuctionItemTimeLeft  PlaceAuctionBid  GetAuctionItemClasses
GetAuctionItemSubClasses  GetAuctionInvTypes  CanSendAuctionQuery
SortAuctionItems  SetSelectedAuctionItem  GetSelectedAuctionItem
IsAuctionSortReversed  CancelAuction  CanCancelAuction  GetAuctionSort
SortAuctionClearSort  SortAuctionSetSort  SortAuctionApplySort  CancelSell
SetAuctionsTabShowing
```

### Pet stables `[0x00ACF768]` — 14
```
ClosePetStables  StablePet  UnstablePet  BuyStableSlot  GetNumStablePets
GetNumStableSlots  GetStablePetInfo  GetNextStableSlotCost  ClickStablePet
PickupStablePet  GetSelectedStablePet  SetPetStablePaperdoll
GetStablePetFoodTypes  IsAtStableMaster
```

### Petition vendor (charters) `[0x00ACF7F0]` — 8
```
ClosePetitionVendor  GetNumPetitionItems  GetPetitionItemInfo  BuyPetition
ClickPetitionButton  TurnInPetition  TurnInArenaPetition  HasFilledPetition
```

### Arena teams `[0x00ACF838]` — 13
```
GetArenaTeam  GetNumArenaTeamMembers  GetArenaTeamRosterInfo
GetArenaTeamGdfInfo  SetArenaTeamRosterSelection
GetArenaTeamRosterSelection  SortArenaTeamRoster
SetArenaTeamRosterShowOffline  GetArenaTeamRosterShowOffline
CloseArenaTeamRoster  ArenaTeamRoster  GetCurrentArenaSeason
GetPreviousArenaSeason
```

### Guild bank `[0x00ACF8D0]` — 29
```
QueryGuildBankTab  SetCurrentGuildBankTab  GetCurrentGuildBankTab
GetGuildBankItemInfo  SetGuildBankTabInfo  GetGuildBankItemLink
PickupGuildBankItem  AutoStoreGuildBankItem  SplitGuildBankItem
GetNumGuildBankTabs  GetGuildBankTabInfo  GetGuildBankTabCost
BuyGuildBankTab  DepositGuildBankMoney  WithdrawGuildBankMoney
CanWithdrawGuildBankMoney  PickupGuildBankMoney  GetGuildBankMoney
GetGuildBankWithdrawMoney  CloseGuildBankFrame  GetGuildTabardFileNames
QueryGuildBankLog  GetNumGuildBankTransactions  GetGuildBankTransaction
GetNumGuildBankMoneyTransactions  GetGuildBankMoneyTransaction
QueryGuildBankText  GetGuildBankText  SetGuildBankText
```

### Action bars `[0x00ACF9E0]` — 28
```
GetActionInfo  GetActionTexture  GetActionCount  GetActionCooldown
GetActionAutocast  GetActionText  HasAction  UseAction  PickupAction
PlaceAction  IsAttackAction  IsCurrentAction  IsAutoRepeatAction
IsUsableAction  IsConsumableAction  IsStackableAction  IsEquippedAction
ActionHasRange  IsActionInRange  GetBonusBarOffset  GetMultiCastBarOffset
ChangeActionBarPage  GetActionBarPage  GetActionBarToggles
SetActionBarToggles  IsPossessBarVisible  GetMultiCastTotemSpells
SetMultiCastSpell
```

### GM tickets / surveys `[0x00ACFAF8]` — 15
```
GetGMTicket  NewGMTicket  UpdateGMTicket  DeleteGMTicket
GMResponseNeedMoreHelp  GMResponseResolve  GetGMStatus  GMSurveyQuestion
GMSurveyNumAnswers  GMSurveyAnswer  GMSurveyAnswerSubmit
GMSurveyCommentSubmit  GMSurveySubmit  GMReportLag  RegisterStaticConstants
```

### Equipment manager `[0x00ACFB78]` — 17
```
SaveEquipmentSet  DeleteEquipmentSet  RenameEquipmentSet
EquipmentManagerIgnoreSlotForSave  EquipmentManagerIsSlotIgnoredForSave
EquipmentManagerClearIgnoredSlotsForSave
EquipmentManagerUnignoreSlotForSave  GetEquipmentSetLocations
GetEquipmentSetItemIDs  GetNumEquipmentSets  GetEquipmentSetInfo
GetEquipmentSetInfoByName  EquipmentSetContainsLockedItems
PickupEquipmentSetByName  PickupEquipmentSet  UseEquipmentSet
CanUseEquipmentSets
```

### Currency tokens `[0x00ACFC34]` — 6
```
GetCurrencyListSize  GetCurrencyListInfo  ExpandCurrencyList
SetCurrencyUnused  SetCurrencyBackpack  GetBackpackCurrencyInfo
```

### Achievements `[0x00ACFC70]` — 37
```
GetCategoryList  GetStatisticsCategoryList  GetCategoryInfo
GetCategoryNumAchievements  GetComparisonCategoryNumAchievements
GetAchievementInfo  GetAchievementNumRewards  GetAchievementReward
GetAchievementNumCriteria  GetAchievementCriteriaInfo
SetAchievementComparisonUnit  ClearAchievementComparisonUnit
GetAchievementComparisonInfo  GetPreviousAchievement  GetNextAchievement
GetAchievementCategory  GetAchievementLink  GetNumCompletedAchievements
GetNumComparisonCompletedAchievements  GetLatestCompletedAchievements
GetLatestUpdatedStats  GetLatestCompletedComparisonAchievements
GetLatestUpdatedComparisonStats  GetTotalAchievementPoints
GetAchievementInfoFromCriteria  GetStatistic  GetComparisonStatistic
GetComparisonAchievementPoints  CanShowAchievementUI  GetTrackedAchievements
AddTrackedAchievement  RemoveTrackedAchievement  IsTrackedAchievement
GetNumTrackedAchievements  HasCompletedAnyAchievement  QueryQuestsCompleted
GetQuestsCompleted
```

### Glyphs `[0x00ACFF54]` — 6
```
GetNumGlyphSockets  GetGlyphSocketInfo  GlyphMatchesSocket
PlaceGlyphInSocket  RemoveGlyphFromSocket  GetGlyphLink
```

### Calendar `[0x00AD0030]` — 95
```
CalendarGetMonthNames  CalendarGetWeekdayNames  CalendarGetDate
CalendarGetMinDate  CalendarGetMaxDate  CalendarGetMinHistoryDate
CalendarGetMaxCreateDate  CalendarGetMonth  CalendarGetAbsMonth
CalendarSetMonth  CalendarSetAbsMonth  CalendarGetNumDayEvents
CalendarGetDayEvent  CalendarGetDayEventSequenceInfo
CalendarGetFirstPendingInvite  CalendarOpenEvent  CalendarGetEventIndex
CalendarCloseEvent  CalendarGetEventInfo  CalendarGetHolidayInfo
CalendarGetRaidInfo  CalendarGetNumPendingInvites
CalendarEventGetNumInvites  CalendarEventGetInvite
CalendarEventGetInviteResponseTime  CalendarAddEvent  CalendarNewEvent
CalendarMassInviteGuild  CalendarMassInviteArenaTeam
CalendarNewGuildAnnouncement  CalendarNewGuildEvent
CalendarDefaultGuildFilter  CalendarUpdateEvent  CalendarRemoveEvent
CalendarEventSelectInvite  CalendarEventGetSelectedInvite
CalendarContextSelectEvent  CalendarContextDeselectEvent
CalendarContextGetEventIndex  CalendarContextInviteIsPending
CalendarContextInviteModeratorStatus  CalendarContextInviteStatus
CalendarContextInviteType  CalendarContextInviteAvailable
CalendarContextInviteTentative  CalendarContextInviteDecline
CalendarContextInviteRemove  CalendarContextEventSignUp
CalendarContextEventRemove  CalendarContextEventCopy
CalendarContextEventPaste  CalendarContextEventClipboard
CalendarContextEventCanComplain  CalendarContextEventComplain
CalendarContextEventCanEdit  CalendarContextEventGetCalendarType
CalendarEventInvite  CalendarEventRemoveInvite  CalendarEventAvailable
CalendarEventTentative  CalendarEventDecline  CalendarEventSignUp
CalendarEventSortInvites  CalendarEventGetInviteSortCriterion
CalendarEventGetStatusOptions  CalendarEventSetStatus
CalendarEventSetModerator  CalendarEventClearModerator
CalendarEventCanModerate  CalendarEventIsModerator  CalendarEventGetTypes
CalendarEventGetRepeatOptions  CalendarEventSetTitle
CalendarEventSetDescription  CalendarEventSetType
CalendarEventSetRepeatOption  CalendarEventSetSize  CalendarEventSetDate
CalendarEventSetTime  CalendarEventSetLockoutDate
CalendarEventSetLockoutTime  CalendarEventSetTextureID
CalendarEventSetLocked  CalendarEventClearLocked
CalendarEventSetAutoApprove  CalendarEventClearAutoApprove
CalendarEventGetTextures  CalendarEventHasPendingInvite
CalendarEventHaveSettingsChanged  CalendarEventCanEdit
CalendarEventGetCalendarType  CalendarCanSendInvite  CalendarCanAddEvent
CalendarIsActionPending  OpenCalendar
```

### Item socketing (gems) `[0x00AD0568]` — 12
```
CloseSocketInfo  GetSocketItemInfo  GetNumSockets  GetExistingSocketInfo
GetExistingSocketLink  GetNewSocketInfo  GetNewSocketLink  ClickSocketButton
AcceptSockets  GetSocketTypes  GetSocketItemRefundable
GetSocketItemBoundTradeable
```

### Minigame `[0x00AD05D0]` — 3
```
GetMinigameType  MakeMinigameMove  GetMinigameState
```

### Talents `[0x00AD05F0]` — 17
```
GetNumTalentTabs  GetTalentTabInfo  GetNumTalents  GetTalentInfo
GetTalentLink  GetTalentPrereqs  LearnTalent  GetUnspentTalentPoints
GetNumTalentGroups  GetActiveTalentGroup  SetActiveTalentGroup
GetPreviewTalentPointsSpent  GetGroupPreviewTalentPointsSpent
AddPreviewTalentPoints  ResetPreviewTalentPoints
ResetGroupPreviewTalentPoints  LearnPreviewTalents
```

### Guild roster & ranks `[0x00AD0840]` — 43
```
GetNumGuildMembers  GetGuildRosterMOTD  GetGuildRosterInfo
GetGuildRosterLastOnline  GuildRosterSetPublicNote
GuildRosterSetOfficerNote  SetGuildRosterSelection  GetGuildRosterSelection
CanGuildPromote  CanGuildDemote  CanGuildInvite  CanGuildRemove  CanEditMOTD
CanEditPublicNote  CanEditOfficerNote  CanViewOfficerNote  CanEditGuildInfo
CanGuildBankRepair  CanEditGuildTabInfo  CanEditGuildEvent  SortGuildRoster
SetGuildRosterShowOffline  GetGuildRosterShowOffline
GuildControlGetNumRanks  GuildControlGetRankName  GuildControlSetRank
GuildControlGetRankFlags  GuildControlSetRankFlag  GuildControlSaveRank
GuildControlAddRank  GuildControlDelRank  SetGuildBankTabPermissions
GetGuildBankTabPermissions  SetGuildBankWithdrawLimit
GetGuildBankWithdrawLimit  SetGuildBankTabWithdraw  CloseGuildRoster
GuildRoster  GetGuildInfoText  SetGuildInfoText  QueryGuildEventLog
GetNumGuildEvents  GetGuildEventInfo
```

### Skills `[0x00AD09B8]` — 13
```
GetNumSkillLines  GetSkillLineInfo  AbandonSkill  CollapseSkillHeader
ExpandSkillHeader  AddSkillUp  RemoveSkillUp  GetAdjustedSkillPoints
AcceptSkillUps  CancelSkillUps  BuySkillTier  SetSelectedSkill
GetSelectedSkill
```

### Petitions `[0x00AD0A60]` — 8
```
ClosePetition  GetPetitionInfo  GetNumPetitionNames  GetPetitionNameInfo
CanSignPetition  SignPetition  OfferPetition  RenamePetition
```

### Duel `[0x00AD0AC4]` — 3
```
StartDuel  AcceptDuel  CancelDuel
```

### Reputation / factions `[0x00AD0AE8]` — 15
```
GetNumFactions  GetFactionInfo  GetFactionInfoByID  GetWatchedFactionInfo
SetWatchedFactionIndex  FactionToggleAtWar  CollapseFactionHeader
CollapseAllFactionHeaders  SetFactionInactive  SetFactionActive
IsFactionInactive  ExpandFactionHeader  ExpandAllFactionHeaders
SetSelectedFaction  GetSelectedFaction
```

### Pet `[0x00AD0C30]` — 31
```
PetHasActionBar  GetPetActionInfo  GetPetActionCooldown  GetPetActionsUsable
GetPetActionSlotUsable  IsPetAttackActive  PickupPetAction
TogglePetAutocast  CastPetAction  PetPassiveMode  PetDefensiveMode
PetAggressiveMode  PetWait  PetFollow  PetAttack  PetStopAttack  PetAbandon
PetDismiss  PetRename  PetCanBeAbandoned  PetCanBeDismissed  PetCanBeRenamed
GetPetTimeRemaining  HasPetUI  GetPetExperience  GetPetHappiness
GetPetFoodTypes  GetPetIcon  GetPetTalentTree  GetPossessInfo
IsPetAttackAction
```

### Container / bags `[0x00AD0D50]` — 22
```
ContainerIDToInventoryID  GetContainerNumSlots  GetContainerItemInfo
GetContainerItemID  GetContainerItemLink  GetContainerItemCooldown
PickupContainerItem  SplitContainerItem  UseContainerItem
SocketContainerItem  ShowContainerSellCursor  SetBagPortraitTexture
GetBagName  GetContainerItemDurability  GetContainerNumFreeSlots
GetContainerFreeSlots  GetContainerItemPurchaseInfo
GetContainerItemPurchaseItem  ContainerRefundItemPurchase
GetMaxArenaCurrency  GetContainerItemGems  GetContainerItemQuestInfo
```

### Trade skills (professions) `[0x00AD0E80]` — 36
```
CloseTradeSkill  GetNumTradeSkills  GetTradeSkillInfo  SelectTradeSkill
GetTradeSkillSelectionIndex  GetTradeSkillCooldown  GetTradeSkillIcon
GetTradeSkillNumMade  GetTradeSkillLine  GetTradeSkillItemLink
SetTradeSkillItemNameFilter  GetTradeSkillItemNameFilter
SetTradeSkillItemLevelFilter  GetTradeSkillItemLevelFilter
GetTradeSkillNumReagents  GetTradeSkillReagentInfo
GetTradeSkillReagentItemLink  GetTradeSkillTools  GetTradeSkillDescription
GetTradeSkillSubClasses  GetTradeSkillInvSlots  SetTradeSkillSubClassFilter
GetTradeSkillSubClassFilter  SetTradeSkillInvSlotFilter
GetTradeSkillInvSlotFilter  TradeSkillOnlyShowMakeable
TradeSkillOnlyShowSkillUps  CollapseTradeSkillSubClass
ExpandTradeSkillSubClass  GetFirstTradeSkill  GetTradeskillRepeatCount
DoTradeSkill  GetTradeSkillRecipeLink  StopTradeSkillRepeat
GetTradeSkillListLink  IsTradeSkillLinked
```

### Quest log `[0x00AD1020]` — 67
```
GetNumQuestLogEntries  GetQuestLogTitle  SelectQuestLogEntry
GetQuestLogSelection  SetAbandonQuest  GetAbandonQuestName
GetAbandonQuestItems  AbandonQuest  IsUnitOnQuest  GetQuestLogQuestText
GetNumQuestLeaderBoards  GetQuestLogLeaderBoard  GetNumQuestItemDrops
GetQuestLogItemDrop  GetQuestPOILeaderBoard  GetQuestLogTimeLeft
IsCurrentQuestFailed  GetNumQuestLogRewards  GetNumQuestLogChoices
GetQuestLogRewardInfo  GetQuestLogChoiceInfo  GetQuestLogItemLink
GetQuestLogSpellLink  GetQuestLogRewardMoney  GetQuestLogRewardXP
GetQuestLogRewardHonor  GetQuestLogRewardSpell  GetQuestLogRequiredMoney
GetQuestLogPushable  QuestLogPushQuest  GetQuestTimers
GetQuestIndexForTimer  CollapseQuestHeader  ExpandQuestHeader
GetQuestGreenRange  GetNumQuestWatches  IsQuestWatched  AddQuestWatch
RemoveQuestWatch  GetQuestIndexForWatch  GetQuestLogGroupNum
GetQuestResetTime  GetQuestLink  GetQuestLogRewardTitle
GetQuestLogRewardTalents  GetQuestLogRewardArenaPoints
GetQuestLogSpecialItemInfo  GetQuestLogSpecialItemCooldown
IsQuestLogSpecialItemInRange  UseQuestLogSpecialItem
ProcessQuestLogRewardFactions  GetNumQuestLogRewardFactions
GetQuestLogRewardFactionInfo  SortQuestWatches  ShiftQuestWatches
GetQuestWatchIndex  QuestMapUpdateAllQuests  GetQuestSortIndex
GetQuestWorldMapAreaID  QuestPOIUpdateTexture  QuestPOIUpdateIcons
QuestPOIGetIconInfo  QuestPOIGetQuestIDByIndex
QuestPOIGetQuestIDByVisibleIndex  GetQuestLogCompletionText
SetPOIIconOverlapDistance  SetPOIIconOverlapPushDistance
```

### Inventory & player items `[0x00AD1270]` — 33
```
GetInventorySlotInfo  GetInventoryItemsForSlot  GetInventoryItemTexture
GetInventoryItemBroken  GetInventoryItemCount  GetInventoryItemQuality
GetInventoryItemCooldown  GetInventoryItemDurability  GetInventoryItemLink
GetInventoryItemID  GetInventoryItemGems  KeyRingButtonIDToInvSlotID
PickupInventoryItem  UseInventoryItem  SocketInventoryItem
IsInventoryItemLocked  PutItemInBag  PutItemInBackpack  PickupBagFromSlot
CursorCanGoInSlot  ShowInventorySellCursor  SetInventoryPortraitTexture
GetGuildInfo  GetInventoryAlertStatus  UpdateInventoryAlertStatus
OffhandHasWeapon  HasInspectHonorData  RequestInspectHonorData
GetInspectHonorData  GetInspectArenaTeamData  ClearInspectPlayer
GetWeaponEnchantInfo  HasWandEquipped
```

### Movement & mouselook `[0x00AD1938]` — 52
```
JumpOrAscendStart  AscendStop  DescendStop  ToggleRun  ToggleAutoRun
MoveForwardStart  MoveForwardStop  MoveBackwardStart  MoveBackwardStop
TurnLeftStart  TurnLeftStop  TurnRightStart  TurnRightStop  StrafeLeftStart
StrafeLeftStop  StrafeRightStart  StrafeRightStop  PitchUpStart  PitchUpStop
PitchDownStart  PitchDownStop  TurnOrActionStart  TurnOrActionStop
CameraOrSelectOrMoveStart  CameraOrSelectOrMoveStop  MoveAndSteerStart
MoveAndSteerStop  SetMouselookOverrideBinding  MouselookStart  MouselookStop
IsMouselooking  VehicleExit  VehiclePrevSeat  VehicleNextSeat
VehicleAimUpStart  VehicleAimUpStop  VehicleAimDownStart  VehicleAimDownStop
VehicleAimIncrement  VehicleAimDecrement  VehicleAimRequestAngle
VehicleAimGetAngle  VehicleAimRequestNormAngle  VehicleAimGetNormAngle
VehicleAimSetNormPower  VehicleAimGetNormPower  IsUsingVehicleControls
CanExitVehicle  CanSwitchVehicleSeats  IsVehicleAimAngleAdjustable
IsVehicleAimPowerAdjustable  DetectWowMouse
```

### Camera `[0x00AD2060]` — 22
```
CameraZoomIn  CameraZoomOut  MoveViewInStart  MoveViewInStop
MoveViewOutStart  MoveViewOutStop  MoveViewLeftStart  MoveViewLeftStop
MoveViewRightStart  MoveViewRightStop  MoveViewUpStart  MoveViewUpStop
MoveViewDownStart  MoveViewDownStop  SetView  SaveView  ResetView  NextView
PrevView  FlipCameraYaw  VehicleCameraZoomIn  VehicleCameraZoomOut
```

### Time / file I/O `[0x00AD2184]` — 7
```
GetTime  GetGameTime  ConsoleExec  ReadFile  DeleteFile  AppendToFile
GetAccountExpansionLevel
```

### Unit & PvP `[0x00AD21D8]` — 169
```
UnitExists  UnitIsVisible  UnitIsUnit  UnitIsPlayer  UnitIsInMyGuild
UnitIsCorpse  UnitIsPartyLeader  UnitGroupRolesAssigned  UnitIsRaidOfficer
UnitInParty  UnitPlayerOrPetInParty  UnitInRaid  UnitPlayerOrPetInRaid
UnitPlayerControlled  UnitIsAFK  UnitIsDND  UnitIsPVP  UnitIsPVPSanctuary
UnitIsPVPFreeForAll  UnitFactionGroup  UnitReaction  UnitIsEnemy
UnitIsFriend  UnitCanCooperate  UnitCanAssist  UnitCanAttack  UnitIsCharmed
UnitIsPossessed  PlayerCanTeleport  UnitClassification  UnitSelectionColor
UnitGUID  UnitName  UnitPVPName  UnitXP  UnitXPMax  UnitHealth
UnitHealthMax  UnitMana  UnitManaMax  UnitPower  UnitPowerMax  UnitPowerType
UnitOnTaxi  UnitIsFeignDeath  UnitIsDead  UnitIsGhost  UnitIsDeadOrGhost
UnitIsConnected  UnitAffectingCombat  UnitSex  UnitLevel  GetMoney
GetHonorCurrency  GetArenaCurrency  UnitRace  UnitClass  UnitClassBase
UnitResistance  UnitStat  UnitAttackBothHands  UnitDamage  UnitRangedDamage
UnitRangedAttack  UnitAttackSpeed  UnitAttackPower  UnitRangedAttackPower
UnitDefense  UnitArmor  UnitCharacterPoints  UnitBuff  UnitDebuff  UnitAura
UnitIsTapped  UnitIsTappedByPlayer  UnitIsTappedByAllThreatList
UnitIsTrivial  UnitHasRelicSlot  SetPortraitTexture  HasFullControl
GetComboPoints  IsInGuild  IsGuildLeader  IsArenaTeamCaptain  IsInArenaTeam
IsResting  GetCombatRating  GetCombatRatingBonus  GetMaxCombatRatingBonus
GetDodgeChance  GetBlockChance  GetShieldBlock  GetParryChance
GetCritChanceFromAgility  GetSpellCritChanceFromIntellect  GetCritChance
GetRangedCritChance  GetSpellCritChance  GetSpellBonusDamage
GetSpellBonusHealing  GetPetSpellBonusDamage  GetSpellPenetration
GetArmorPenetration  GetAttackPowerForStat  UnitCreatureType
UnitCreatureFamily  GetResSicknessDuration  GetPVPSessionStats
GetPVPYesterdayStats  GetPVPLifetimeStats  UnitPVPRank  GetPVPRankInfo
GetPVPRankProgress  UnitCastingInfo  UnitChannelInfo  IsLoggedIn
IsFlyableArea  IsIndoors  IsOutdoors  IsOutOfBounds  IsFalling  IsSwimming
IsFlying  IsMounted  IsStealthed  UnitIsSameServer  GetUnitHealthModifier
GetUnitMaxHealthModifier  GetUnitPowerModifier
GetUnitHealthRegenRateFromSpirit  GetUnitManaRegenRateFromSpirit
GetManaRegen  GetPowerRegen  GetRuneCooldown  GetRuneCount  GetRuneType
ReportPlayerIsPVPAFK  PlayerIsPVPInactive  GetExpertise  GetExpertisePercent
UnitInBattleground  UnitInRange  GetUnitSpeed  GetUnitPitch  UnitInVehicle
UnitUsingVehicle  UnitControllingVehicle  UnitInVehicleControlSeat
UnitHasVehicleUI  UnitTargetsVehicleInRaidUI  UnitVehicleSkin
UnitVehicleSeatCount  UnitVehicleSeatInfo  UnitSwitchToVehicleSeat
CanSwitchVehicleSeat  GetVehicleUIIndicator  GetVehicleUIIndicatorSeat
UnitThreatSituation  UnitDetailedThreatSituation  UnitIsControlling
EjectPassengerFromSeat  CanEjectPassengerFromSeat  RespondInstanceLock
GetPlayerFacing  GetPlayerInfoByGUID  GetItemStats  GetItemStatDelta
IsXPUserDisabled  FillLocalizedClassList
```

### Friends / Who / Ignore (Battle.net) `[0x00AD93D8]` — 31
```
GetNumFriends  GetFriendInfo  SetSelectedFriend  GetSelectedFriend
AddOrRemoveFriend  AddFriend  RemoveFriend  ShowFriends  SetFriendNotes
GetNumIgnores  GetIgnoreName  SetSelectedIgnore  GetSelectedIgnore
AddOrDelIgnore  AddIgnore  DelIgnore  GetNumMutes  GetMuteName
SetSelectedMute  GetSelectedMute  AddOrDelMute  AddMute  DelMute  IsIgnored
IsMuted  IsIgnoredOrMuted  SendWho  GetNumWhoResults  GetWhoInfo  SetWhoToUI
SortWho
```

### Combat log filtering `[0x00ADB8E0]` — 11
```
CombatLogResetFilter  CombatLogAddFilter  CombatLogSetRetentionTime
CombatLogGetRetentionTime  CombatLogGetNumEntries  CombatLogSetCurrentEntry
CombatLogGetCurrentEntry  CombatLogAdvanceEntry  CombatLogClearEntries
CombatLog_Object_IsA  CombatTextSetActiveUnit
```

### Voice chat `[0x00AF29C0]` — 15
```
VoiceEnumerateOutputDevices  VoiceEnumerateCaptureDevices
VoiceSelectOutputDevice  VoiceSelectCaptureDevice
VoiceGetCurrentOutputDevice  VoiceGetCurrentCaptureDevice  GetVoiceStatus
GetNumVoiceSessions  GetVoiceSessionInfo  GetVoiceCurrentSessionID
SetActiveVoiceChannelBySessionID  GetNumVoiceSessionMembersBySessionID
GetVoiceSessionMemberInfoBySessionID  VoiceIsDisabledByClient  UnitIsTalking
```

### Spell targeting `[0x00AF51C8]` — 11
```
SpellIsTargeting  SpellCanTargetItem  SpellTargetItem  SpellCanTargetUnit
SpellTargetUnit  SpellCanTargetGlyph  SpellStopTargeting  SpellStopCasting
CancelUnitBuff  CancelItemTempEnchantment  CannotBeResurrected
```

### Frame creation (FrameScript core) `[0x00AF5848]` — 7
```
GetText  GetNumFrames  EnumerateFrames  CreateFont  CreateFrame
GetFramesRegisteredForEvent  GetCurrentKeyBoardFocus
```

### Sound `[0x00B2D928]` — 23
```
PlaySound  PlayMusic  PlaySoundFile  StopMusic
Sound_GameSystem_GetNumInputDrivers
Sound_GameSystem_GetInputDriverNameByIndex
Sound_GameSystem_GetNumOutputDrivers
Sound_GameSystem_GetOutputDriverNameByIndex
Sound_GameSystem_RestartSoundSystem  Sound_ChatSystem_GetNumInputDrivers
Sound_ChatSystem_GetInputDriverNameByIndex
Sound_ChatSystem_GetNumOutputDrivers
Sound_ChatSystem_GetOutputDriverNameByIndex  VoiceChat_StartCapture
VoiceChat_StopCapture  VoiceChat_RecordLoopbackSound
VoiceChat_StopRecordingLoopbackSound  VoiceChat_PlayLoopbackSound
VoiceChat_StopPlayingLoopbackSound  VoiceChat_IsRecordingLoopbackSound
VoiceChat_IsPlayingLoopbackSound  VoiceChat_GetCurrentMicrophoneSignalLevel
VoiceChat_ActivatePrimaryCaptureCallback
```

## Frame types & their methods

34 method tables registered with the runtime dispatcher (iterator at
`0x008167E0`). Frame-type names labelled by method-set — unambiguous against
the documented 3.3.5 API. Notable 3.x additions: the **Animation** family
(base + AnimationGroup + Translation/Rotation/Scale/Path/Alpha sub-tables),
**Cooldown**, **QuestPOIFrame**, secure-template attributes on Frame
(`IsProtected`, `SetAttribute` etc.).

### `Texture` — 29 `[table 0x00AC1028]`
```
IsObjectType  GetObjectType  GetDrawLayer  SetDrawLayer  GetBlendMode
SetBlendMode  GetVertexColor  SetVertexColor  SetGradient  SetGradientAlpha
SetAlpha  GetAlpha  Show  Hide  IsVisible  IsShown  GetTexture  SetTexture
GetTexCoord  SetTexCoord  SetRotation  SetDesaturated  IsDesaturated
SetNonBlocking  GetNonBlocking  SetHorizTile  GetHorizTile  SetVertTile
GetVertTile
```

### `FontString` — 41 `[table 0x00AC1118]`
```
IsObjectType  GetObjectType  GetDrawLayer  SetDrawLayer  SetVertexColor
GetAlpha  SetAlpha  SetAlphaGradient  Show  Hide  IsVisible  IsShown
GetFontObject  SetFontObject  GetFont  SetFont  GetText  GetFieldSize
SetText  SetFormattedText  GetTextColor  SetTextColor  GetShadowColor
SetShadowColor  GetShadowOffset  SetShadowOffset  GetSpacing  SetSpacing
SetTextHeight  GetStringWidth  GetStringHeight  GetJustifyH  SetJustifyH
GetJustifyV  SetJustifyV  CanNonSpaceWrap  SetNonSpaceWrap  CanWordWrap
SetWordWrap  GetIndentedWordWrap  SetIndentedWordWrap
```

### `Region` — 25 `[table 0x00AC1480]`
```
IsProtected  CanChangeProtectedState  SetParent  GetRect  GetCenter  GetLeft
GetRight  GetTop  GetBottom  GetWidth  SetWidth  GetHeight  SetHeight
SetSize  GetSize  GetNumPoints  GetPoint  SetPoint  SetAllPoints
ClearAllPoints  CreateAnimationGroup  GetAnimationGroups  StopAnimating
IsDragging  IsMouseOver
```

### `Frame` — 85 `[table 0x00AC1550]`
```
GetTitleRegion  CreateTitleRegion  CreateTexture  CreateFontString
GetBoundsRect  GetNumRegions  GetRegions  GetNumChildren  GetChildren
GetFrameStrata  SetFrameStrata  GetFrameLevel  SetFrameLevel  HasScript
GetScript  SetScript  HookScript  RegisterEvent  UnregisterEvent
RegisterAllEvents  UnregisterAllEvents  IsEventRegistered
AllowAttributeChanges  CanChangeAttribute  GetAttribute  SetAttribute
GetEffectiveScale  GetScale  SetScale  GetEffectiveAlpha  GetAlpha  SetAlpha
GetID  SetID  SetToplevel  IsToplevel  EnableDrawLayer  DisableDrawLayer
Show  Hide  IsVisible  IsShown  Raise  Lower  GetHitRectInsets
SetHitRectInsets  GetClampRectInsets  SetClampRectInsets  GetMinResize
SetMinResize  GetMaxResize  SetMaxResize  SetMovable  IsMovable
SetDontSavePosition  GetDontSavePosition  SetResizable  IsResizable
StartMoving  StartSizing  StopMovingOrSizing  SetUserPlaced  IsUserPlaced
SetClampedToScreen  IsClampedToScreen  RegisterForDrag  EnableKeyboard
IsKeyboardEnabled  EnableMouse  IsMouseEnabled  EnableMouseWheel
IsMouseWheelEnabled  EnableJoystick  IsJoystickEnabled  GetBackdrop
SetBackdrop  GetBackdropColor  SetBackdropColor  GetBackdropBorderColor
SetBackdropBorderColor  SetDepth  GetDepth  GetEffectiveDepth  IgnoreDepth
IsIgnoringDepth
```

### `Font (FontInstance)` — 24 `[table 0x00AC1818]`
```
GetObjectType  IsObjectType  GetName  SetFontObject  GetFontObject
CopyFontObject  SetFont  GetFont  SetAlpha  GetAlpha  SetTextColor
GetTextColor  SetShadowColor  GetShadowColor  SetShadowOffset
GetShadowOffset  SetSpacing  GetSpacing  SetJustifyH  GetJustifyH
SetJustifyV  GetJustifyV  SetIndentedWordWrap  GetIndentedWordWrap
```

### `Animation` — 31 `[table 0x00AC18E0]`
```
Play  Pause  Stop  IsDone  IsPlaying  IsPaused  IsStopped  IsDelaying
GetElapsed  SetStartDelay  GetStartDelay  SetEndDelay  GetEndDelay
SetDuration  GetDuration  GetSmoothProgress  SetSmoothProgress  GetProgress
GetProgressWithDelay  SetMaxFramerate  GetMaxFramerate  SetOrder  GetOrder
SetSmoothing  GetSmoothing  SetParent  GetRegionParent  HasScript  GetScript
SetScript  HookScript
```

### `AnimTranslation` — 2 `[table 0x00AC19DC]`
```
SetOffset  GetOffset
```

### `AnimRotation` — 6 `[table 0x00AC19F0]`
```
SetOrigin  GetOrigin  SetDegrees  GetDegrees  SetRadians  GetRadians
```

### `AnimScale` — 4 `[table 0x00AC1A24]`
```
SetOrigin  GetOrigin  SetScale  GetScale
```

### `AnimPathControlPoint` — 5 `[table 0x00AC1A48]`
```
SetParent  SetOffset  GetOffset  SetOrder  GetOrder
```

### `AnimPath` — 5 `[table 0x00AC1A74]`
```
SetCurve  GetCurve  GetControlPoints  CreateControlPoint  GetMaxOrder
```

### `AnimAlpha` — 2 `[table 0x00AC1AA0]`
```
SetChange  GetChange
```

### `AnimationGroup` — 22 `[table 0x00AC1AB8]`
```
Play  Pause  Stop  Finish  GetProgress  IsDone  IsPlaying  IsPaused
IsPendingFinish  GetDuration  SetLooping  GetLooping  GetLoopState
GetMaxOrder  SetInitialOffset  GetInitialOffset  GetAnimations
CreateAnimation  HasScript  GetScript  SetScript  HookScript
```

### `ParentedObject (UIObject)` — 4 `[table 0x00AC1B68]`
```
GetObjectType  IsObjectType  GetName  GetParent
```

### `Glue: ModelLight` — 4 `[table 0x00AC465C]`
```
ResetLights  AddLight  AddCharacterLight  AddPetLight
```

### `Minimap` — 15 `[table 0x00ACEBB8]`
```
SetMaskTexture  SetIconTexture  SetBlipTexture  SetClassBlipTexture
SetPOIArrowTexture  SetStaticPOIArrowTexture  SetCorpsePOIArrowTexture
SetPlayerTexture  SetPlayerTextureHeight  SetPlayerTextureWidth
GetZoomLevels  GetZoom  SetZoom  PingLocation  GetPingPosition
```

### `QuestPOIFrame / map overlay` — 14 `[table 0x00ACF180]`
```
SetFillTexture  SetFillAlpha  SetBorderTexture  SetBorderAlpha
SetBorderScalar  DrawQuestBlob  EnableSmoothing  EnableMerging
SetMergeThreshold  SetNumSplinePoints  UpdateQuestPOI
UpdateMouseOverTooltip  GetTooltipIndex  GetNumTooltips
```

### `PlayerModel` — 4 `[table 0x00ACF514]`
```
SetUnit  SetCreature  RefreshUnit  SetRotation
```

### `DressUpModel` — 3 `[table 0x00ACF550]`
```
Undress  Dress  TryOn
```

### `TabardModel` — 10 `[table 0x00ACF588]`
```
InitializeTabardColors  Save  CycleVariation  GetUpperBackgroundFileName
GetLowerBackgroundFileName  GetUpperEmblemFileName  GetLowerEmblemFileName
GetUpperEmblemTexture  GetLowerEmblemTexture  CanSaveTabardNow
```

### `Cooldown` — 5 `[table 0x00AD13A4]`
```
SetCooldown  SetReverse  GetReverse  SetDrawEdge  GetDrawEdge
```

### `GameTooltip` — 69 `[table 0x00AD2AE0]`
```
AddFontStrings  SetMinimumWidth  GetMinimumWidth  SetPadding  GetPadding
IsOwned  GetOwner  SetOwner  GetAnchorType  SetAnchorType  ClearLines
AddLine  AddDoubleLine  AddTexture  SetText  AppendText  FadeOut
SetHyperlink  SetAction  SetPetAction  SetShapeshift  SetPossession
SetTracking  SetSpell  SetSpellByID  SetGlyph  SetInventoryItem  SetLootItem
SetQuestItem  SetQuestLogItem  SetTrainerService  SetTradeSkillItem
SetMerchantItem  SetMerchantCostItem  SetTradePlayerItem  SetTradeTargetItem
SetBagItem  SetUnit  SetUnitBuff  SetUnitDebuff  SetUnitAura  SetTalent
SetSendMailItem  SetInboxItem  SetAuctionSellItem  SetAuctionItem  NumLines
SetQuestRewardSpell  SetQuestLogRewardSpell  SetHyperlinkCompareItem
SetBuybackItem  SetLootRollItem  SetSocketedItem  SetSocketGem
SetExistingSocketGem  SetGuildBankItem  IsUnit  GetUnit  GetItem  GetSpell
SetTotem  SetCurrencyToken  SetBackpackToken  IsEquippedItem
SetQuestLogSpecialItem  SetEquipmentSet  SetFrameStack  SetLFGDungeonReward
SetLFGCompletionReward
```

### `Model` — 24 `[table 0x00B2CB10]`
```
SetModel  GetModel  ClearModel  SetPosition  SetFacing  SetModelScale
SetSequence  SetSequenceTime  SetCamera  SetLight  GetLight  GetPosition
GetFacing  GetModelScale  AdvanceTime  ReplaceIconTexture  SetFogColor
GetFogColor  SetFogNear  GetFogNear  SetFogFar  GetFogFar  ClearFog  SetGlow
```

### `MovieFrame` — 3 `[table 0x00B2CD30]`
```
StartMovie  StopMovie  EnableSubtitles
```

### `ColorSelect` — 12 `[table 0x00B2CD50]`
```
GetColorWheelTexture  SetColorWheelTexture  GetColorWheelThumbTexture
SetColorWheelThumbTexture  GetColorValueTexture  SetColorValueTexture
GetColorValueThumbTexture  SetColorValueThumbTexture  SetColorHSV
GetColorHSV  SetColorRGB  GetColorRGB
```

### `StatusBar` — 12 `[table 0x00B2CDB8]`
```
GetOrientation  SetOrientation  GetMinMaxValues  SetMinMaxValues  GetValue
SetValue  GetStatusBarTexture  SetStatusBarTexture  GetStatusBarColor
SetStatusBarColor  GetRotatesTexture  SetRotatesTexture
```

### `Slider` — 13 `[table 0x00B2CE20]`
```
GetThumbTexture  SetThumbTexture  GetOrientation  SetOrientation
GetMinMaxValues  SetMinMaxValues  GetValue  SetValue  GetValueStep
SetValueStep  Enable  Disable  IsEnabled
```

### `ScrollFrame` — 9 `[table 0x00B2CE90]`
```
SetScrollChild  GetScrollChild  SetHorizontalScroll  SetVerticalScroll
GetHorizontalScroll  GetVerticalScroll  GetHorizontalScrollRange
GetVerticalScrollRange  UpdateScrollChildRect
```

### `ScrollingMessageFrame` — 48 `[table 0x00B2CEE0]`
```
SetFontObject  GetFontObject  SetFont  GetFont  SetTextColor  GetTextColor
SetShadowColor  GetShadowColor  SetShadowOffset  GetShadowOffset  SetSpacing
GetSpacing  SetJustifyH  GetJustifyH  SetJustifyV  GetJustifyV  AddMessage
GetMessageInfo  RemoveMessagesByAccessID  ScrollUp  ScrollDown  PageUp
PageDown  ScrollToTop  ScrollToBottom  SetScrollOffset  AtTop  AtBottom
UpdateColorByID  GetNumMessages  GetNumLinesDisplayed  GetCurrentScroll
GetCurrentLine  GetMaxLines  SetMaxLines  SetFading  GetFading
SetTimeVisible  GetTimeVisible  SetFadeDuration  GetFadeDuration  Clear
SetInsertMode  GetInsertMode  SetIndentedWordWrap  GetIndentedWordWrap
SetHyperlinksEnabled  GetHyperlinksEnabled
```

### `MessageFrame` — 28 `[table 0x00B2D068]`
```
SetFontObject  GetFontObject  SetFont  GetFont  SetTextColor  GetTextColor
SetShadowColor  GetShadowColor  SetShadowOffset  GetShadowOffset  SetSpacing
GetSpacing  SetJustifyH  GetJustifyH  SetJustifyV  GetJustifyV
SetInsertMode  GetInsertMode  SetIndentedWordWrap  GetIndentedWordWrap
SetFading  GetFading  SetTimeVisible  GetTimeVisible  SetFadeDuration
GetFadeDuration  AddMessage  Clear
```

### `SimpleHTML` — 23 `[table 0x00B2D150]`
```
SetFontObject  GetFontObject  SetFont  GetFont  SetTextColor  GetTextColor
SetShadowColor  GetShadowColor  SetShadowOffset  GetShadowOffset  SetSpacing
GetSpacing  SetJustifyH  GetJustifyH  SetJustifyV  GetJustifyV
SetIndentedWordWrap  GetIndentedWordWrap  SetText  SetHyperlinkFormat
GetHyperlinkFormat  SetHyperlinksEnabled  GetHyperlinksEnabled
```

### `EditBox` — 58 `[table 0x00B2D210]`
```
SetFontObject  GetFontObject  SetFont  GetFont  SetTextColor  GetTextColor
SetShadowColor  GetShadowColor  SetShadowOffset  GetShadowOffset  SetSpacing
GetSpacing  SetJustifyH  GetJustifyH  SetJustifyV  GetJustifyV
SetIndentedWordWrap  GetIndentedWordWrap  SetAutoFocus  IsAutoFocus
SetCountInvisibleLetters  IsCountInvisibleLetters  SetMultiLine  IsMultiLine
SetNumeric  IsNumeric  SetPassword  IsPassword  SetBlinkSpeed  GetBlinkSpeed
Insert  SetText  GetText  SetNumber  GetNumber  HighlightText
AddHistoryLine  ClearHistory  SetTextInsets  GetTextInsets  SetFocus
ClearFocus  HasFocus  SetMaxBytes  GetMaxBytes  SetMaxLetters  GetMaxLetters
GetNumLetters  GetHistoryLines  SetHistoryLines  GetInputLanguage
ToggleInputLanguage  SetAltArrowKeyMode  GetAltArrowKeyMode
IsInIMECompositionMode  SetCursorPosition  GetCursorPosition
GetUTF8CursorPosition
```

### `CheckButton` — 6 `[table 0x00B2D3E4]`
```
SetChecked  GetChecked  GetCheckedTexture  SetCheckedTexture
GetDisabledCheckedTexture  SetDisabledCheckedTexture
```

### `Button` — 34 `[table 0x00B2D418]`
```
Enable  Disable  IsEnabled  GetButtonState  SetButtonState
SetNormalFontObject  GetNormalFontObject  SetDisabledFontObject
GetDisabledFontObject  SetHighlightFontObject  GetHighlightFontObject
SetFontString  GetFontString  SetText  SetFormattedText  GetText
SetNormalTexture  GetNormalTexture  SetPushedTexture  GetPushedTexture
SetDisabledTexture  GetDisabledTexture  SetHighlightTexture
GetHighlightTexture  SetPushedTextOffset  GetPushedTextOffset  GetTextWidth
GetTextHeight  RegisterForClicks  Click  LockHighlight  UnlockHighlight
GetMotionScriptsWhileDisabled  SetMotionScriptsWhileDisabled
```

## Rough numbers

| Surface           | Tables | Entries |
|-------------------|--------|---------|
| Globals (in-game) | 67     | 2022    |
| Globals (glue)    | 4      | 168     |
| `PlayDance` singleton | 1  | 1       |
| Frame methods     | 34     | 679     |
| **Total**         | **106** | **2870** |

(73 register-call sites + 1 singleton + 39 iterator-call sites; 39 method-table
sites map to 34 unique tables since several tables get re-registered from
multiple init paths.)

## How this was extracted

1. Locate `FrameScript_RegisterFunction` — `0x00817F90`, verified by reading
   its body and finding `lua_setfield(L, LUA_GLOBALSINDEX=-10002, name)`.
2. Scan all of `.text` for `E8` calls to that VA. 73 hits.
3. For each call site, recognise the canonical cdecl loop pattern
   (`mov eax/ecx,[esi+T+4/T]; push eax; push ecx; call register; add esi,8;
   add esp,8; cmp esi,N; jb`). Both 8-bit and 32-bit `cmp` immediates appear,
   and the register pair varies (`eax/ecx` or `edx/eax`).
4. Walk each table reading 8-byte `{name, func}` records.
5. For the iterator at `0x008167E0` (frame-method registrar), do the same:
   scan `.text` for callers (39), extract pushed count/table/context.
6. Frame-type identification by method set — all 34 are unambiguous against
   the documented 3.3.5 API.

Raw extracted dumps are kept alongside this doc as `raw_wotlk_globals.txt`
and `raw_wotlk_methods.txt` for cross-checking.

