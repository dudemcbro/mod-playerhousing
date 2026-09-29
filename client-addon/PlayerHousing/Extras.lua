-- Smaller windows: the undo history under an Undo button, the row dialog, the "save as a set"
-- question, and the photo tour that takes the buildings' pictures for the preview.

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
-- A row of copies: how many, how far apart (blank: the piece's own length), which way.

local DIRECTIONS = { "right", "left", "forward", "back" }
local rowDialog, rowCount, rowSpacing, rowDirection

local function CreateRowDialog()
    rowDialog = CreateFrame("Frame", "PlayerHousingRowDialog", UIParent)
    rowDialog:SetWidth(250)
    rowDialog:SetHeight(128)
    rowDialog:SetPoint("CENTER", 0, 120)
    rowDialog:SetFrameStrata("DIALOG")
    rowDialog:EnableMouse(true)
    rowDialog:SetBackdrop({
        bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        tile = true, tileSize = 32, edgeSize = 16,
        insets = { left = 4, right = 4, top = 4, bottom = 4 },
    })
    local title = rowDialog:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    title:SetPoint("TOP", 0, -10)
    title:SetText("A row of copies")

    local function Box(name, label, y, width)
        local text = rowDialog:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        text:SetPoint("TOPLEFT", 14, y - 3)
        text:SetText(label)
        local box = CreateFrame("EditBox", name, rowDialog, "InputBoxTemplate")
        box:SetWidth(width)
        box:SetHeight(20)
        box:SetPoint("TOPLEFT", 130, y)
        box:SetAutoFocus(false)
        box:SetScript("OnEscapePressed", box.ClearFocus)
        return box
    end
    rowCount = Box("PlayerHousingRowCount", "How many (1 to 20)", -30, 40)
    rowSpacing = Box("PlayerHousingRowSpacing", "Yards apart", -54, 40)
    local blank = rowDialog:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    blank:SetPoint("TOPLEFT", 176, -57)
    blank:SetText("blank: its length")

    local which = rowDialog:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    which:SetPoint("TOPLEFT", 14, -81)
    which:SetText("Toward your")
    rowDirection = API.MakeButton(rowDialog, "right", 80, function(self)
        local position = 1
        for index, direction in ipairs(DIRECTIONS) do
            if direction == self:GetText() then
                position = index
            end
        end
        self:SetText(DIRECTIONS[position % #DIRECTIONS + 1])
    end, "Which way", "From where you face now: to your right, left, forward or back.")
    rowDirection:SetHeight(20)
    rowDirection:SetPoint("TOPLEFT", 126, -78)

    local place = API.MakeButton(rowDialog, "Place row", 100, function()
        local count = tonumber(rowCount:GetText() or "")
        if not count or count < 1 then
            API.Print("how many copies? 1 to 20.")
            return
        end
        local spacing = tonumber(rowSpacing:GetText() or "") or 0
        API.Command(("row %d %.2f %s"):format(math.min(20, math.floor(count)), math.max(0, spacing), rowDirection:GetText()))
        rowDialog:Hide()
    end, "Place the row", "Copies of the selected piece, turned the same way, from your bags or House Storage (or new ones, where copies are free).")
    place:SetPoint("BOTTOMLEFT", 14, 10)
    local cancel = API.MakeButton(rowDialog, CANCEL, 80, function() rowDialog:Hide() end)
    cancel:SetPoint("BOTTOMRIGHT", -14, 10)
    rowDialog:Hide()
end

API.ShowRowDialog = function()
    if API.state.selected == 0 then
        API.Print("select a piece first: its copies make the row.")
        return
    end
    if not rowDialog then
        CreateRowDialog()
        rowCount:SetText("3")
    end
    rowDialog:Show()
end

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
-- The photo tour (GMs, on their own island): the server sets each building up in front of
-- the camera; the addon hides the interface, takes a screenshot and asks for the next one.
-- Which screenshot shows which building is kept in PlayerHousingDB.photos, for
-- tools/pictures/make_pictures.py, which turns them into the preview's pictures.

local SETTLE, SHOT, RESTORE = 4.0, 0.3, 1.0  -- seconds: for the building to load, then around the shot
local tour = { active = false, step = nil, at = 0, item = 0, taken = 0 }
local tourFrame = CreateFrame("Frame", "PlayerHousingPhotoTour", UIParent)

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
