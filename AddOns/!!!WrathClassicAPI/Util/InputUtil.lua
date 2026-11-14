-- Backport of the cursor functions of retail's Blizzard_SharedXML/InputUtil.lua,
-- verbatim. The rest of retail's InputUtil needs API this client does not have.

InputUtil = {};

function InputUtil.GetCursorPosition(parent)
	local x, y = GetCursorPosition();
	local scale = parent:GetEffectiveScale();
	return x / scale, y / scale;
end

function InputUtil.GetCursorDelta(parent)
	local x, y = GetCursorDelta();
	local scale = parent:GetEffectiveScale();
	return x / scale, y / scale;
end
