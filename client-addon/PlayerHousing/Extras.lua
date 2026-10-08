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
-- A mannequin's dress list: what it wears (take it off), what in your bags it could wear (put
-- it on), and its figure. The server's list (.house data stand <id>) comes again after each.

local DRESS_ROWS = 12
local dress, dressTitle, dressFigure, dressRows, dressEmpty, dressAllOff
local dressing = 0      -- the mannequin the list is for

local function DressCommand(command)
    API.Command(command)
    API.RequestData("stand", dressing)
end

local function CreateDress()
    dress = CreateFrame("Frame", "PlayerHousingDress", UIParent)
    dress:SetWidth(320)
    dress:SetHeight(76 + DRESS_ROWS * 20)
    dress:SetPoint("CENTER", 0, 60)
    dress:SetFrameStrata("DIALOG")
    dress:EnableMouse(true)
    dress:SetMovable(true)
    dress:RegisterForDrag("LeftButton")
    dress:SetScript("OnDragStart", dress.StartMoving)
    dress:SetScript("OnDragStop", dress.StopMovingOrSizing)
    dress:SetBackdrop({
        bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        tile = true, tileSize = 32, edgeSize = 16,
        insets = { left = 4, right = 4, top = 4, bottom = 4 },
    })
    dressTitle = dress:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    dressTitle:SetPoint("TOPLEFT", 12, -10)
    dressTitle:SetText("Dress the mannequin")
    local close = CreateFrame("Button", "PlayerHousingDressClose", dress, "UIPanelCloseButton")
    close:SetPoint("TOPRIGHT", 2, 2)

    dressFigure = dress:CreateFontString("PlayerHousingDressFigure", "OVERLAY", "GameFontHighlightSmall")
    dressFigure:SetPoint("TOPLEFT", 12, -32)
    local figureButton = API.MakeButton(dress, "Change", 64, function() DressCommand("stand figure " .. dressing) end,
        "Change its figure", "Another race and gender to wear the gear.")
    figureButton:SetHeight(18)
    figureButton:SetPoint("TOPRIGHT", -12, -28)
    dressAllOff = API.MakeButton(dress, "Take all off", 90, function() DressCommand("stand undress all " .. dressing) end,
        "Take everything off", "Back to your bags (by mail when they're full).")
    dressAllOff:SetHeight(18)
    dressAllOff:SetPoint("RIGHT", figureButton, "LEFT", -4, 0)

    dressEmpty = dress:CreateFontString("PlayerHousingDressEmpty", "OVERLAY", "GameFontDisableSmall")
    dressEmpty:SetPoint("TOPLEFT", 12, -56)
    dressEmpty:SetWidth(296)
    dressEmpty:SetJustifyH("LEFT")
    dressRows = {}
    for index = 1, DRESS_ROWS do
        local row = CreateFrame("Frame", "PlayerHousingDressRow" .. index, dress)
        row:SetWidth(296)
        row:SetHeight(20)
        row:SetPoint("TOPLEFT", 12, -54 - (index - 1) * 20)
        row.icon = row:CreateTexture(nil, "ARTWORK")
        row.icon:SetWidth(18)
        row.icon:SetHeight(18)
        row.icon:SetPoint("LEFT", 0, 0)
        row.text = row:CreateFontString("PlayerHousingDressRow" .. index .. "Text", "OVERLAY", "GameFontHighlightSmall")
        row.text:SetPoint("LEFT", 22, 0)
        row.text:SetWidth(200)
        row.text:SetJustifyH("LEFT")
        row.button = API.MakeButton(row, "", 68, function()
            local item = row.item
            if item and item.kind == "worn" then
                DressCommand(("stand undress %d %d"):format(item.slot, dressing))
            elseif item then
                DressCommand(("stand dress %d %d"):format(item.entry, dressing))
            end
        end)
        row.button:SetHeight(18)
        row.button:SetPoint("RIGHT", 0, 0)
        row:Hide()
        dressRows[index] = row
    end
    dress:Hide()
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
    dressEmpty:SetText("Asking the server...")
    for _, row in ipairs(dressRows) do
        row:Hide()
    end
    dress:Show()
    API.RequestData("stand", placementId)
end

API.OnData("stand", function(list)
    if not dress or tonumber(list.arg or "") ~= dressing then
        return
    end
    local items, worn = {}, 0
    for _, row in ipairs(list.rows) do
        if row[1] == "figure" then
            dressFigure:SetText("Figure: " .. (row[2] or ""))
        elseif row[1] == "worn" then
            worn = worn + 1
            table.insert(items, worn, { kind = "worn", slot = tonumber(row[2]), slotName = row[3], entry = tonumber(row[4]), name = row[5] })
        elseif row[1] == "wear" then
            items[#items + 1] = { kind = "wear", entry = tonumber(row[2]), name = row[3], slotName = row[4] }
        end
    end
    if worn > 1 then dressAllOff:Show() else dressAllOff:Hide() end
    dressEmpty:SetText(#items == 0 and "Nothing to show: armor you can see, weapons, shields, shirts and tabards in your bags can go on it." or "")
    for index, row in ipairs(dressRows) do
        local item = items[index]
        row.item = item
        if item then
            row.icon:SetTexture(GetItemIcon(item.entry) or "Interface\\Icons\\INV_Misc_QuestionMark")
            if item.kind == "worn" then
                row.text:SetText(("%s |cffa0a0a0(%s)|r"):format(item.name, item.slotName))
                row.button:SetText("Take off")
            else
                row.text:SetText(("|cffd0d0d0%s|r |cffa0a0a0%s|r"):format(item.name, item.slotName ~= "" and ("(" .. item.slotName .. ")") or ""))
                row.button:SetText("Put on")
            end
            row:Show()
        else
            row:Hide()
        end
    end
end)

-- The window closes the list when the mannequin is no longer selected.
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
