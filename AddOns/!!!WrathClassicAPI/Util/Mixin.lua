-- `Mixin` and `CreateFromMixins` are engine (DLL) natives — see
-- src/baselib/Mixin.cpp — so they exist whenever WrathClassicAPI is injected,
-- even with this addon disabled, mirroring retail where they live in TableUtil
-- as C functions. Only the composite helper (which calls a Lua `:Init` method)
-- stays here, like retail's Mixin.lua.

function CreateAndInitFromMixin(mixin, ...)
    local object = CreateFromMixins(mixin)
    object:Init(...)
    return object
end
