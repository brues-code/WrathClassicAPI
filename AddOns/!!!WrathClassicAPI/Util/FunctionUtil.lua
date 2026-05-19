-- Backport of FunctionUtil.lua to 3.3.5a / Lua 5.1.
--
-- Implementation differences from the modern source:
--   - 3.3.5 Lua 5.1's xpcall is strictly (f, handler) — no trailing args.
--     We wrap the script call in a local closure that captures frame
--     and arg pack.
--   - CallErrorHandler isn't in 3.3.5; route through geterrorhandler()
--     (same pattern as ItemUtil.lua).
--   - `nop` and `assertsafe` aren't globals in stock 3.3.5; defined as
--     local fallbacks so this file stays self-contained.

local function nop() end

local function CallErrorHandler(err)
	local h = geterrorhandler and geterrorhandler()
	if h then return h(err) end
end

-- Match the modern assertsafe semantics: only fires when `condition` is
-- falsy. Calls with just a message string are no-ops (the message ends
-- up as the condition and is truthy) — matches Blizzard's source.
local function assertsafe(condition, msg)
	if not condition then
		CallErrorHandler(msg or "assertion failed!")
	end
end

function ExecuteFrameScript(frame, scriptName, ...)
	local script = frame:GetScript(scriptName);
	if script then
		local n = select("#", ...);
		local args = { ... };
		local function invoke()
			return script(frame, unpack(args, 1, n));
		end
		xpcall(invoke, CallErrorHandler);
	end
end

function CallMethodOnNearestAncestor(self, methodName, ...)
	local ancestor = self:GetParent();
	while ancestor and not ancestor[methodName] do
		ancestor = ancestor:GetParent();
	end

	if ancestor then
		return true, ancestor[methodName](ancestor, ...);
	end

	return false;
end

function GetValueOrCallFunction(tbl, key, ...)
	if not tbl then
		return;
	end

	local value = tbl[key];
	if type(value) == "function" then
		return value(...);
	else
		return value;
	end
end

local function CompositeIterator(iteratorCallbackArray)
	local count = #iteratorCallbackArray;
	if count == 0 then
		return nop;
	end

	local iteratorIndex = 1;
	local currentIterator, currentTable, iteratorKey = iteratorCallbackArray[1]();
	local function AdvanceIterators()
		if currentIterator == nil then
			return nil;
		end

		local nextKey = currentIterator(currentTable, iteratorKey);
		if nextKey ~= nil then
			iteratorKey = nextKey;
			return nextKey;
		end

		if iteratorIndex < count then
			iteratorIndex = iteratorIndex + 1;
			currentIterator, currentTable, iteratorKey = iteratorCallbackArray[iteratorIndex]();
			return AdvanceIterators();
		end

		currentIterator = nil;
		return nil;
	end

	return AdvanceIterators;
end

function IteratePools(...)
	local callbackArray = {};
	for i = 1, select("#", ...) do
		local pool = select(i, ...);
		table.insert(callbackArray, GenerateClosure(pool.EnumerateActive, pool));
	end

	return CompositeIterator(callbackArray);
end

function IterateTables(iteratorFunction, ...)
	local callbackArray = {};
	for i = 1, select("#", ...) do
		local tbl = select(i, ...);
		table.insert(callbackArray, GenerateClosure(iteratorFunction, tbl));
	end

	return CompositeIterator(callbackArray);
end

local s_passThroughClosureGenerators = {
	function(f) return function(...) return f(...); end; end,
	function(f, a) return function(...) return f(a, ...); end; end,
	function(f, a, b) return function(...) return f(a, b, ...); end; end,
	function(f, a, b, c) return function(...) return f(a, b, c, ...); end; end,
	function(f, a, b, c, d) return function(...) return f(a, b, c, d, ...); end; end,
	function(f, a, b, c, d, e) return function(...) return f(a, b, c, d, e, ...); end; end,
};

local s_flatClosureGenerators = {
	function(f) return function() return f(); end; end,
	function(f, a) return function() return f(a); end; end,
	function(f, a, b) return function() return f(a, b); end; end,
	function(f, a, b, c) return function() return f(a, b, c); end; end,
	function(f, a, b, c, d) return function() return f(a, b, c, d); end; end,
	function(f, a, b, c, d, e) return function() return f(a, b, c, d, e); end; end,
};

local function GenerateClosureInternal(generatorArray, f, ...)
	local count = select("#", ...);
	local generator = generatorArray[count + 1];
	if generator then
		return generator(f, ...);
	end

	assertsafe(false, "Closure generation does not support more than " .. (#generatorArray - 1) .. " parameters");
	return nil;
end

-- Syntactic sugar for function(...) return f(a, b, c, ...); end
function GenerateClosure(f, ...)
	return GenerateClosureInternal(s_passThroughClosureGenerators, f, ...);
end

-- Generates a closure with the specified arguments that will ignore extra arguments when called later. Useful for passing
-- through callbacks to APIs where we don't want extra arguments to be passed through, i.e. simple OnClick scripts.
-- This is equivalent to: function() return f(a, b, c); end INSTEAD OF function(...) return f(a, b, c, ...); end
function GenerateFlatClosure(f, ...)
	return GenerateClosureInternal(s_flatClosureGenerators, f, ...);
end

function RunNextFrame(callback)
	C_Timer.After(0, callback);
end

FunctionUtil = {};

function FunctionUtil.SafeInvokeMethod(table, methodName, ...)
	local method = table[methodName];
	if method then
		return method(table, ...);
	end

	return nil;
end
