-- Backport of modern WoW's IconDataProvider.lua (Blizzard_FrameXMLBase) to
-- 3.3.5a / Lua 5.1.
--
-- Wraps the four engine icon-enumeration functions (GetLooseMacroIcons /
-- GetLooseMacroItemIcons / GetMacroIcons / GetMacroItemIcons — all surfaced by
-- WrathClassicAPI.dll) into a stateful Spell/Item bucket pair, with an optional
-- "extras" array of icons prepended ahead of the base DB. The two extra types:
--
--   * Spellbook -- icons of every spell in the player's spellbook UI plus every
--     talent across the three talent trees, so "icons relevant to my class"
--     surface first in the Macro UI's icon picker.
--   * Equipment -- icons of the currently-equipped items (slots 1..19 via
--     GetInventoryItemTexture("player", slot)), so the Equipment-Manager popup
--     leads with what the player is wearing.
--
-- 3.3.5 differences from the modern source:
--
--   * EnumUtil.MakeEnum is inlined as a plain table.
--   * Modern's spec-aware C_SpecializationInfo.GetTalentInfo / GetPvpTalentInfoByID
--     are replaced with 3.3.5's GetTalentInfo(tab, idx) across the three trees;
--     there are no specs or PvP talents, so those layers are dropped. Flyouts
--     don't exist either, so GetSpellBookItemTexture becomes GetSpellTexture(
--     slot, "spell").
--   * Every icon here is already a full "Interface\Icons\..." path (3.3.5 has no
--     fileID system): the four macro functions return paths, and so do
--     GetSpellTexture / GetTalentInfo / GetInventoryItemTexture. So the base
--     lookup returns the stored value verbatim (no prefixing), and saving strips
--     back to the basename with a trailing-component match.
--   * extraIconsMap is a Lua-set keyed by texture path -- GetKeysArray flattens
--     it to a numbered array. Same dedup semantics as modern: each texture
--     appears once within the extras section, with no cross-dedup against the
--     base DB walk.

local QuestionMarkIconPath = "Interface\\Icons\\INV_Misc_QuestionMark"

local NumActiveIconDataProviders = 0
local BaseIconFilenames = nil

-- Builds BaseIconFilenames from the engine icon DB. Lazy and shared across all
-- active IconDataProvider instances, so the four engine calls (each scanning
-- thousands of files) only run once per picker session.
local function IconDataProvider_RefreshIconTextures()
    if BaseIconFilenames ~= nil then
        return
    end

    BaseIconFilenames = {}
    BaseIconFilenames[IconDataProviderIconType.Spell] = {}
    BaseIconFilenames[IconDataProviderIconType.Item] = {}
    GetLooseMacroIcons(BaseIconFilenames[IconDataProviderIconType.Spell])
    GetLooseMacroItemIcons(BaseIconFilenames[IconDataProviderIconType.Item])
    GetMacroIcons(BaseIconFilenames[IconDataProviderIconType.Spell])
    GetMacroItemIcons(BaseIconFilenames[IconDataProviderIconType.Item])
end

local function IconDataProvider_ClearIconTextures()
    BaseIconFilenames = nil
    collectgarbage()
end

local function IconDataProvider_GetBaseIconTexture(iconType, index)
    -- The macro-icon functions already return full "Interface\Icons\..." paths
    -- (3.3.5 has no fileID system), so there's nothing to prefix.
    return BaseIconFilenames[iconType][index]
end

function IconDataProvider_GetAllIconTypes()
    local iconTypeValues = GetValuesArray(IconDataProviderIconType)
    table.sort(iconTypeValues)
    return iconTypeValues
end

IconDataProviderMixin = {}

-- EnumUtil.MakeEnum("Spell", "Item") produces { Spell = 1, Item = 2 }.
IconDataProviderIconType = { Spell = 1, Item = 2 }

IconDataProviderExtraType = {
    Spellbook = 1,
    Equipment = 2,
    None = 3,
}

local function FillOutExtraIconsMapWithSpells(extraIconsMap)
    for tab = 1, GetNumSpellTabs() do
        local _, _, offset, numSpells = GetSpellTabInfo(tab)
        offset = offset + 1
        local tabEnd = offset + numSpells
        for slot = offset, tabEnd - 1 do
            local tex = GetSpellTexture(slot, "spell")
            if tex ~= nil and tex ~= "" then
                extraIconsMap[tex] = true
            end
        end
    end
end

-- Walks all three 3.3.5 talent trees, gathering every talent's icon -- including
-- talents with no points invested; the whole tree is "relevant to my class" for
-- picker purposes. GetTalentInfo(tab, idx) returns (name, icon, ...); we read
-- the icon (second return) directly.
local function FillOutExtraIconsMapWithTalents(extraIconsMap)
    for tab = 1, GetNumTalentTabs() do
        local numTalents = GetNumTalents(tab)
        for talent = 1, numTalents do
            local _, icon = GetTalentInfo(tab, talent)
            if icon ~= nil and icon ~= "" then
                extraIconsMap[icon] = true
            end
        end
    end
end

local function FillOutExtraIconsMapWithEquipment(extraIconsMap)
    for slot = 1, 19 do
        local itemTexture = GetInventoryItemTexture("player", slot)
        if itemTexture then
            extraIconsMap[itemTexture] = true
        end
    end
end

function IconDataProviderMixin:Init(extraType, extraIconsOnly, requestedIconTypes)
    self.extraIcons = {}
    self.extraIconType = extraType
    self.requestedIconTypes = requestedIconTypes or IconDataProvider_GetAllIconTypes()

    if extraType == IconDataProviderExtraType.Spellbook then
        local extraIconsMap = {}
        FillOutExtraIconsMapWithSpells(extraIconsMap)
        FillOutExtraIconsMapWithTalents(extraIconsMap)
        self.extraIcons = GetKeysArray(extraIconsMap)
    elseif extraType == IconDataProviderExtraType.Equipment then
        local extraIconsMap = {}
        FillOutExtraIconsMapWithEquipment(extraIconsMap)
        self.extraIcons = GetKeysArray(extraIconsMap)
    end

    if not extraIconsOnly then
        NumActiveIconDataProviders = NumActiveIconDataProviders + 1
        IconDataProvider_RefreshIconTextures()
    end
end

function IconDataProviderMixin:SetIconTypes(iconTypes)
    self.requestedIconTypes = iconTypes or IconDataProvider_GetAllIconTypes()
end

function IconDataProviderMixin:GetNumIcons()
    -- 1 to account for the leading `?` icon.
    local numIcons = 1
    if self:ShouldShowExtraIcons() then
        numIcons = numIcons + #self.extraIcons
    end
    if BaseIconFilenames then
        for _, v in pairs(self.requestedIconTypes) do
            numIcons = numIcons + #BaseIconFilenames[v]
        end
    end
    return numIcons
end

function IconDataProviderMixin:GetIconByIndex(index)
    if index == 1 then
        return QuestionMarkIconPath
    end

    index = index - 1

    local numExtraIcons = (self:ShouldShowExtraIcons() and #self.extraIcons) or 0
    if index <= numExtraIcons then
        return self.extraIcons[index]
    end

    local baseIndex = index - numExtraIcons
    local lookupIconType = nil
    -- Each icon type's table is indexed from 1, so loop through the tables to
    -- find which icon type we index to.
    for _, v in pairs(self.requestedIconTypes) do
        local numIconsForType = #BaseIconFilenames[v]
        if baseIndex <= numIconsForType then
            lookupIconType = v
            break
        end
        baseIndex = baseIndex - numIconsForType
    end

    if lookupIconType then
        return IconDataProvider_GetBaseIconTexture(lookupIconType, baseIndex)
    else
        return nil
    end
end

function IconDataProviderMixin:GetIconForSaving(index)
    local icon = self:GetIconByIndex(index)
    if type(icon) == "string" then
        -- Strip any "Interface\Icons\" prefix (case-agnostic) to the basename.
        icon = icon:match("[^\\]+$") or icon
    end
    return icon
end

function IconDataProviderMixin:GetIndexOfIcon(icon)
    local numIcons = self:GetNumIcons()
    for i = 1, numIcons do
        if self:GetIconByIndex(i) == icon then
            return i
        end
    end
    return nil
end

function IconDataProviderMixin:ShouldShowExtraIcons()
    return (self.extraIconType == IconDataProviderExtraType.Spellbook and
            tContains(self.requestedIconTypes, IconDataProviderIconType.Spell))
        or (self.extraIconType == IconDataProviderExtraType.Equipment and
            tContains(self.requestedIconTypes, IconDataProviderIconType.Item))
end

function IconDataProviderMixin:Release()
    NumActiveIconDataProviders = NumActiveIconDataProviders - 1
    if NumActiveIconDataProviders <= 0 then
        IconDataProvider_ClearIconTextures()
    end
end
