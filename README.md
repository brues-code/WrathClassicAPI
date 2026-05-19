# WrathClassicAPI

A small DLL for World of Warcraft 3.3.5a (Wrath of the Lich King /
ChromieCraft) that adds a handful of modern Lua API calls Blizzard
introduced after WotLK shipped — primarily to backport addons written
against later API versions (4.x / 5.x / 8.x+) where these calls exist.

The DLL hooks the FrameScript engine after WoW boots and registers its
extensions through the same in-engine mechanisms the engine itself
uses for Lua functions and events. No companion addon required.

This is the WotLK 3.3.5a sibling of [ClassicAPI][classicapi] (1.12).
Mirrors its module pattern and conventions; only the engine offsets
and the Lua-state ABI differ (3.3.5 moved Lua to 5.1 and the C
function ABI to `__cdecl`).

[classicapi]: https://github.com/brues-code/ClassicAPI

## Status

Verified in-game on ChromieCraft 3.3.5a. Loads cleanly alongside
[awesome_wotlk][aw]; coexistence is non-conflicting (no shared
detours on hot dispatch sites). The work-in-progress scope is "the
modern item-cache API surface plus the supporting event-injection
plumbing"; more features will follow.

[aw]: https://github.com/FrostAtom/awesome_wotlk

## What's added

### Lua calls

| Namespace | Calls |
|-----------|-------|
| Events    | `C_EventUtils.IsEventValid` |
| Expansion | `GetClassicExpansionLevel`, `ClassicExpansionAtLeast`, `ClassicExpansionAtMost` |
| Item      | `C_Item.DoesItemExist`, `C_Item.DoesItemExistByID`, `C_Item.IsItemDataCached`, `C_Item.IsItemDataCachedByID`, `C_Item.RequestLoadItemData`, `C_Item.RequestLoadItemDataByID` |
| UI Color  | `C_UIColor.GetColors` |

### Globals

| Group | Constants |
|-------|-----------|
| Expansion | `LE_EXPANSION_LEVEL_CURRENT`, `LE_EXPANSION_CLASSIC` … `LE_EXPANSION_MIDNIGHT` |

### Events

| Event | Payload | When it fires |
|-------|---------|---------------|
| `GET_ITEM_INFO_RECEIVED` | `itemID, success` | The engine has just filled the item-stats cache from an `SMSG_ITEM_QUERY_SINGLE_RESPONSE` triggered by an **implicit** path (`GetItemInfo(uncachedID)`, hyperlink hover, chat-link resolution, etc.) |
| `ITEM_DATA_LOAD_RESULT`  | `itemID, success` | The engine has just filled the cache for an **explicit** `C_Item.RequestLoadItemData(ByID)` call |

A given cache fill fires exactly one of these — never both — depending
on what initiated the request. Same split as modern WoW.

### Behavioral extensions

| Function | Change |
|----------|--------|
| `GetItemInfo(itemID|"item:N..."|"name")` | The 3.3.5 implementation returns nil on cache misses with no follow-up query. We hook it so a miss now kicks off `SMSG_ITEM_QUERY_SINGLE` transparently; the original still returns nil this call, but subsequent calls return data and `GET_ITEM_INFO_RECEIVED` fires when the response arrives. Same shape as modern WoW (5.4+). |

### itemLocation argument shape

The two `_ByID`-suffixed `C_Item` calls take an integer item ID. The
unsuffixed variants take a Lua table — same as the modern
[ItemLocation mixin][item-location]:

```lua
{ equipmentSlotIndex = N }     -- character-pane slot, 1..19
{ bagID = 0, slotIndex = N }   -- backpack, 1..16
{ bagID = 1..4, slotIndex = N } -- equipped bag
```

The string-GUID form (`"0xHHHHHHHHLLLLLLLL"`) is **not** supported in
this build; it needs the engine's CGItemMgr GUID resolver re-derived.

[item-location]: https://warcraft.wiki.gg/wiki/ItemLocationMixin

## Building

```powershell
git submodule update --init --recursive    # vendored MinHook
cmake -B build -A Win32
cmake --build build --config Release
```

Output: `build/Release/WrathClassicAPI.dll`. Must be **Win32 (x86)** —
the target `Wow.exe` is 32-bit and won't load an x64 DLL.

## Deploying

WrathClassicAPI is loaded via [LichLoader][lichloader]. Copy
`WrathClassicAPI.dll` into the same directory as your `Wow.exe` (or a
`dll\` subdirectory next to it) and add its path to `dlls.txt`:

```
dll\WrathClassicAPI.dll
```

Launch via `LichLoader.exe`. Hooks install before the game's main
thread starts.

[lichloader]: https://github.com/brues-code/LichLoader

## Project layout

```
src/
  Common.h         macro for HOOK_FUNCTION + MinHook plumbing
  DllMain.cpp      MinHook init, GameUI hook, hook-registration chain
  Game.{h,cpp}     Lua 5.1 C API wrappers + ModuleAutoRegister / HookAutoRegister
  Offsets.h        every 3.3.5 VA we touch, in one file with rationale
  event/
    Custom.{h,cpp} AutoReserve / Fire — appends names to the engine's event table
    Util.cpp       C_EventUtils.IsEventValid implementation
  expansion/
    Info.cpp       GetClassicExpansionLevel + ClassicExpansionAt{Least,Most} + LE_EXPANSION_*
  item/
    Arg.{h,cpp}    Lua arg → itemID parsing (number, item:N, hyperlink)
    Data.{h,cpp}   GetItemInfo hook, C_Item.* registrations, event firing
    Exists.cpp     C_Item.DoesItemExist[ByID]
    Location.{h,cpp}  itemLocation table → CGItem resolution
  ui/
    ColorData.h    Embedded GlobalColor.dbc snapshot (~190 named colors)
    Color.cpp      C_UIColor.GetColors implementation
```

## Adding a new Lua function

Each feature is a single `src/<domain>/<File>.cpp` with three pieces:

1. A `static int __cdecl Script_Foo(void *L)` — the Lua C function.
2. A `static void RegisterLuaFunctions()` that calls
   `Game::Lua::RegisterTableFunction("C_Foo", "Bar", &Script_Foo)`
   (or `RegisterGlobalFunction` for flat-global names).
3. A file-scope `static const Game::ModuleAutoRegister _r{&RegisterLuaFunctions};`.

`CMakeLists.txt` globs `src/**/*.cpp` with `CONFIGURE_DEPENDS`, so a
new file doesn't need any other edits. The static-init `_r` chains
the module's registration into the GameUI post-hook's batch.

Engine hooks follow the same pattern via `Game::HookAutoRegister` — see
[`src/item/Data.cpp`][hook-example] for an example.

[hook-example]: src/item/Data.cpp
