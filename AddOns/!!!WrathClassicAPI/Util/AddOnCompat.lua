if C_AddOns.DoesAddOnExist('pfUI') then
    EventUtil.ContinueOnAddOnLoaded('pfUI', function()
        -- pfUI\pfUI.lua redeclares the RAID_CLASS_COLORS object at least 14 times (insane!!)
        -- This stops it from replacing all our ColorMixin values
        pfUI.UpdateColors = function() end
        RAID_CLASS_COLORS = setmetatable(RAID_CLASS_COLORS, { __index = function()
            local unknownColor = CreateColor(0.6, 0.6, 0.6)
            unknownColor.colorStr = unknownColor:GenerateHexColor()
            return unknownColor
        end})
    end)
end
