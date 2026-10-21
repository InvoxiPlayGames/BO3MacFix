-- BO3MacFix's stand-in for T7Overcharged, a Windows-only DLL that workshop mods
-- (e.g. All-around Enhancement) load for extra Lua libraries.
-- BO3MacFix runs this when a mod loads the DLL, passing in a few native helpers.
local nativeCreateDirectory, nativeDirectoryExists, nativeClipboardGet, nativeClipboardSet, nativePrint = ...

if EnableGlobals then
    EnableGlobals()
end

local function noop()
end

local function log(message)
    nativePrint("[T7Overcharged] " .. tostring(message))
end

-- fills in a library table, leaving anything that already exists alone
local function provide(library, members)
    for name, implementation in pairs(members) do
        if library[name] == nil then
            library[name] = implementation
        end
    end
    -- functions we don't know about do nothing rather than erroring
    if getmetatable(library) == nil then
        setmetatable(library, { __index = function() return noop end })
    end
    return library
end

local function isSet(value)
    return value ~= nil and value ~= false and value ~= 0 and value ~= "0" and value ~= "false"
end

local function setup()
    local io = require("io")

    local function fileExists(path)
        local file = io.open(tostring(path), "rb")
        if file then
            file:close()
            return true
        end
        return false
    end

    local function readFile(path)
        local file = io.open(tostring(path), "rb")
        if not file then
            return nil
        end
        local contents = file:read("*a")
        file:close()
        return contents
    end

    local function writeFile(path, contents, append)
        local file = io.open(tostring(path), append and "ab" or "wb")
        if not file then
            return false
        end
        file:write(tostring(contents or ""))
        file:close()
        return true
    end

    FileIO = provide(FileIO or {}, {
        FileExists = fileExists,
        ReadFile = readFile,
        WriteFile = function(path, contents, append)
            return writeFile(path, contents, isSet(append))
        end,
        -- like Windows' CopyFile, the third argument means "fail if the destination exists"
        Copy = function(source, destination, failIfExists)
            if isSet(failIfExists) and fileExists(destination) then
                return false
            end
            local contents = readFile(source)
            if contents == nil then
                return false
            end
            return writeFile(destination, contents, false)
        end,
        FileSize = function(path)
            local file = io.open(tostring(path), "rb")
            if not file then
                return 0
            end
            local size = file:seek("end")
            file:close()
            return size or 0
        end,
        DirectoryExists = function(path)
            return nativeDirectoryExists(tostring(path))
        end,
        CreateDirectory = function(path)
            return nativeCreateDirectory(tostring(path))
        end,
        ClipboardGet = function()
            return nativeClipboardGet()
        end,
        ClipboardSet = function(text)
            nativeClipboardSet(tostring(text or ""))
        end,
        Wine = function()
            return false
        end,
    })

    Console = provide(Console or {}, {
        Print = function(...)
            local parts = {}
            for i = 1, select("#", ...) do
                parts[i] = tostring((select(i, ...)))
            end
            log(table.concat(parts, " "))
        end,
        PrintFile = function(name, ...)
            Console.Print(...)
        end,
        ShowExternalConsole = noop,
    })

    -- no network or Discord support, so requests never return anything
    Axios = provide(Axios or {}, {})
    DiscordRPC = provide(DiscordRPC or {}, {
        IsAvailable = function()
            return false
        end,
    })
    MainLUA = provide(MainLUA or {}, {})
    winOs = provide(winOs or {}, {})
    T7Overcharged = provide(T7Overcharged or {}, {})
    UIErrorHash = provide(UIErrorHash or {}, {})
    HotReload = provide(HotReload or {}, {})

    -- functions mods call from the Windows game's newer UI scripts that the Mac build never got
    if Engine then
        local function returnsEmptyTable()
            return {}
        end
        for name, implementation in pairs({
            PartyHostClearUIState = noop,
            PartyHostReassignTeams = noop,
            PartyHostSetUIState = noop,
            PartyHostToggleStart = noop,
            GetRegulations = returnsEmptyTable,
            GetUserRegulationPreference = returnsEmptyTable,
            SetModel = Engine.CreateModel,
        }) do
            if Engine[name] == nil then
                Engine[name] = implementation
            end
        end
    end
end

local ok, err = pcall(setup)
if ok then
    log("loaded")
else
    log("failed to load: " .. tostring(err))
end
