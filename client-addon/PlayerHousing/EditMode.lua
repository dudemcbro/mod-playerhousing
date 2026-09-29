-- Edit mode: while it's on, keys work on the selected piece. Click a piece (or press Tab),
-- then: arrows slide it, the mouse wheel turns it, Page Up and Page Down raise and lower it,
-- Shift makes any of those finer, G lets it follow the mouse with the targeting circle,
-- Delete picks it up, Ctrl+Z and Ctrl+Y undo and redo, Escape leaves edit mode.
--
-- The keys are override bindings, active only in edit mode, so the usual ones come back
-- afterwards. Bindings can't change in combat; they wait for it to end.

local API = PlayerHousingAPI
local HOLD_DELAY, HOLD_REPEAT, HOLD_LIMIT = 0.35, 0.08, 5   -- seconds

local owner = CreateFrame("Frame", "PlayerHousingEditKeys", UIParent)
local bound = false
local held = {}   -- action -> { next = time of the next step, stop = time it gives up }
local hud, hudName, hudHelp

-- Keys, with the actions they press. Shift, Ctrl and Alt are read when the key goes down.
local KEYS = {
    UP = "Forward", DOWN = "Back", LEFT = "Left", RIGHT = "Right", PAGEUP = "Raise", PAGEDOWN = "Lower",
    MOUSEWHEELUP = "WheelUp", MOUSEWHEELDOWN = "WheelDown",
    TAB = "Next", DELETE = "PickUp", ["CTRL-Z"] = "Undo", ["CTRL-Y"] = "Redo", ["CTRL-SHIFT-Z"] = "Redo", ESCAPE = "Done",
}
for _, key in ipairs({ "UP", "DOWN", "LEFT", "RIGHT", "PAGEUP", "PAGEDOWN", "MOUSEWHEELUP", "MOUSEWHEELDOWN", "TAB" }) do
    KEYS["SHIFT-" .. key] = KEYS[key]
end
for _, key in ipairs({ "MOUSEWHEELUP", "MOUSEWHEELDOWN" }) do
    KEYS["CTRL-" .. key] = KEYS[key]
    KEYS["CTRL-SHIFT-" .. key] = KEYS[key]
    KEYS["ALT-" .. key] = KEYS[key]
end
local FOLLOW_KEY = "G"   -- clicks the addon's secure spot button, which uses Move a Piece

local function Step(action)
    local state = API.state
    local fine = IsShiftKeyDown()
    local flat = state.grid > 0 and state.grid or (fine and 0.05 or 0.25)
    local rise = fine and 0.02 or 0.1
    if action == "Forward" then API.Shift(flat, 0, 0, 0)
    elseif action == "Back" then API.Shift(-flat, 0, 0, 0)
    elseif action == "Left" then API.Shift(0, flat, 0, 0)
    elseif action == "Right" then API.Shift(0, -flat, 0, 0)
    elseif action == "Raise" then API.Shift(0, 0, rise, 0)
    elseif action == "Lower" then API.Shift(0, 0, -rise, 0)
    end
end

local HELD = { Forward = true, Back = true, Left = true, Right = true, Raise = true, Lower = true }

local function Wheel(delta)
    -- Alt, or nothing selected: the wheel zooms the camera as usual.
    if IsAltKeyDown() or API.state.selected == 0 then
        if delta > 0 then CameraZoomIn(1) else CameraZoomOut(1) end
    elseif IsControlKeyDown() then
        API.Shift(0, 0, delta * (IsShiftKeyDown() and 0.02 or 0.1), 0)
    else
        API.Shift(0, 0, 0, delta * (IsShiftKeyDown() and 5 or 15))
    end
end

local ONCE = {
    WheelUp = function() Wheel(1) end,
    WheelDown = function() Wheel(-1) end,
    Next = function() API.Command(IsShiftKeyDown() and "select previous" or "select next") end,
    PickUp = function() API.PickUp(false) end,
    Undo = function() API.Command("undo") end,
    Redo = function() API.Command("redo") end,
    Done = function()
        if SpellIsTargeting() then
            SpellStopTargeting()
        else
            API.Command("edit off")
        end
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
    for action, hold in pairs(held) do
        if now >= hold.stop then
            held[action] = nil
        elseif now >= hold.next then
            Step(action)
            hold.next = now + HOLD_REPEAT
        end
    end
end)

local function CreateHud()
    hud = CreateFrame("Frame", "PlayerHousingEditHud", UIParent)
    hud:SetWidth(460)
    hud:SetHeight(78)
    hud:SetPoint("TOP", UIParent, "TOP", 0, -96)
    hud:SetFrameStrata("HIGH")
    hud:SetBackdrop({
        bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        tile = true, tileSize = 16, edgeSize = 16,
        insets = { left = 4, right = 4, top = 4, bottom = 4 },
    })
    hud:SetBackdropColor(0, 0, 0, 0.75)

    local title = hud:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    title:SetPoint("TOPLEFT", 10, -8)
    title:SetText("Edit mode")

    hudName = hud:CreateFontString("PlayerHousingEditHudName", "OVERLAY", "GameFontHighlight")
    hudName:SetPoint("TOPLEFT", title, "TOPRIGHT", 10, 0)
    hudName:SetPoint("RIGHT", hud, "RIGHT", -10, 0)
    hudName:SetJustifyH("LEFT")

    hudHelp = hud:CreateFontString("PlayerHousingEditHudHelp", "OVERLAY", "GameFontHighlightSmall")
    hudHelp:SetPoint("TOPLEFT", 10, -28)
    hudHelp:SetPoint("RIGHT", hud, "RIGHT", -10, 0)
    hudHelp:SetJustifyH("LEFT")
    hud:Hide()
end

local function UpdateHud()
    local state = API.state
    if not state.editMode then
        hud:Hide()
        return
    end
    if state.selected > 0 then
        hudName:SetText(state.selectedName)
    else
        hudName:SetText("|cffa0a0a0Right-click a piece, or press Tab|r")
    end
    local grid = state.grid > 0 and ("   Grid: " .. state.grid .. " yd") or ""
    hudHelp:SetText("Arrows: slide   Wheel: turn   Page Up/Down: raise, lower   Shift: finer" .. grid .. "\n" ..
        "Tab: next piece   G: follow the mouse   Delete: pick up   Ctrl+Z/Y: undo, redo\n" ..
        "Ctrl+wheel: raise, lower   Alt+wheel: zoom   Escape: done")
    hud:Show()
end

-- Keys follow the server's word on edit mode, except in combat, when bindings can't change.
local function SyncKeys()
    local want = API.state.editMode and API.CanEdit()
    if want == bound or InCombatLockdown() then
        return
    end
    if want then
        for key, action in pairs(KEYS) do
            SetOverrideBindingClick(owner, true, key, "PlayerHousingEdit" .. action)
        end
        SetOverrideBindingClick(owner, true, FOLLOW_KEY, "PlayerHousingSpotButton")
    else
        ClearOverrideBindings(owner)
        wipe(held)
    end
    bound = want
end

API.OnState(function()
    if not hud then
        CreateHud()
    end
    if not API.state.editMode then
        wipe(held)
    end
    UpdateHud()
    SyncKeys()
end)

owner:RegisterEvent("PLAYER_REGEN_ENABLED")
owner:SetScript("OnEvent", SyncKeys)
