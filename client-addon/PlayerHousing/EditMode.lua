-- The mouse while a piece is held (on the mouse, or following you until it's set down): the
-- plain wheel zooms the camera as ever; every change to the piece is, by default, the wheel
-- with a modifier (each can be changed under Interface > AddOns > Player Housing, Options.lua):
--
--   Shift+wheel        turn it                 Alt+wheel          tilt it forward or back
--   Ctrl+wheel         raise or lower it       Alt+Shift+wheel    tilt it to its side
--   Ctrl+Shift+wheel   turn it finely (1 deg)  Ctrl+Alt+wheel     bigger or smaller
--   middle-click       stand it straight       Shift+middle-click the grid: off, 1/4, 1/2, 1, 2 yd
--   Ctrl+middle-click  normal size
--
-- (Binding names put the modifiers in the game's order: ALT-CTRL-SHIFT-.)
--
-- Left-click sets it down, right-click or Escape puts it back, Shift+right-click puts it away
-- in the Collection (Mouse.lua). Without PlayerHousing.dll it follows you instead, and the
-- arrows push it farther, nearer and to the sides, and G sets it down (Shift+G: then another).
--
-- While decorating (the window open on your island), the server also hears when Ctrl goes
-- down and up: a right-click on a piece with Ctrl held adds it to the selection (or takes it
-- out) instead of picking it up.
--
-- The keys are override bindings, active only while a piece is held, so the usual ones come
-- back afterwards. Bindings can't change in combat; they wait for it to end.

local API = PlayerHousingAPI
local HOLD_DELAY, HOLD_REPEAT, HOLD_LIMIT = 0.35, 0.08, 5   -- seconds

local owner = CreateFrame("Frame", "PlayerHousingEditKeys", UIParent)
local bound = false
local held = {}   -- action -> { next = time of the next step, stop = time it gives up }
local ctrlSent = false      -- what the server last heard about Ctrl

-- Every key, in the order the options page lists them (Options.lua): the action, what it does,
-- its default. "follow" ones are bound only without PlayerHousing.dll. A player's own keys are
-- in PlayerHousingDB.keys (action -> key, "" for none); the rest keep their defaults.
local ACTIONS = {
    { "WheelUp", "Turn left", "SHIFT-MOUSEWHEELUP" },
    { "WheelDown", "Turn right", "SHIFT-MOUSEWHEELDOWN" },
    { "FineLeft", "Turn left finely (1\194\176)", "CTRL-SHIFT-MOUSEWHEELUP" },
    { "FineRight", "Turn right finely (1\194\176)", "CTRL-SHIFT-MOUSEWHEELDOWN" },
    { "RaiseWheelUp", "Raise", "CTRL-MOUSEWHEELUP" },
    { "RaiseWheelDown", "Lower", "CTRL-MOUSEWHEELDOWN" },
    { "TiltForward", "Tilt forward", "ALT-MOUSEWHEELUP" },
    { "TiltBack", "Tilt back", "ALT-MOUSEWHEELDOWN" },
    { "TiltRight", "Tilt to the right", "ALT-SHIFT-MOUSEWHEELUP" },
    { "TiltLeft", "Tilt to the left", "ALT-SHIFT-MOUSEWHEELDOWN" },
    { "Bigger", "Bigger", "ALT-CTRL-MOUSEWHEELUP" },
    { "Smaller", "Smaller", "ALT-CTRL-MOUSEWHEELDOWN" },
    { "Straight", "Stand it straight", "BUTTON3" },
    { "NormalSize", "Normal size", "CTRL-BUTTON3" },
    { "Grid", "Grid: off, 1/4, 1/2, 1, 2 yd", "SHIFT-BUTTON3" },
    { "Cancel", "Never mind (put it back)", "ESCAPE" },
    { "Forward", "Farther", "UP", follow = true },
    { "Back", "Nearer", "DOWN", follow = true },
    { "Left", "To the left", "LEFT", follow = true },
    { "Right", "To the right", "RIGHT", follow = true },
    { "SetDown", "Set it down (with Shift: and another)", "G", follow = true },
}
local ACTION = {}
for _, entry in ipairs(ACTIONS) do
    ACTION[entry[1]] = entry
end

-- A key's modifiers and the rest; and back, in the game's order (ALT-CTRL-SHIFT-), or the
-- binding never fires.
local MODIFIERS = { "ALT", "CTRL", "SHIFT" }
local function SplitKey(key)
    local mods, rest = {}, key
    for _, mod in ipairs(MODIFIERS) do
        if rest:sub(1, #mod + 1) == mod .. "-" and #rest > #mod + 1 then
            mods[mod], rest = true, rest:sub(#mod + 2)
        end
    end
    return mods, rest
end
local function JoinKey(mods, base)
    return (mods.ALT and "ALT-" or "") .. (mods.CTRL and "CTRL-" or "") .. (mods.SHIFT and "SHIFT-" or "") .. base
end

local function KeyFor(action)
    local keys = PlayerHousingDB and PlayerHousingDB.keys
    local key = keys and keys[action]
    if key == nil then
        key = ACTION[action][3]
    end
    return key
end

-- SetDown with Shift places another: its key with Shift as well (none if it has Shift already).
local function ShiftOf(key)
    local mods, base = SplitKey(key)
    if key == "" or mods.SHIFT then
        return nil
    end
    mods.SHIFT = true
    return JoinKey(mods, base)
end
local TILT_STEP, GRID_SIZES = 15, { 0, 0.25, 0.5, 1, 2 }

local function Step(action)
    local flat = API.state.grid > 0 and API.state.grid or 0.25
    if action == "Forward" then API.Shift(flat, 0, 0, 0)
    elseif action == "Back" then API.Shift(-flat, 0, 0, 0)
    elseif action == "Left" then API.Shift(0, flat, 0, 0)
    elseif action == "Right" then API.Shift(0, -flat, 0, 0)
    end
end

local HELD = { Forward = true, Back = true, Left = true, Right = true }

local ONCE = {
    WheelUp = function() API.Shift(0, 0, 0, 15) end,
    WheelDown = function() API.Shift(0, 0, 0, -15) end,
    RaiseWheelUp = function() API.Shift(0, 0, 0.1, 0) end,
    RaiseWheelDown = function() API.Shift(0, 0, -0.1, 0) end,
    FineLeft = function() API.Shift(0, 0, 0, 1) end,
    FineRight = function() API.Shift(0, 0, 0, -1) end,
    TiltForward = function() API.Command("tilt forward " .. TILT_STEP) end,
    TiltBack = function() API.Command("tilt back " .. TILT_STEP) end,
    TiltRight = function() API.Command("tilt right " .. TILT_STEP) end,
    TiltLeft = function() API.Command("tilt left " .. TILT_STEP) end,
    Bigger = function() API.Command("size bigger") end,
    Smaller = function() API.Command("size smaller") end,
    Straight = function() API.Command("tilt straight") end,
    NormalSize = function() API.Command("size normal") end,
    Grid = function()
        local current, nextSize = API.state.grid, GRID_SIZES[2]
        for index, size in ipairs(GRID_SIZES) do
            if math.abs(size - current) < 0.01 then
                nextSize = GRID_SIZES[index % #GRID_SIZES + 1]
            end
        end
        API.Command(nextSize == 0 and "grid off" or ("grid " .. nextSize))
    end,
    Cancel = function() API.Command("ghost cancel") end,
    SetDown = function()
        local another = IsShiftKeyDown() and not API.state.ghostMove
        API.Command(another and "ghost place another" or "ghost place")
    end,
}

local function Press(action, down)
    if HELD[action] then
        if down then
            Step(action)
            local now = GetTime()
            held[action] = { next = now + HOLD_DELAY, stop = now + HOLD_LIMIT }
        else
            held[action] = nil
        end
    elseif down ~= false then
        ONCE[action]()
    end
end

-- One hidden button per action; held keys want both the press and the release.
for action in pairs(HELD) do
    local button = CreateFrame("Button", "PlayerHousingEdit" .. action, owner)
    button:RegisterForClicks("AnyDown", "AnyUp")
    button:SetScript("OnClick", function(self, mouseButton, down) Press(action, down) end)
end
for action in pairs(ONCE) do
    local button = CreateFrame("Button", "PlayerHousingEdit" .. action, owner)
    button:RegisterForClicks("AnyDown")
    button:SetScript("OnClick", function(self, mouseButton, down) Press(action, down) end)
end

owner:SetScript("OnUpdate", function(self, elapsed)
    local now = GetTime()
    -- While decorating, Ctrl down means a right-click adds the piece to the selection (or takes
    -- it out): the click itself reaches the server without the key.
    local ctrl = API.state.decorating and API.CanEdit() and IsControlKeyDown() and true or false
    if ctrl ~= ctrlSent then
        ctrlSent = ctrl
        API.Command(ctrl and "group hold on" or "group hold off")
    end
    for action, hold in pairs(held) do
        if now >= hold.stop then
            held[action] = nil
        elseif now >= hold.next then
            Step(action)
            hold.next = now + HOLD_REPEAT
        end
    end
end)

-- Keys follow the server's word on what's held, except in combat, when bindings can't change;
-- a changed key (the options page) binds again the same way.
local dirty = false
local function SyncKeys()
    local want = API.state.ghostItem > 0 and API.CanEdit()
    if (want == bound and not dirty) or InCombatLockdown() then
        return
    end
    ClearOverrideBindings(owner)
    if want then
        local follow = not API.HasMouse()
        -- SetDown's Shift key first, so a key of another action's own wins over it.
        local another = follow and ShiftOf(KeyFor("SetDown"))
        if another then
            SetOverrideBindingClick(owner, true, another, "PlayerHousingEditSetDown")
        end
        for _, entry in ipairs(ACTIONS) do
            local key = KeyFor(entry[1])
            if key ~= "" and (follow or not entry.follow) then
                SetOverrideBindingClick(owner, true, key, "PlayerHousingEdit" .. entry[1])
            end
        end
    else
        wipe(held)
    end
    bound, dirty = want, false
end

-- For Options.lua and the held panel.
API.KEY_ACTIONS = ACTIONS
API.SplitKey, API.JoinKey, API.KeyFor = SplitKey, JoinKey, KeyFor
API.ShiftKeyFor = function(action) return ShiftOf(KeyFor(action)) end

local function KeysChanged()
    dirty = true
    SyncKeys()
    if API.RefreshHeld then
        API.RefreshHeld()
    end
end

-- A new key for an action ("" for none). An action that had the key loses it; its name is
-- returned.
API.SetKey = function(action, key)
    PlayerHousingDB = PlayerHousingDB or {}
    PlayerHousingDB.keys = PlayerHousingDB.keys or {}
    local keys, taken = PlayerHousingDB.keys, nil
    if key ~= "" then
        for _, entry in ipairs(ACTIONS) do
            if entry[1] ~= action and KeyFor(entry[1]) == key then
                keys[entry[1]], taken = "", entry[1]
            end
        end
    end
    keys[action] = (key ~= ACTION[action][3]) and key or nil
    if next(keys) == nil then
        PlayerHousingDB.keys = nil
    end
    KeysChanged()
    return taken
end

API.ResetKeys = function()
    if PlayerHousingDB then
        PlayerHousingDB.keys = nil
    end
    KeysChanged()
end

-- How a key reads: "Ctrl+Shift+wheel up", "Middle-click", "G"; nil for none.
local KEY_NAMES = {
    MOUSEWHEELUP = "wheel up", MOUSEWHEELDOWN = "wheel down", BUTTON3 = "middle-click",
    BUTTON4 = "mouse button 4", BUTTON5 = "mouse button 5", ESCAPE = "Esc", SPACE = "Space",
    UP = "Up arrow", DOWN = "Down arrow", LEFT = "Left arrow", RIGHT = "Right arrow",
    ENTER = "Enter", TAB = "Tab", BACKSPACE = "Backspace", DELETE = "Delete", INSERT = "Insert",
    HOME = "Home", END = "End", PAGEUP = "Page Up", PAGEDOWN = "Page Down",
}
local function Capital(text)
    return text:sub(1, 1):upper() .. text:sub(2)
end
API.KeyText = function(key)
    if not key or key == "" then
        return nil
    end
    local mods, base = SplitKey(key)
    local name = KEY_NAMES[base] or base
    local text = (mods.CTRL and "Ctrl+" or "") .. (mods.ALT and "Alt+" or "") .. (mods.SHIFT and "Shift+" or "") .. name
    return Capital(text)
end
-- Two actions on the wheel's two ways with the same modifiers read as one: "Shift+wheel".
API.WheelPairText = function(up, down)
    local upKey, downKey = KeyFor(up), KeyFor(down)
    local upMods, upBase = SplitKey(upKey)
    local downMods, downBase = SplitKey(downKey)
    if upBase == "MOUSEWHEELUP" and downBase == "MOUSEWHEELDOWN" and JoinKey(upMods, "") == JoinKey(downMods, "") then
        return Capital(API.KeyText(JoinKey(upMods, "X")):sub(1, -2) .. "wheel")
    end
    local parts = {}
    parts[#parts + 1] = API.KeyText(upKey)
    parts[#parts + 1] = API.KeyText(downKey)
    return #parts > 0 and table.concat(parts, ", ") or nil
end

API.OnState(function()
    if API.state.ghostItem == 0 then
        wipe(held)
    end
    SyncKeys()
end)

owner:RegisterEvent("PLAYER_REGEN_ENABLED")
owner:SetScript("OnEvent", SyncKeys)
