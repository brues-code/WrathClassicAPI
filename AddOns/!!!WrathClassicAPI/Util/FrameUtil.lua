-- Backport of the top-level parent functions of retail's
-- Blizzard_SharedXMLBase/FrameUtil.lua, verbatim.

local alternateTopLevelParent;
function SetAlternateTopLevelParent(parent)
	alternateTopLevelParent = parent;
	EventRegistry:TriggerEvent("UI.AlternateTopLevelParentChanged", parent);
end

function ClearAlternateTopLevelParent()
	alternateTopLevelParent = nil;
	EventRegistry:TriggerEvent("UI.AlternateTopLevelParentChanged");
end

-- optionalExcludedParent: Frame to avoid returning if it is currently the alternateTopLevelParent; Useful for frames that might currently be the alternate but need the root top for scaling
function GetAppropriateTopLevelParent(optionalExcludedParent)
	if alternateTopLevelParent and alternateTopLevelParent:IsShown() and (not optionalExcludedParent or alternateTopLevelParent ~= optionalExcludedParent) then
		return alternateTopLevelParent;
	end

	return UIParent or GlueParent;
end

function SetAppropriateTopLevelParent(frame)
	local parent = GetAppropriateTopLevelParent();
	if parent then
		frame:SetParent(parent);
	end
end
function GetAppropriateTooltip()
	return UIParent and GameTooltip or GlueTooltip;
end
