-- Edit mode: while it's on, keys work on the selected piece. Click a piece (or press Tab),
-- then: arrows slide it, the mouse wheel turns it, Ctrl+wheel and Page Up and Page Down raise
-- and lower it, Shift makes any of those finer, R makes the plain wheel raise instead, G
-- moves it with the targeting circle, Delete picks it up, Ctrl+Z and Ctrl+Y undo and redo,
-- Escape leaves edit mode. The banner says what each key just did.
--
-- The keys are override bindings, active only in edit mode, so the usual ones come back
-- afterwards. Bindings can't change in combat; they wait for it to end.

local API = PlayerHousingAPI
local HOLD_DELAY, HOLD_REPEAT, HOLD_LIMIT = 0.35, 0.08, 5   -- seconds

local owner = CreateFrame("Frame", "PlayerHousingEditKeys", UIParent)
local bound = false
local held = {}   -- action -> { next = time of the next step, stop = time it gives up }
local hud, hudName, hudHelp, hudLast, gridButton, wheelButton, undoButton, redoButton
local wheelRaises = false   -- R: the plain wheel raises and lowers instead of turning
local lastShown = 0         -- when the banner's last line was set
local GRID_SIZES = { 0, 0.25, 0.5, 1, 2 }

-- Keys, with the actions they press. Each wheel binding has its own action, so what it does
-- doesn't hang on reading Ctrl as the wheel turns; Shift is read when the key goes down.
local KEYS = {
    UP = "Forward", DOWN = "Back", LEFT = "Left", RIGHT = "Right", PAGEUP = "Raise", PAGEDOWN = "Lower",
    MOUSEWHEELUP = "WheelUp", MOUSEWHEELDOWN = "WheelDown",
    ["CTRL-MOUSEWHEELUP"] = "RaiseWheelUp", ["CTRL-MOUSEWHEELDOWN"] = "RaiseWheelDown",
    ["CTRL-SHIFT-MOUSEWHEELUP"] = "RaiseWheelUp", ["CTRL-SHIFT-MOUSEWHEELDOWN"] = "RaiseWheelDown",
    ["ALT-MOUSEWHEELUP"] = "ZoomWheelUp", ["ALT-MOUSEWHEELDOWN"] = "ZoomWheelDown",
    TAB = "Next", DELETE = "PickUp", ["CTRL-Z"] = "Undo", ["CTRL-Y"] = "Redo", ["CTRL-SHIFT-Z"] = "Redo", ESCAPE = "Done",
    R = "WheelMode",
}
for _, key in ipairs({ "UP", "DOWN", "LEFT", "RIGHT", "PAGEUP", "PAGEDOWN", "MOUSEWHEELUP", "MOUSEWHEELDOWN", "TAB" }) do
    KEYS["SHIFT-" .. key] = KEYS[key]
end
local FOLLOW_KEY = "G"   -- clicks the addon's secure spot button, which uses Move a Piece

local KEY_NAMES = {
    Forward = "Up arrow", Back = "Down arrow", Left = "Left arrow", Right = "Right arrow", Raise = "Page Up", Lower = "Page Down",
}

-- The banner's last line: what the key just pressed did, so a key that does the wrong thing
-- shows itself.
local function Feedback(key, what)
    if hudLast then
        hudLast:SetText("|cffffd000" .. key .. ":|r " .. what)
        lastShown = GetTime()
    end
end

local function Yd(value)
    return (("%.2f"):format(math.abs(value)):gsub("%.?0+$", "")) .. " yd"
end

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
    local amount = (action == "Raise" or action == "Lower") and rise or flat
    Feedback((fine and "Shift+" or "") .. KEY_NAMES[action], ("%s %s"):format(action == "Raise" and "raised" or action == "Lower" and "lowered"
        or ("moved " .. action:lower()), Yd(amount)))
end

local HELD = { Forward = true, Back = true, Left = true, Right = true, Raise = true, Lower = true }

-- how: "raise" or "zoom" from their own bindings; the plain wheel turns (or raises, after R),
-- unless Ctrl or Alt reads as held.
local function Wheel(delta, how)
    local key = "Wheel " .. (delta > 0 and "up" or "down")
    local fine = IsShiftKeyDown()
    if how == "raise" then
        key = "Ctrl+" .. key
    elseif how == "zoom" then
        key = "Alt+" .. key
    elseif IsAltKeyDown() then
        how, key = "zoom", key .. " (Alt held)"
    elseif IsControlKeyDown() then
        how, key = "raise", key .. " (Ctrl held)"
    else
        how = wheelRaises and "raise" or "turn"
    end
    if fine then
        key = "Shift+" .. key
    end

    if how == "zoom" or API.state.selected == 0 then
        if delta > 0 then CameraZoomIn(1) else CameraZoomOut(1) end
        Feedback(key, API.state.selected == 0 and how ~= "zoom" and "camera zoom (nothing selected)" or "camera zoom")
    elseif how == "raise" then
        local yards = delta * (fine and 0.02 or 0.1)
        API.Shift(0, 0, yards, 0)
        Feedback(key, (yards > 0 and "raised " or "lowered ") .. Yd(yards))
    else
        local degrees = delta * (fine and 5 or 15)
        API.Shift(0, 0, 0, degrees)
        Feedback(key, ("turned %s %d degrees"):format(degrees > 0 and "left" or "right", math.abs(degrees)))
    end
end

local UpdateHud

local function SetWheelRaises(on)
    wheelRaises = on
    Feedback("R", on and "the wheel raises and lowers now (R again: turns)" or "the wheel turns now")
    UpdateHud()
end

local function CycleGrid()
    local current, nextSize = API.state.grid, GRID_SIZES[1]
    for index, size in ipairs(GRID_SIZES) do
        if math.abs(size - current) < 0.01 then
            nextSize = GRID_SIZES[index % #GRID_SIZES + 1]
        end
    end
    API.Command(nextSize == 0 and "grid off" or ("grid " .. nextSize))
end

local ONCE = {
    WheelUp = function() Wheel(1) end,
    WheelDown = function() Wheel(-1) end,
    RaiseWheelUp = function() Wheel(1, "raise") end,
    RaiseWheelDown = function() Wheel(-1, "raise") end,
    ZoomWheelUp = function() Wheel(1, "zoom") end,
    ZoomWheelDown = function() Wheel(-1, "zoom") end,
    WheelMode = function() SetWheelRaises(not wheelRaises) end,
    Next = function()
        API.Command(IsShiftKeyDown() and "select previous" or "select next")
        Feedback(IsShiftKeyDown() and "Shift+Tab" or "Tab", IsShiftKeyDown() and "the piece before" or "the next piece")
    end,
    PickUp = function()
        API.PickUp(false)
        Feedback("Delete", "pick up")
    end,
    Undo = function()
        API.Command("undo")
        Feedback("Ctrl+Z", "undo")
    end,
    Redo = function()
        API.Command("redo")
        Feedback("Ctrl+Y", "redo")
    end,
    Done = function()
        if SpellIsTargeting() then
            SpellStopTargeting()
            Feedback("Escape", "the circle is gone; the piece stays")
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
    if hudLast and lastShown > 0 and now - lastShown > 6 then
        hudLast:SetText("")
        lastShown = 0
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

local function HudButton(name, text, width, onClick, tooltipTitle, tooltipText)
    local button = CreateFrame("Button", "PlayerHousingEditHud" .. name, hud, "UIPanelButtonTemplate")
    button:SetWidth(width)
    button:SetHeight(20)
    button:SetText(text)
    button:SetScript("OnClick", onClick)
    button:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_BOTTOM")
        GameTooltip:SetText(tooltipTitle)
        GameTooltip:AddLine(tooltipText, 1, 1, 1, true)
        GameTooltip:Show()
    end)
    button:SetScript("OnLeave", GameTooltip_Hide)
    return button
end

local function CreateHud()
    hud = CreateFrame("Frame", "PlayerHousingEditHud", UIParent)
    hud:SetWidth(470)
    hud:SetHeight(114)
    hud:EnableMouse(true)
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

    hudLast = hud:CreateFontString("PlayerHousingEditHudLast", "OVERLAY", "GameFontHighlightSmall")
    hudLast:SetPoint("TOPLEFT", 10, -68)
    hudLast:SetPoint("RIGHT", hud, "RIGHT", -10, 0)
    hudLast:SetJustifyH("LEFT")

    gridButton = HudButton("Grid", "Grid: off", 96, CycleGrid, "Grid",
        "Slides and moves land on a grid: off, a quarter yard, half, 1 or 2 yards. The arrow keys step a square at a time.")
    gridButton:SetPoint("TOPLEFT", 10, -86)
    wheelButton = HudButton("Wheel", "Wheel: turn", 96, function() SetWheelRaises(not wheelRaises) end, "What the wheel does",
        "Turn the piece, or raise and lower it (R does this too). Ctrl+wheel always raises and lowers.")
    wheelButton:SetPoint("LEFT", gridButton, "RIGHT", 4, 0)
    undoButton = HudButton("Undo", "Undo", 80, ONCE.Undo, "Undo", "Ctrl+Z")
    undoButton:SetPoint("LEFT", wheelButton, "RIGHT", 4, 0)
    redoButton = HudButton("Redo", "Redo", 80, ONCE.Redo, "Redo", "Ctrl+Y")
    redoButton:SetPoint("LEFT", undoButton, "RIGHT", 4, 0)
    local done = HudButton("Done", "Done", 80, function() API.Command("edit off") end, "Done", "Leaves edit mode. Escape does too.")
    done:SetPoint("LEFT", redoButton, "RIGHT", 4, 0)
    hud:Hide()
end

function UpdateHud()
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
    hudHelp:SetText("Arrows: slide   " .. (wheelRaises and "Wheel: raise, lower" or "Wheel: turn") ..
        "   Ctrl+wheel, Page Up/Down: raise, lower   Shift: finer\n" ..
        "Tab: next piece   G: move it with the mouse   R: " .. (wheelRaises and "wheel turns" or "wheel raises") ..
        "   Delete: pick up\n" ..
        "Ctrl+Z/Y: undo, redo   Alt+wheel: zoom   Escape: done")
    gridButton:SetText(state.grid > 0 and ("Grid: " .. state.grid .. " yd") or "Grid: off")
    wheelButton:SetText(wheelRaises and "Wheel: raise" or "Wheel: turn")
    if state.undo ~= "" then undoButton:Enable() else undoButton:Disable() end
    if state.redo ~= "" then redoButton:Enable() else redoButton:Disable() end
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

-- G: the spot button uses the Move a Piece item edit mode keeps ready.
API.OnWindow(function()
    PlayerHousingSpotButton:HookScript("PostClick", function(self)
        if not API.state.editMode then
            return
        end
        if SpellIsTargeting() then
            Feedback("G", "click the new spot (right-click or Escape: never mind)")
        elseif not self:GetAttribute("item") then
            Feedback("G", API.state.selected > 0 and "getting Move a Piece ready: press G again" or "select a piece first")
        end
    end)
end)

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
