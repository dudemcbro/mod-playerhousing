-- The mouse while a piece is held (on the mouse, or following you until it's set down): the
-- plain wheel zooms the camera as ever; every change to the piece is the wheel with a modifier:
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

-- With the mouse: the wheel, the middle button and Escape. Following you (no DLL): the arrows
-- and G as well.
local KEYS = {
    ["SHIFT-MOUSEWHEELUP"] = "WheelUp", ["SHIFT-MOUSEWHEELDOWN"] = "WheelDown",
    ["CTRL-MOUSEWHEELUP"] = "RaiseWheelUp", ["CTRL-MOUSEWHEELDOWN"] = "RaiseWheelDown",
    ["CTRL-SHIFT-MOUSEWHEELUP"] = "FineLeft", ["CTRL-SHIFT-MOUSEWHEELDOWN"] = "FineRight",
    ["ALT-MOUSEWHEELUP"] = "TiltForward", ["ALT-MOUSEWHEELDOWN"] = "TiltBack",
    ["ALT-SHIFT-MOUSEWHEELUP"] = "TiltRight", ["ALT-SHIFT-MOUSEWHEELDOWN"] = "TiltLeft",
    ["ALT-CTRL-MOUSEWHEELUP"] = "Bigger", ["ALT-CTRL-MOUSEWHEELDOWN"] = "Smaller",
    BUTTON3 = "Straight", ["SHIFT-BUTTON3"] = "Grid", ["CTRL-BUTTON3"] = "NormalSize",
    ESCAPE = "Cancel",
}
local TILT_STEP, GRID_SIZES = 15, { 0, 0.25, 0.5, 1, 2 }
local FOLLOW_KEYS = {
    UP = "Forward", DOWN = "Back", LEFT = "Left", RIGHT = "Right", G = "SetDown", ["SHIFT-G"] = "SetDown",
}

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

-- Keys follow the server's word on what's held, except in combat, when bindings can't change.
local function SyncKeys()
    local want = API.state.ghostItem > 0 and API.CanEdit()
    if want == bound or InCombatLockdown() then
        return
    end
    if want then
        for key, action in pairs(KEYS) do
            SetOverrideBindingClick(owner, true, key, "PlayerHousingEdit" .. action)
        end
        if not API.HasMouse() then
            for key, action in pairs(FOLLOW_KEYS) do
                SetOverrideBindingClick(owner, true, key, "PlayerHousingEdit" .. action)
            end
        end
    else
        ClearOverrideBindings(owner)
        wipe(held)
    end
    bound = want
end

API.OnState(function()
    if API.state.ghostItem == 0 then
        wipe(held)
    end
    SyncKeys()
end)

owner:RegisterEvent("PLAYER_REGEN_ENABLED")
owner:SetScript("OnEvent", SyncKeys)
