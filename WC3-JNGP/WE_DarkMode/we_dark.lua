-- WE_DarkMode\we_dark.lua — Cargado automáticamente por NewGen_WE_Dark.exe vía bin\exehack.exe
-- No modifica we.lua, we.conf.lua ni tesh\config\styles.ini.

local _we_dark_done = false
local function _we_dark_attach(p)
    if (not _we_dark_done) and p and p ~= 0 and grim and grim.injectdll then
        _we_dark_done = true
        local dll = os.getenv("WE_DARKMODE_DLL") or "WE_DarkMode\\WE_DarkMode_JNGP.dll"
        grim.injectdll(p, dll)
    end
end

if grim and grim.exestarted then
    local _orig_exestarted = grim.exestarted
    grim.exestarted = function(p)
        _we_dark_attach(p)
        return _orig_exestarted(p)
    end
end

dofile("we.lua")
_we_dark_attach(proc)
