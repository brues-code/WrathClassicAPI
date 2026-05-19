-- Backport of FrameXML/GlobalCallbackRegistry.lua to 3.3.5a / Lua 5.1.
--
-- The modern source ref-counts WoW frame events via a
-- SetAttribute/OnAttributeChanged dance because attribute mutations are
-- taint-safe in the secure-template model. 3.3.5 has neither secure
-- templates nor an attribute-driven registration path that maps cleanly,
-- so we keep an ordinary {[event]=count} table and Register/Unregister
-- on 0<->1 transitions.
--
-- 3.3.5 OnEvent scripts receive `(self, event, ...)` as arguments
-- (the WoW 2.0+ signature) rather than the 1.12 `event`/`arg1` globals.

EventRegistry = CreateFromMixins(CallbackRegistryMixin)

function EventRegistry:OnLoad()
    CallbackRegistryMixin.OnLoad(self)
    self:SetUndefinedEventsAllowed(true)

    self.eventCounts = {}
    self.frameEventFrame = CreateFrame("Frame")
    self.frameEventFrame.registry = self
    self.frameEventFrame:SetScript("OnEvent", function(_, event, ...)
        self:TriggerEvent(event, ...)
    end)
end

function EventRegistry:RegisterFrameEvent(frameEvent)
    local n = (self.eventCounts[frameEvent] or 0) + 1
    self.eventCounts[frameEvent] = n
    if n == 1 then
        self.frameEventFrame:RegisterEvent(frameEvent)
    end
end

function EventRegistry:UnregisterFrameEvent(frameEvent)
    local n = self.eventCounts[frameEvent] or 0
    if n > 0 then
        n = n - 1
        self.eventCounts[frameEvent] = n
        if n == 0 then
            self.frameEventFrame:UnregisterEvent(frameEvent)
        end
    end
end

function EventRegistry:RegisterFrameEventAndCallback(frameEvent, ...)
    self:RegisterFrameEvent(frameEvent)
    return self:RegisterCallback(frameEvent, ...)
end

local function CreateCallbackHandle(cbr, cbrHandle, frameEvent)
    local handle = {
        Unregister = function()
            cbr:UnregisterFrameEvent(frameEvent)
            cbrHandle:Unregister()
        end,
    }
    return handle
end

function EventRegistry:RegisterFrameEventAndCallbackWithHandle(frameEvent, ...)
    self:RegisterFrameEvent(frameEvent)
    local cbrHandle = self:RegisterCallbackWithHandle(frameEvent, ...)
    return CreateCallbackHandle(self, cbrHandle, frameEvent)
end

function EventRegistry:UnregisterFrameEventAndCallback(frameEvent, ...)
    self:UnregisterFrameEvent(frameEvent)
    self:UnregisterCallback(frameEvent, ...)
end

function EventRegistry:GetEventCounts(...)
    local counts = {}
    for i = 1, select("#", ...) do
        local frameEvent = select(i, ...)
        local count = self.eventCounts[frameEvent] or "?"
        table.insert(counts, frameEvent .. " : " .. tostring(count))
    end

    return table.concat(counts, "\n")
end

EventRegistry:OnLoad()
