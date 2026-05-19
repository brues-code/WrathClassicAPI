-- Backport of FrameXML/Mixin.lua. The Mixin pattern landed in Cataclysm;
-- stock 3.3.5 FrameXML has none of these helpers.
--
-- Lua 5.1 has real varargs (`...` + `select`), so unlike the 1.12 sibling
-- there's no `arg`-table dance here.

function Mixin(object, ...)
    for i = 1, select("#", ...) do
        local mixin = select(i, ...)
        if mixin then
            for k, v in pairs(mixin) do
                object[k] = v
            end
        end
    end
    return object
end

function CreateFromMixins(...)
    return Mixin({}, ...)
end

function CreateAndInitFromMixin(mixin, ...)
    local object = CreateFromMixins(mixin)
    object:Init(...)
    return object
end
