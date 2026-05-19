-- Stock 3.3.5 returns (localizedName, fileName) from UnitClass — no classID.
-- UnitClassBase is a Wrath-era / private-server extension; WrathClassicAPI.dll
-- is expected to register it, but we degrade gracefully so this file still
-- loads without the DLL.
local className, classFile = UnitClass('player')
local classID
if UnitClassBase then
	classFile, classID = UnitClassBase('player')
end

PlayerUtil = PlayerUtil or {}

function PlayerUtil.GetClassID()
    return classID
end

function PlayerUtil.GetClassName()
	return className
end

function PlayerUtil.GetClassFile()
	return classFile
end

function PlayerUtil.GetClassInfo()
	return {
        classID = classID,
        classFile = classFile,
        className = className
    };
end

function PlayerUtil.GetClassColor()
    local r, g, b = GetClassColor(PlayerUtil.GetClassFile())
	return CreateColor(r, g, b);
end

function PlayerUtil.GetCurrentSpeed()
    return GetUnitSpeed("player") / BASE_MOVEMENT_SPEED * 100
end

function GetPlayerGuid()
	return UnitGUID("player");
end

function IsPlayerGuid(guid)
	return guid == GetPlayerGuid();
end

function GetNameAndServerNameFromGUID(unitGUID)
	local _, _, _, _, _, name = GetPlayerInfoByGUID(unitGUID);
	return name, GetRealmName()
end
