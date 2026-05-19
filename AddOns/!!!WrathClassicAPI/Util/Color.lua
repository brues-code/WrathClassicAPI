ColorMixin = ColorMixin or {}

function CreateColor(r, g, b, a)
    local color = CreateFromMixins(ColorMixin)
    color:OnLoad(r, g, b, a)
    return color
end

function ColorMixin:OnLoad(r, g, b, a)
    self:SetRGBA(r, g, b, a)
end

function ColorMixin:IsEqualTo(otherColor)
    return self.r == otherColor.r
        and self.g == otherColor.g
        and self.b == otherColor.b
        and self.a == otherColor.a
end

function ColorMixin:GetRGB()
    return self.r, self.g, self.b
end

function ColorMixin:GetRGBAsBytes()
    return self.r * 255, self.g * 255, self.b * 255
end

function ColorMixin:GetRGBA()
    return self.r, self.g, self.b, self.a
end

function ColorMixin:GetRGBAAsBytes()
    return self.r * 255, self.g * 255, self.b * 255, (self.a or 1) * 255
end

function ColorMixin:SetRGBA(r, g, b, a)
    self.r = r
    self.g = g
    self.b = b
    self.a = a
end

function ColorMixin:SetRGB(r, g, b)
    self:SetRGBA(r, g, b, nil)
end

function ColorMixin:GenerateHexColor()
    return string.format("ff%.2x%.2x%.2x", self:GetRGBAsBytes())
end

function ColorMixin:GenerateHexColorMarkup()
    return "|c"..self:GenerateHexColor()
end

function ColorMixin:WrapTextInColorCode(text)
    return WrapTextInColorCode(text, self:GenerateHexColor())
end

function WrapTextInColorCode(text, colorHexString)
    return string.format("|c%s%s|r", colorHexString, text)
end

function ColorMixin:WrapTextInColorTableCode(text)
    return WrapTextInColorCode(text, self:GenerateHexColor())
end

for _, v in pairs(ITEM_QUALITY_COLORS) do
    v.color = CreateColor(v.r, v.g, v.b, 1)
end

do
    local envTbl = _G
    for _, dbColor in ipairs(C_UIColor.GetColors()) do
        local color = CreateColor(dbColor.color.r, dbColor.color.g, dbColor.color.b, dbColor.color.a)
        envTbl[dbColor.baseTag] = color
        envTbl[dbColor.baseTag.."_CODE"] = color:GenerateHexColorMarkup()
    end
end
