-- Smaller windows: the undo history under an Undo button, the "save as a set"
-- question, a mannequin's dress list, and the photo tour that takes the buildings' pictures
-- for the preview.

local API = PlayerHousingAPI
local HISTORY_ROWS = 15

---------------------------------------------------------------------------------------------
-- Undo history: right-click Undo. Each row undoes back to (and including) that change.

local history, historyRows, historyEmpty

local function CreateHistory()
    history = CreateFrame("Frame", "PlayerHousingHistory", UIParent)
    history:SetWidth(300)
    history:SetHeight(30 + HISTORY_ROWS * 18)
    history:SetFrameStrata("DIALOG")
    history:EnableMouse(true)
    history:SetBackdrop({
        bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        tile = true, tileSize = 16, edgeSize = 16,
        insets = { left = 4, right = 4, top = 4, bottom = 4 },
    })
    history:SetBackdropColor(0, 0, 0, 0.9)
    local title = history:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    title:SetPoint("TOPLEFT", 10, -8)
    title:SetText("Undo back to...")
    local close = CreateFrame("Button", "PlayerHousingHistoryClose", history, "UIPanelCloseButton")
    close:SetPoint("TOPRIGHT", 2, 2)
    historyEmpty = history:CreateFontString("PlayerHousingHistoryEmpty", "OVERLAY", "GameFontDisableSmall")
    historyEmpty:SetPoint("TOPLEFT", 10, -28)
    historyRows = {}
    for index = 1, HISTORY_ROWS do
        local row = CreateFrame("Button", "PlayerHousingHistoryRow" .. index, history)
        row:SetWidth(280)
        row:SetHeight(18)
        row:SetPoint("TOPLEFT", 10, -24 - (index - 1) * 18)
        row:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight", "ADD")
        row.text = row:CreateFontString("PlayerHousingHistoryRow" .. index .. "Text", "OVERLAY", "GameFontHighlightSmall")
        row.text:SetAllPoints()
        row.text:SetJustifyH("LEFT")
        row:SetScript("OnClick", function(self)
            API.Command("undo " .. self.steps)
            history:Hide()
        end)
        row:SetScript("OnEnter", function(self)
            -- What goes: this change and every newer one.
            for position, other in ipairs(historyRows) do
                if position <= self.steps then other:LockHighlight() else other:UnlockHighlight() end
            end
        end)
        row:SetScript("OnLeave", function()
            for _, other in ipairs(historyRows) do
                other:UnlockHighlight()
            end
        end)
        row:Hide()
        historyRows[index] = row
    end
    history:Hide()
end

API.ShowHistory = function(anchor)
    if not history then
        CreateHistory()
    end
    history:ClearAllPoints()
    history:SetPoint("TOPLEFT", anchor, "BOTTOMLEFT", 0, -2)
    historyEmpty:SetText("Asking the server...")
    for _, row in ipairs(historyRows) do
        row:Hide()
    end
    history:Show()
    API.RequestData("history")
end

API.OnData("history", function(list)
    if not history then
        return
    end
    local count = 0
    for _, row in ipairs(list.rows) do
        if row[1] == "undo" and count < HISTORY_ROWS then
            count = count + 1
            local button = historyRows[count]
            button.steps = count
            button.text:SetText(("%d. %s"):format(count, row[2] or ""))
            button:Show()
        end
    end
    for index = count + 1, HISTORY_ROWS do
        historyRows[index]:Hide()
    end
    historyEmpty:SetText(count == 0 and "Nothing to undo." or "")
end)

---------------------------------------------------------------------------------------------
-- Save the selected pieces as a set.

StaticPopupDialogs["PLAYERHOUSING_SAVE_SET"] = {
    text = "Save the selected pieces, and what stands on them, as a set called:",
    button1 = SAVE or "Save", button2 = CANCEL,
    hasEditBox = 1,
    OnAccept = function(self)
        local box = self.editBox or _G[self:GetName() .. "EditBox"]
        local name = strtrim(box:GetText() or "")
        if name ~= "" then
            API.Command("set save " .. name)
        end
    end,
    EditBoxOnEnterPressed = function(self)
        local parent = self:GetParent()
        StaticPopupDialogs["PLAYERHOUSING_SAVE_SET"].OnAccept(parent)
        parent:Hide()
    end,
    timeout = 0, whileDead = 1, hideOnEscape = 1,
}

---------------------------------------------------------------------------------------------
-- A mannequin's character sheet (a right-click on it): its gear in slots like the game's own,
-- dragged on from the bags and off again (or right-clicked off); its race, man or woman and a
-- new look; trading gear with it; and Move. The server's list (.house data stand <id>) comes
-- again after each change.

local MANNEQUIN_NAME = "Mannequin"
local RACES = { { 1, "Human" }, { 3, "Dwarf" }, { 4, "Night Elf" }, { 7, "Gnome" }, { 11, "Draenei" },
                { 2, "Orc" }, { 5, "Undead" }, { 6, "Tauren" }, { 8, "Troll" }, { 10, "Blood Elf" } }
-- The game's slot names (for their empty-slot pictures) and the server's slots (0-based).
local LEFT_SLOTS = { "HeadSlot", "ShoulderSlot", "BackSlot", "ChestSlot", "ShirtSlot", "TabardSlot", "WristSlot" }
local RIGHT_SLOTS = { "HandsSlot", "WaistSlot", "LegsSlot", "FeetSlot" }
local WEAPON_SLOTS = { "MainHandSlot", "SecondaryHandSlot", "RangedSlot" }

local dress, dressFigure, dressHint, dressModel, raceButton
local slotButtons = {}      -- server slot -> button
local dressing = 0          -- the mannequin the sheet is for
local current = { race = 1, gender = 0 }
local raceMenu = CreateFrame("Frame", "PlayerHousingDressRaceMenu", UIParent, "UIDropDownMenuTemplate")

local function DressCommand(command)
    API.Command(command)
    API.RequestData("stand", dressing)
end

local function RaceName(race)
    for _, entry in ipairs(RACES) do
        if entry[1] == race then
            return entry[2]
        end
    end
    return "Human"
end

-- An item on the cursor (picked up from the bags) goes on; the server finds its slot.
local function DropOn()
    local kind, itemId = GetCursorInfo()
    if kind == "item" and itemId then
        ClearCursor()
        DressCommand(("stand dress %d %d"):format(itemId, dressing))
        return true
    end
    return false
end

local function SlotButton(name, x, y)
    local slotId, empty = GetInventorySlotInfo(name)
    local button = CreateFrame("Button", "PlayerHousingDress" .. name, dress)
    button:SetWidth(37)
    button:SetHeight(37)
    button:SetPoint("TOPLEFT", x, y)
    button.slot = slotId - 1
    button.empty = empty
    button.entry = false
    button.icon = button:CreateTexture(nil, "ARTWORK")
    button.icon:SetAllPoints()
    button.icon:SetTexture(empty)
    button:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square", "ADD")
    button:RegisterForClicks("LeftButtonUp", "RightButtonUp")
    button:RegisterForDrag("LeftButton")
    button:SetScript("OnReceiveDrag", DropOn)
    button:SetScript("OnClick", function(self, mouseButton)
        if DropOn() then
            return
        end
        if self.entry and mouseButton == "RightButton" then
            DressCommand(("stand undress %d %d"):format(self.slot, dressing))
        end
    end)
    -- Dragging what it wears off the sheet takes it off, back to the bags.
    button:SetScript("OnDragStart", function(self)
        if self.entry then
            DressCommand(("stand undress %d %d"):format(self.slot, dressing))
        end
    end)
    button:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        if self.entry then
            GameTooltip:SetHyperlink("item:" .. self.entry)
            GameTooltip:AddLine("Right-click or drag it off: back to your bags.", 0.4, 1, 0.4, true)
        else
            GameTooltip:SetText(_G[name:upper()] or name)
            GameTooltip:AddLine("Drag gear here from your bags.", 1, 1, 1, true)
        end
        GameTooltip:Show()
    end)
    button:SetScript("OnLeave", GameTooltip_Hide)
    slotButtons[button.slot] = button
    return button
end

local function ShowLook()
    raceButton:SetText(RaceName(current.race))
end

local function CreateDress()
    dress = CreateFrame("Frame", "PlayerHousingDress", UIParent)
    dress:SetWidth(330)
    dress:SetHeight(440)
    dress:SetPoint("CENTER", 0, 40)
    dress:SetFrameStrata("DIALOG")
    dress:EnableMouse(true)
    dress:SetMovable(true)
    dress:RegisterForDrag("LeftButton")
    dress:SetScript("OnDragStart", dress.StartMoving)
    dress:SetScript("OnDragStop", dress.StopMovingOrSizing)
    dress:SetScript("OnReceiveDrag", DropOn)
    dress:SetBackdrop({
        bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        tile = true, tileSize = 32, edgeSize = 16,
        insets = { left = 4, right = 4, top = 4, bottom = 4 },
    })
    local title = dress:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    title:SetPoint("TOPLEFT", 12, -10)
    title:SetText("Mannequin")
    local close = CreateFrame("Button", "PlayerHousingDressClose", dress, "UIPanelCloseButton")
    close:SetPoint("TOPRIGHT", 2, 2)
    dressFigure = dress:CreateFontString("PlayerHousingDressFigure", "OVERLAY", "GameFontHighlightSmall")
    dressFigure:SetPoint("TOP", 0, -12)

    for index, name in ipairs(LEFT_SLOTS) do
        SlotButton(name, 12, -36 - (index - 1) * 41)
    end
    for index, name in ipairs(RIGHT_SLOTS) do
        SlotButton(name, 330 - 12 - 37, -36 - (index - 1) * 41)
    end
    for index, name in ipairs(WEAPON_SLOTS) do
        SlotButton(name, 330 / 2 - 60 + (index - 1) * 41, -330)
    end

    -- The mannequin itself, when it's the target (a right-click targets it).
    dressModel = CreateFrame("PlayerModel", "PlayerHousingDressModel", dress)
    dressModel:SetPoint("TOPLEFT", 56, -36)
    dressModel:SetWidth(330 - 112)
    dressModel:SetHeight(280)
    dressHint = dress:CreateFontString("PlayerHousingDressHint", "OVERLAY", "GameFontDisableSmall")
    dressHint:SetPoint("BOTTOM", dressModel, "BOTTOM", 0, 6)
    dressHint:SetWidth(200)
    dressHint:SetText("Drag gear from your bags onto it.")

    raceButton = API.MakeButton(dress, "Human", 90, function(self)
        local menu = {}
        for _, entry in ipairs(RACES) do
            menu[#menu + 1] = { text = entry[2], checked = entry[1] == current.race, func = function()
                DressCommand(("stand look %d %s %d"):format(entry[1], current.gender == 1 and "female" or "male", dressing))
            end }
        end
        EasyMenu(menu, raceMenu, self, 0, 0, "MENU")
    end, "Race", "Its race; a new look comes with it.")
    raceButton:SetPoint("TOPLEFT", 12, -376)
    local male = API.MakeButton(dress, "Man", 50, function() DressCommand(("stand look %d male %d"):format(current.race, dressing)) end,
        "A man", "Of the same race, with a new look.")
    male:SetPoint("LEFT", raceButton, "RIGHT", 4, 0)
    local female = API.MakeButton(dress, "Woman", 60, function() DressCommand(("stand look %d female %d"):format(current.race, dressing)) end,
        "A woman", "Of the same race, with a new look.")
    female:SetPoint("LEFT", male, "RIGHT", 4, 0)
    local newLook = API.MakeButton(dress, "New look", 86, function()
        DressCommand(("stand look %d %s %d"):format(current.race, current.gender == 1 and "female" or "male", dressing))
    end, "A new look", "Another face, skin, hair and hair color, all ones a character could be made with.")
    newLook:SetPoint("LEFT", female, "RIGHT", 4, 0)

    local trade = API.MakeButton(dress, "Trade gear", 86, function() DressCommand(("stand trade %d"):format(dressing)) end,
        "Trade gear", "What it wears goes on you, and what you wear goes on it. What you can't wear goes to your bags.")
    trade:SetPoint("TOPLEFT", 12, -402)
    local allOff = API.MakeButton(dress, "Take all off", 86, function() DressCommand(("stand undress all %d"):format(dressing)) end,
        "Take everything off", "Back to your bags (by mail when they're full).")
    allOff:SetPoint("LEFT", trade, "RIGHT", 4, 0)
    local move = API.MakeButton(dress, "Move", 70, function()
        API.Command("select " .. dressing)
        API.Command("ghost move " .. dressing)
        dress:Hide()
    end, "Move it", "It goes on your mouse, like any piece: a click sets it down.")
    move:SetPoint("LEFT", allOff, "RIGHT", 4, 0)
    dress:Hide()
    tinsert(UISpecialFrames, "PlayerHousingDress")
end

API.ShowDress = function(placementId)
    if not placementId or placementId == 0 then
        return
    end
    if not dress then
        CreateDress()
    end
    dressing = placementId
    dressFigure:SetText("")
    for _, button in pairs(slotButtons) do
        button.entry = false
        button.icon:SetTexture(button.empty)
    end
    if UnitExists("target") and not UnitIsPlayer("target") and UnitName("target") == MANNEQUIN_NAME then
        dressModel:SetUnit("target")
        dressModel:Show()
    else
        dressModel:Hide()
    end
    dress:Show()
    API.RequestData("stand", placementId)
end

-- The server opens it (a right-click on a mannequin).
API.OnMessage("dress", function(fields)
    API.ShowDress(tonumber(fields[2] or ""))
end)

API.OnData("stand", function(list)
    if not dress or tonumber(list.arg or "") ~= dressing then
        return
    end
    for _, button in pairs(slotButtons) do
        button.entry = false
        button.icon:SetTexture(button.empty)
    end
    local figure = ""
    for _, row in ipairs(list.rows) do
        if row[1] == "figure" then
            figure = row[2] or ""
        elseif row[1] == "look" then
            local look = tonumber(row[2] or "") or 0
            current.race = look % 16
            current.gender = math.floor(look / 256) % 2
        elseif row[1] == "worn" then
            local button = slotButtons[tonumber(row[2] or "")]
            if button then
                button.entry = tonumber(row[4])
                button.icon:SetTexture(GetItemIcon(button.entry) or "Interface\\Icons\\INV_Misc_QuestionMark")
            end
        end
    end
    dressFigure:SetText(figure)
    ShowLook()
    if dressModel:IsShown() then
        dressModel:RefreshUnit()
    end
end)

-- The sheet closes when the mannequin is no longer selected.
API.OnState(function(state)
    if dress and dress:IsShown() and state.selected ~= dressing then
        dress:Hide()
    end
end)

---------------------------------------------------------------------------------------------
-- The photo tour (GMs, on their own island): the server sets each building up in front of
-- the camera; the addon hides the interface, takes a screenshot and asks for the next one.
-- Which screenshot shows which building is kept in PlayerHousingDB.photos, for
-- tools/pictures/make_pictures.py, which turns them into the preview's pictures.

local SETTLE, SHOT, RESTORE = 4.0, 0.3, 1.0  -- seconds: for the building to load, then around the shot
local tour = { active = false, step = nil, at = 0, item = 0, taken = 0 }
-- Not under UIParent: hiding the interface for the shot would stop its OnUpdate too.
local tourFrame = CreateFrame("Frame", "PlayerHousingPhotoTour")

local function StopTour(message)
    tour.active, tour.step = false, nil
    if not UIParent:IsShown() then
        UIParent:Show()
    end
    if message then
        API.Print(message)
    end
end

tourFrame:SetScript("OnUpdate", function()
    if not tour.active or not tour.step or GetTime() < tour.at then
        return
    end
    if tour.step == "time" then
        -- Weather, time of day and music have a moment's wait between them.
        API.Command("time midday")
        tour.step, tour.at = "start", GetTime() + 1.5
    elseif tour.step == "start" then
        tour.step = nil
        API.Command("phototour start")
    elseif tour.step == "settle" then
        UIParent:Hide()
        tour.step, tour.at = "shoot", GetTime() + SHOT
    elseif tour.step == "shoot" then
        Screenshot()
        PlayerHousingDB.photos = PlayerHousingDB.photos or {}
        PlayerHousingDB.photos[tour.item] = date("%m%d%y_%H%M%S")
        tour.taken = tour.taken + 1
        tour.step, tour.at = "restore", GetTime() + RESTORE
    elseif tour.step == "restore" then
        UIParent:Show()
        tour.step = nil
        API.Command("phototour next")
    end
end)

API.OnMessage("photo", function(fields)
    if not tour.active then
        return
    end
    if fields[2] == "done" then
        StopTour(("the photo tour is done: %d pictures. Log out (or /reload) so they're saved, then run tools/pictures/make_pictures.py."):format(tour.taken))
        return
    end
    tour.item = tonumber(fields[2]) or 0
    tour.step, tour.at = "settle", GetTime() + SETTLE
end)

function PlayerHousing_PhotoTour(start)
    if not start then
        if tour.active then
            API.Command("phototour stop")
        end
        StopTour("photo tour stopped.")
        return
    end
    if InCombatLockdown() then
        API.Print("not in combat.")
        return
    end
    if not API.state.own then
        API.Print("go home first: the tour sets the buildings up on your own island (GMs only).")
        return
    end
    tour.active, tour.taken = true, 0
    API.Print("photo tour: daylight and clear skies, first person, then a picture of each building. /housing phototour stop ends it.")
    API.Command("weather clear")
    CameraZoomIn(50)
    tour.step, tour.at = "time", GetTime() + 1.5
end
