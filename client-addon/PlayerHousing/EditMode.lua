-- Edit mode: while it's on, keys work on the selected piece. Click a piece (or press Tab),
-- then: arrows slide it, the mouse wheel turns it, Ctrl+wheel and Page Up and Page Down raise
-- and lower it, Shift makes any of those finer, R makes the plain wheel raise instead, G
-- picks it up to move it, Delete picks it up for good, Ctrl+Z and Ctrl+Y undo and redo,
-- Escape leaves edit mode. The banner says what each key just did. Ctrl-click more pieces to
-- move them all together (the server hears when Ctrl goes down and up, while decorating).
--
-- A ghost (a piece being placed or moved, following you until it's set down) takes the same
-- keys, in or out of edit mode: arrows push it farther or nearer and to the sides, the wheel
-- turns it, Ctrl+wheel and Page Up/Down raise and lower it, G sets it down (Shift+G: then
-- another), Escape puts it back. With PlayerHousing.dll it follows the mouse instead, and a
-- click sets it down (Mouse.lua); the arrows nudge it from there.
--
-- The keys are override bindings, active only then, so the usual ones come back afterwards.
-- Bindings can't change in combat; they wait for it to end.

local API = PlayerHousingAPI
local HOLD_DELAY, HOLD_REPEAT, HOLD_LIMIT = 0.35, 0.08, 5   -- seconds

local owner = CreateFrame("Frame", "PlayerHousingEditKeys", UIParent)
local bound = false
local held = {}   -- action -> { next = time of the next step, stop = time it gives up }
local hud, hudTitle, hudName, hudHelp, hudLast, gridButton, wheelButton, undoButton, redoButton, doneButton
local groupHudButtons = {}
local ghostHudButtons = {}
local ctrlSent = false      -- what the server last heard about Ctrl
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
    R = "WheelMode", G = "Follow",
}
for _, key in ipairs({ "UP", "DOWN", "LEFT", "RIGHT", "PAGEUP", "PAGEDOWN", "MOUSEWHEELUP", "MOUSEWHEELDOWN", "TAB", "G" }) do
    KEYS["SHIFT-" .. key] = KEYS[key]
end

-- A piece following you, to place or move.
local function Ghosting()
    return API.state.ghostItem > 0
end

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

API.Feedback = Feedback

local function Yd(value)
    return (("%.2f"):format(math.abs(value)):gsub("%.?0+$", "")) .. " yd"
end

local function Step(action)
    local state = API.state
    local fine = IsShiftKeyDown()
    local flat = state.grid > 0 and state.grid or (fine and 0.05 or 0.25)
    local rise = fine and 0.02 or 0.1
    -- With a ghost following you, the same steps move it (farther, nearer, to the side).
    local move = function(f, l, u) API.Shift(f, l, u, 0) end
    if action == "Forward" then move(flat, 0, 0)
    elseif action == "Back" then move(-flat, 0, 0)
    elseif action == "Left" then move(0, flat, 0)
    elseif action == "Right" then move(0, -flat, 0)
    elseif action == "Raise" then move(0, 0, rise)
    elseif action == "Lower" then move(0, 0, -rise)
    end
    local amount = (action == "Raise" or action == "Lower") and rise or flat
    local what
    if action == "Raise" or action == "Lower" then
        what = action == "Raise" and "raised" or "lowered"
    elseif Ghosting() and (action == "Forward" or action == "Back") then
        what = action == "Forward" and "farther" or "nearer"
    else
        what = "moved " .. action:lower()
    end
    Feedback((fine and "Shift+" or "") .. KEY_NAMES[action], ("%s %s"):format(what, Yd(amount)))
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

    local ghost = Ghosting()
    if how == "zoom" or (API.state.selected == 0 and not ghost) then
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
        if Ghosting() then
            Feedback("Delete", "set it down first (G), or Escape")
            return
        end
        API.PickUp(false)
        Feedback("Delete", API.state.groupSize > 1 and ("pick up the %d selected pieces"):format(API.state.groupSize) or "pick up")
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
        if Ghosting() then
            API.Command("ghost cancel")
            Feedback("Escape", API.state.ghostMove and "never mind: it stays where it was" or "never mind")
        elseif SpellIsTargeting() then
            SpellStopTargeting()
            Feedback("Escape", "the circle is gone; the piece stays")
        else
            API.Command("edit off")
        end
    end,
    -- G: pick the selected piece up to move it; with one following you, set it down (Shift:
    -- then another of the same).
    Follow = function()
        local another = IsShiftKeyDown()
        if Ghosting() then
            API.Command(another and not API.state.ghostMove and "ghost place another" or "ghost place")
            Feedback(another and "Shift+G" or "G", another and not API.state.ghostMove and "set down; another follows" or "set down")
        elseif API.state.selected > 0 then
            API.Command("ghost move")
            Feedback("G", "it follows you: walk it there, then G again")
        else
            Feedback("G", "select a piece first (click it, or Tab)")
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
    -- While decorating, Ctrl down means a click adds the piece to the selection (or takes it
    -- out): the click itself reaches the server without the key.
    local ctrl = API.state.decorating and API.CanEdit() and IsControlKeyDown() and true or false
    if ctrl ~= ctrlSent then
        ctrlSent = ctrl
        API.Command(ctrl and "group hold on" or "group hold off")
    end
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
    hud:SetHeight(138)
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

    hudTitle = hud:CreateFontString("PlayerHousingEditHudTitle", "OVERLAY", "GameFontNormal")
    hudTitle:SetPoint("TOPLEFT", 10, -8)
    hudTitle:SetText("Edit mode")

    hudName = hud:CreateFontString("PlayerHousingEditHudName", "OVERLAY", "GameFontHighlight")
    hudName:SetPoint("TOPLEFT", hudTitle, "TOPRIGHT", 10, 0)
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
    undoButton = HudButton("Undo", "Undo", 80, function(self, mouseButton)
        if mouseButton == "RightButton" and API.ShowHistory then
            API.ShowHistory(self)
        else
            ONCE.Undo()
        end
    end, "Undo", "Ctrl+Z. Right-click: the last changes, to undo back to any of them.")
    undoButton:RegisterForClicks("LeftButtonUp", "RightButtonUp")
    undoButton:SetPoint("LEFT", wheelButton, "RIGHT", 4, 0)
    redoButton = HudButton("Redo", "Redo", 80, ONCE.Redo, "Redo", "Ctrl+Y")
    redoButton:SetPoint("LEFT", undoButton, "RIGHT", 4, 0)
    doneButton = HudButton("Done", "Done", 80, function() API.Command("edit off") end, "Done", "Leaves edit mode. Escape does too.")
    doneButton:SetPoint("LEFT", redoButton, "RIGHT", 4, 0)

    -- While a piece follows you: set it down, set it down and bring another, never mind.
    local ghostSpecs = {
        { "SetDown", "Set it down", 100, function() API.Command("ghost place") end, "Set it down", "Where it is now. G does too." },
        { "Another", "And another", 100, function() API.Command("ghost place another") end, "Set it down, then another",
          "Another of the same follows you, while you have more. Shift+G does too." },
        { "Cancel", "Never mind", 100, function() API.Command("ghost cancel") end, "Never mind",
          "Nothing changes: a new piece stays in your bags, a moved one where it was. Escape does too." },
    }
    local previousGhost
    for index, spec in ipairs(ghostSpecs) do
        local button = HudButton(spec[1], spec[2], spec[3], spec[4], spec[5], spec[6])
        if previousGhost then
            button:SetPoint("LEFT", previousGhost, "RIGHT", 4, 0)
        else
            button:SetPoint("TOPLEFT", 10, -110)
        end
        button:Hide()
        ghostHudButtons[index] = button
        previousGhost = button
    end

    -- Rows, and several pieces at once: Ctrl-click them first.
    local specs = {
        { "Row", "Row...", 1, function() if API.ShowRowDialog then API.ShowRowDialog() end end, "A row of copies",
          "Copies of the selected piece in a straight row: how many, how far apart, which way." },
        { "Height", "Height", 2, function() API.Command("match height") end, "Same height", "The others go to the first piece's height." },
        { "Turn", "Turn", 2, function() API.Command("match turn") end, "Same turn", "The others turn the way the first piece faces." },
        { "Line", "Line up", 2, function() API.Command("match line") end, "Line up", "A straight row across your view, through the first piece." },
        { "Space", "Space", 3, function() API.Command("match space") end, "Space evenly", "Spread out evenly between the two at the ends." },
        { "Set", "Save set", 1, function() StaticPopup_Show("PLAYERHOUSING_SAVE_SET") end, "Save as a set",
          "The selected pieces and what stands on them, to set down anywhere (Layouts tab, Sets)." },
    }
    local previous
    for index, spec in ipairs(specs) do
        local button = HudButton(spec[1], spec[2], 72, spec[4], spec[5], spec[6])
        if previous then
            button:SetPoint("LEFT", previous, "RIGHT", 3, 0)
        else
            button:SetPoint("TOPLEFT", 10, -110)
        end
        button.needs = spec[3]
        groupHudButtons[index] = button
        previous = button
    end
    hud:Hide()
end

function UpdateHud()
    local state = API.state
    local ghost = state.ghostItem > 0 and API.CanEdit()
    if not state.editMode and not ghost then
        hud:Hide()
        return
    end
    for _, button in ipairs(groupHudButtons) do
        if ghost then button:Hide() else button:Show() end
    end
    for index, button in ipairs(ghostHudButtons) do
        -- "And another" is for new pieces only.
        if ghost and (index ~= 2 or not state.ghostMove) then button:Show() else button:Hide() end
    end
    gridButton:SetText(state.grid > 0 and ("Grid: " .. state.grid .. " yd") or "Grid: off")
    if ghost then
        hudTitle:SetText(state.ghostMove and "Moving" or "Placing")
        local name = API.PieceName(state.ghostItem)
        hudName:SetText(state.ghostMove and state.groupSize > 1 and ("%s and %d more"):format(name, state.groupSize - 1) or name)
        local wheel = (wheelRaises and "Wheel: raise, lower" or "Wheel: turn") .. "   Ctrl+wheel, Page Up/Down: raise, lower   Shift: finer   Alt+wheel: zoom\n"
        if API.HasMouse() then
            hudHelp:SetText("It follows your mouse: click where it goes.   Arrows: nudge it   On a wall, it faces out\n" .. wheel ..
                (state.ghostMove and "Click or G: set it down" or "Click or G: set it down   Shift-click: and another") .. "   Escape: never mind")
        else
            hudHelp:SetText("It follows you: walk it where it goes.   Up/Down arrows: farther, nearer   Left/Right: sideways\n" .. wheel ..
                "G: set it down" .. (state.ghostMove and "" or "   Shift+G: set it down, then another") .. "   Escape: never mind")
        end
        -- Why it stopped following the mouse (off the island, too far).
        if state.ghostNote ~= "" then
            Feedback("Mouse", state.ghostNote)
        end
        wheelButton:Hide()
        undoButton:Hide()
        redoButton:Hide()
        doneButton:Hide()
        hud:Show()
        return
    end
    hudTitle:SetText("Edit mode")
    wheelButton:Show()
    undoButton:Show()
    redoButton:Show()
    doneButton:Show()
    if state.selected > 0 and state.groupSize > 1 then
        hudName:SetText(("%s and %d more"):format(state.selectedName, state.groupSize - 1))
    elseif state.selected > 0 then
        hudName:SetText(state.selectedName)
    else
        hudName:SetText("|cffa0a0a0Right-click a piece, or press Tab|r")
    end
    hudHelp:SetText("Arrows: slide   " .. (wheelRaises and "Wheel: raise, lower" or "Wheel: turn") ..
        "   Ctrl+wheel, Page Up/Down: raise, lower   Shift: finer\n" ..
        "Tab: next piece   G: pick it up to move it   R: " .. (wheelRaises and "wheel turns" or "wheel raises") ..
        "   Delete: pick up\n" ..
        "Ctrl-click: more pieces   Ctrl+Z/Y: undo, redo   Alt+wheel: zoom   Escape: done")
    for _, button in ipairs(groupHudButtons) do
        if state.selected > 0 and math.max(state.groupSize, 1) >= button.needs then button:Enable() else button:Disable() end
    end
    wheelButton:SetText(wheelRaises and "Wheel: raise" or "Wheel: turn")
    if state.undo ~= "" then undoButton:Enable() else undoButton:Disable() end
    if state.redo ~= "" then redoButton:Enable() else redoButton:Disable() end
    hud:Show()
end

-- Keys follow the server's word on edit mode and ghosts, except in combat, when bindings
-- can't change.
local function SyncKeys()
    local want = (API.state.editMode or API.state.ghostItem > 0) and API.CanEdit()
    if want == bound or InCombatLockdown() then
        return
    end
    if want then
        for key, action in pairs(KEYS) do
            SetOverrideBindingClick(owner, true, key, "PlayerHousingEdit" .. action)
        end
    else
        ClearOverrideBindings(owner)
        wipe(held)
    end
    bound = want
end

local lastGhost = 0
API.OnState(function()
    if not hud then
        CreateHud()
    end
    -- A key held when the ghost was set down stops, rather than nudge the piece now selected.
    if (not API.state.editMode and API.state.ghostItem == 0) or (lastGhost > 0 and API.state.ghostItem == 0) then
        wipe(held)
    end
    lastGhost = API.state.ghostItem
    UpdateHud()
    SyncKeys()
end)

owner:RegisterEvent("PLAYER_REGEN_ENABLED")
owner:SetScript("OnEvent", SyncKeys)
