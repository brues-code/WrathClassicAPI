-- Backport of FrameXML/EventUtil.lua to 3.3.5a / Lua 5.1.
--
-- Implementation differences from the modern source:
--   - 3.3.5 has no C_AddOns; the legacy IsAddOnLoaded(name) global fills
--     the same role.
--   - 3.3.5 has no secureexecuterange; queued callbacks are dispatched
--     by a plain ipairs loop.
--   - AreVariablesLoaded in the modern source sniffs GlueParent /
--     UIParent.firstTimeLoaded. 3.3.5's UIParent.firstTimeLoaded isn't
--     reliable here, so we track a private flag flipped on
--     VARIABLES_LOADED.

EventUtil = {}

local ContinueAfterAllEventsMixin = {}

function ContinueAfterAllEventsMixin:Init(callback, ...)
    self.events = {}

    for index = 1, select("#", ...) do
        local event = select(index, ...)
        assert(C_EventUtils.IsEventValid(event), "Unknown event \"" .. event .. "\"")
        self.events[event] = false

        local function OnEventReceived()
            self.events[event] = true

            EventRegistry:UnregisterFrameEventAndCallback(event, self)

            if self:HaveReceivedAllEvents() then
                callback()
            end
        end

        EventRegistry:RegisterFrameEventAndCallback(event, OnEventReceived, self)
    end
end

function ContinueAfterAllEventsMixin:HaveReceivedAllEvents()
    for event, received in pairs(self.events) do
        if not received then
            return false
        end
    end
    return true
end

function EventUtil.ContinueAfterAllEvents(callback, ...)
    local obj = CreateFromMixins(ContinueAfterAllEventsMixin)
    obj:Init(callback, ...)
end

-- Tracked locally because 3.3.5 has no reliable GlueParent-style hook.
-- Bootstrap frame at the bottom of the file listens for VARIABLES_LOADED
-- and flips the flag, then drains the queued callbacks via the
-- TriggerOnVariablesLoaded path.
local variablesLoaded = false
function EventUtil.AreVariablesLoaded()
    return variablesLoaded
end

local eventUtilVariablesLoadedCallbacks = {}
function EventUtil.ContinueOnVariablesLoaded(callback)
    if EventUtil.AreVariablesLoaded() then
        callback()
        return
    end

    table.insert(eventUtilVariablesLoadedCallbacks, callback)
end

local eventUtilContinueOnVariablesLoadedTriggered = false
function EventUtil.TriggerOnVariablesLoaded()
    if eventUtilContinueOnVariablesLoadedTriggered then
        return
    end
    eventUtilContinueOnVariablesLoadedTriggered = true
    variablesLoaded = true

    for _, callback in ipairs(eventUtilVariablesLoadedCallbacks) do
        callback()
    end
end

function EventUtil.ContinueOnAddOnLoaded(addOnName, callback)
    -- 3.3.5's IsAddOnLoaded returns a single bool; modern WoW split that
    -- into (isLoadedOrLoading, isLoaded). Treat truthy as "ready to use".
    if IsAddOnLoaded(addOnName) then
        callback()
        return
    end

    EventUtil.RegisterOnceFrameEventAndCallback("ADDON_LOADED", callback, addOnName)
end

function EventUtil.ContinueOnPlayerLogin(callback)
    if IsLoggedIn() then
        callback()
        return
    end

    EventUtil.RegisterOnceFrameEventAndCallback("PLAYER_LOGIN", callback)
end

-- `...` are optional event arguments that must match before the callback fires.
function EventUtil.RegisterOnceFrameEventAndCallback(frameEvent, callback, ...)
    local handle = nil
    local requiredEventArgs = SafePack(...)
    local CallbackWrapper = function(callbackHandlerID, ...)
        for i = 1, requiredEventArgs.n do
            if (select(i, ...)) ~= requiredEventArgs[i] then
                return
            end
        end

        handle:Unregister()
        callback(...)
    end

    handle = EventRegistry:RegisterFrameEventAndCallbackWithHandle(frameEvent, CallbackWrapper)
end

CallbackHandleContainerMixin = {}

function CallbackHandleContainerMixin:Init()
    self.handles = {}
end

function CallbackHandleContainerMixin:RegisterCallback(cbr, event, callback, owner)
    self:AddHandle(cbr:RegisterCallbackWithHandle(event, callback, owner))
end

function CallbackHandleContainerMixin:AddHandle(handle)
    table.insert(self.handles, handle)
end

function CallbackHandleContainerMixin:Unregister()
    for index, handle in ipairs(self.handles) do
        handle:Unregister()
    end
    self.handles = {}
end

function CallbackHandleContainerMixin:IsEmpty()
    return #self.handles == 0
end

function EventUtil.CreateCallbackHandleContainer()
    local cbrHandles = CreateFromMixins(CallbackHandleContainerMixin)
    cbrHandles:Init()
    return cbrHandles
end

-- VARIABLES_LOADED bootstrap. Drains anything queued via
-- ContinueOnVariablesLoaded the first time the event fires. We use a
-- dedicated frame so we don't pollute EventRegistry's payload routing.
do
    local f = CreateFrame("Frame")
    f:RegisterEvent("VARIABLES_LOADED")
    f:SetScript("OnEvent", function(self)
        EventUtil.TriggerOnVariablesLoaded()
        self:UnregisterEvent("VARIABLES_LOADED")
    end)
end
