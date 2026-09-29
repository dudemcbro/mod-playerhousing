-- The housing window's tabs besides Bags: Collection, Storage, Placed, Layouts, Guests, Visit
-- and Island. Each asks the server for its list when shown (.house data <kind>, answered in
-- addon messages) and acts through .house commands, so the window can't do anything a
-- player couldn't type. The House Key menu still has all of it too.

local API = PlayerHousingAPI
local TABS = { "Bags", "Collection", "Storage", "Placed", "Layouts", "Guests", "Visit", "Island" }
local ROW_HEIGHT = 22
local GRID_COLUMNS, GRID_ROWS, SLOT = 9, 4, 40
local VISIT_LISTS = { "Party", "Guild", "Friends", "Invited", "Public", "Most liked" }
local PRIVACY = { "Private", "Friends", "Public" }   -- friends and guild, on the server

local frame
local tabButtons, panels = {}, {}
local current = "Bags"
local pieceById = {}
local collection = { unlocked = {}, fresh = {}, storage = {}, placed = {}, free = false, catalog = false, unlockAll = false }
local collectionView = { category = 0, unlockedOnly = false, search = "", page = 1 }
local island = { weathers = {}, times = {}, tracks = {}, privacy = 0, weather = 0, time = 0, music = 0, musicBox = false }
local visitList = 1

for _, info in ipairs(PlayerHousing_Pieces or {}) do
    pieceById[info[1]] = info
end

---------------------------------------------------------------------------------------------
-- Helpers

local function Ranges(text, into)
    for part in (text or ""):gmatch("[^,]+") do
        local first, last = part:match("^(%d+)-(%d+)$")
        first, last = tonumber(first or part), tonumber(last or part)
        if first and last then
            for id = first, last do
                into[id] = true
            end
        end
    end
end

local function Counts(text, into)
    for id, count in (text or ""):gmatch("(%d+):(%d+)") do
        into[tonumber(id)] = tonumber(count)
    end
end

local function PieceName(id)
    local info = pieceById[id]
    if info then
        return info[2]
    end
    local name = GetItemInfo(id)
    return name and (name:gsub("^Furnishing: ", ""):gsub("^Building: ", "")) or ("Piece " .. id)
end

local function PieceIcon(id)
    local info = pieceById[id]
    if info then
        return "Interface\\Icons\\" .. info[6]
    end
    return GetItemIcon(id) or "Interface\\Icons\\INV_Misc_QuestionMark"
end

local function IsBuilding(id)
    local info = pieceById[id]
    return info ~= nil and info[4] == 1
end

local function Money(copper)
    if collection.free or copper == 0 then
        return "free"
    end
    return GetCoinTextureString(copper)
end

-- A command, then the tab's list again: the server answers them in order, so the list
-- shows what the command did.
local function Do(command, kind, argument)
    API.Command(command)
    if kind then
        API.RequestData(kind, argument)
    end
end

local function Label(parent, text, x, y, font)
    local label = parent:CreateFontString(nil, "OVERLAY", font or "GameFontNormalSmall")
    label:SetPoint("TOPLEFT", x, y)
    label:SetText(text)
    return label
end

local function SmallButton(parent, name, text, width, onClick, tooltipTitle, tooltipText)
    local button = API.MakeButton(parent, text, width, onClick, tooltipTitle, tooltipText)
    button:SetHeight(18)
    if name then
        _G[name] = button
    end
    return button
end

local function InputBox(parent, name, width, x, y)
    local box = CreateFrame("EditBox", name, parent, "InputBoxTemplate")
    box:SetWidth(width)
    box:SetHeight(20)
    box:SetPoint("TOPLEFT", x, y)
    box:SetAutoFocus(false)
    box:SetScript("OnEscapePressed", box.ClearFocus)
    return box
end

-- A list of rows, a page at a time: an icon, a line of text and buttons on the right.
local function MakeList(panel, top, rows, buttons)
    local list = { rows = {}, items = {}, page = 1 }
    local width = API.WIDTH - 24
    for index = 1, rows do
        local row = CreateFrame("Frame", panel:GetName() .. "Row" .. index, panel)
        row:SetPoint("TOPLEFT", 12, top - (index - 1) * ROW_HEIGHT)
        row:SetWidth(width)
        row:SetHeight(ROW_HEIGHT)
        row.icon = row:CreateTexture(nil, "ARTWORK")
        row.icon:SetWidth(18)
        row.icon:SetHeight(18)
        row.icon:SetPoint("LEFT", 0, 0)
        row.text = row:CreateFontString(panel:GetName() .. "Row" .. index .. "Text", "OVERLAY", "GameFontHighlightSmall")
        row.text:SetPoint("LEFT", 22, 0)
        row.text:SetJustifyH("LEFT")
        row.buttons = {}
        local right = 0
        for position = #buttons, 1, -1 do
            local spec = buttons[position]
            local button = SmallButton(row, panel:GetName() .. "Row" .. index .. "Button" .. position, spec[1], spec[2], function()
                if row.item then
                    spec[3](row.item)
                end
            end)
            button:SetPoint("RIGHT", row, "RIGHT", -right, 0)
            right = right + spec[2] + 2
            row.buttons[position] = button
        end
        row.text:SetWidth(width - 22 - right - 4)
        row:Hide()
        list.rows[index] = row
    end

    local pageY = top - rows * ROW_HEIGHT - 2
    list.prev = SmallButton(panel, panel:GetName() .. "Prev", "<", 26, function()
        list.page = list.page - 1
        list:Refresh()
    end)
    list.prev:SetPoint("TOPLEFT", 12, pageY)
    list.next = SmallButton(panel, panel:GetName() .. "Next", ">", 26, function()
        list.page = list.page + 1
        list:Refresh()
    end)
    list.next:SetPoint("TOPRIGHT", -12, pageY)
    list.pageText = panel:CreateFontString(panel:GetName() .. "PageText", "OVERLAY", "GameFontHighlightSmall")
    list.pageText:SetPoint("TOP", panel, "TOP", 0, pageY - 3)
    list.empty = panel:CreateFontString(panel:GetName() .. "Empty", "OVERLAY", "GameFontDisable")
    list.empty:SetPoint("TOP", panel, "TOP", 0, top - 30)

    function list:SetItems(items, render, emptyText)
        self.items, self.render, self.emptyText = items, render, emptyText
        self:Refresh()
    end

    function list:Refresh()
        local pages = math.max(1, math.ceil(#self.items / rows))
        self.page = math.min(math.max(self.page, 1), pages)
        for index, row in ipairs(self.rows) do
            local item = self.items[(self.page - 1) * rows + index]
            row.item = item
            if item then
                self.render(row, item)
                row:Show()
            else
                row:Hide()
            end
        end
        self.empty:SetText(#self.items == 0 and (self.emptyText or "") or "")
        self.pageText:SetText(pages > 1 and ("Page %d of %d"):format(self.page, pages) or "")
        if self.page > 1 then self.prev:Enable() else self.prev:Disable() end
        if self.page < pages then self.next:Enable() else self.next:Disable() end
    end

    return list
end

local function MakePanel(name)
    local panel = CreateFrame("Frame", "PlayerHousing" .. name .. "Panel", frame)
    panel:SetPoint("TOPLEFT", 0, API.CONTENT_TOP)
    panel:SetWidth(API.WIDTH)
    panel:SetHeight(API.CONTENT_HEIGHT)
    panel:Hide()
    panels[name] = panel
    return panel
end

---------------------------------------------------------------------------------------------
-- Tabs

local function Request(name)
    if name == "Collection" or name == "Storage" then
        API.RequestData("collection")
    elseif name == "Placed" then
        if API.CanEdit() then
            API.RequestData("placed")
        end
    elseif name == "Layouts" then
        API.RequestData("layouts")
    elseif name == "Guests" then
        API.RequestData("guests")
    elseif name == "Visit" then
        API.RequestData("visits", visitList - 1)
    elseif name == "Island" then
        API.RequestData("island")
    end
end

-- Panels hold the window's secure buttons' neighbours, so tabs change out of combat only.
function PlayerHousing_SelectTab(name)
    if not frame or not panels[name] then
        return
    end
    if InCombatLockdown() then
        API.Print("tabs can't change in combat.")
        return
    end
    -- Leaving the Collection, the new marks have been seen.
    if current == "Collection" and name ~= "Collection" and next(collection.fresh) then
        API.Command("seen")
        wipe(collection.fresh)
    end
    current = name
    for tab, panel in pairs(panels) do
        if tab == name then panel:Show() else panel:Hide() end
    end
    for tab, button in pairs(tabButtons) do
        if tab == name then button:LockHighlight() else button:UnlockHighlight() end
    end
    if API.IsKnown() then
        Request(name)
    end
end

---------------------------------------------------------------------------------------------
-- Collection: everything there is, unlocked or not, and a copy of what's unlocked.

local collectionSlots, categoryText, collectionPageText, collectionStatus, collectionPrev, collectionNext = {}, nil, nil, nil, nil, nil

local function CollectionPieces()
    local list = {}
    local search = collectionView.search
    for _, info in ipairs(PlayerHousing_Pieces or {}) do
        local id, category = info[1], info[3]
        local catalog = PlayerHousing_Categories[category] == "Catalog"
        if (not catalog or collection.catalog)
            and (collectionView.category == 0 or collectionView.category == category)
            and (not collectionView.unlockedOnly or collection.unlocked[id])
            and (search == "" or info[2]:lower():find(search, 1, true)) then
            list[#list + 1] = info
        end
    end
    return list
end

local function UpdateCollection()
    if not categoryText then
        return
    end
    local name = collectionView.category == 0 and "All categories" or PlayerHousing_Categories[collectionView.category]
    categoryText:SetText(name)

    local list = CollectionPieces()
    local perPage = GRID_COLUMNS * GRID_ROWS
    local pages = math.max(1, math.ceil(#list / perPage))
    collectionView.page = math.min(math.max(collectionView.page, 1), pages)
    for index, button in ipairs(collectionSlots) do
        local info = list[(collectionView.page - 1) * perPage + index]
        button.info = info
        if info then
            local id = info[1]
            local unlocked = collection.unlocked[id]
            button.icon:SetTexture("Interface\\Icons\\" .. info[6])
            button.icon:SetDesaturated(not unlocked)
            button.icon:SetAlpha(unlocked and 1 or 0.45)
            local have = GetItemCount(id)
            button.count:SetText(have > 0 and have or "")
            button.new:SetText(collection.fresh[id] and "New" or "")
            button:Show()
        else
            button:Hide()
        end
    end
    collectionPageText:SetText(("Page %d of %d"):format(collectionView.page, pages))
    if collectionView.page > 1 then collectionPrev:Enable() else collectionPrev:Disable() end
    if collectionView.page < pages then collectionNext:Enable() else collectionNext:Disable() end

    local unlocked, total = 0, 0
    for _, info in ipairs(PlayerHousing_Pieces or {}) do
        if PlayerHousing_Categories[info[3]] ~= "Catalog" or collection.catalog then
            total = total + 1
            if collection.unlocked[info[1]] then
                unlocked = unlocked + 1
            end
        end
    end
    collectionStatus:SetText(("%d of %d unlocked. Click a piece to place it or get copies."):format(unlocked, total))
end

local function CycleCategory(step)
    local count = #PlayerHousing_Categories
    repeat
        collectionView.category = (collectionView.category + step) % (count + 1)
    until collectionView.category == 0 or PlayerHousing_Categories[collectionView.category] ~= "Catalog" or collection.catalog
    collectionView.page = 1
    UpdateCollection()
end

local function CollectionTooltip(button)
    local info = button.info
    if not info then
        return
    end
    local id = info[1]
    GameTooltip:SetOwner(button, "ANCHOR_RIGHT")
    GameTooltip:SetText(info[2])
    GameTooltip:AddLine(PlayerHousing_Categories[info[3]] .. (info[4] == 1 and ", building" or ""), 0.7, 0.7, 0.7)
    if collection.unlocked[id] then
        GameTooltip:AddLine("Unlocked. A copy is " .. Money(info[5]) .. ".", 0.4, 1, 0.4, true)
    else
        GameTooltip:AddLine("Locked" .. (info[7] ~= "" and (": " .. info[7]) or "."), 1, 0.5, 0.3, true)
    end
    GameTooltip:AddLine(("In your bags: %d. In House Storage: %d. Placed: %d."):format(GetItemCount(id), collection.storage[id] or 0,
        collection.placed[id] or 0), 1, 1, 1, true)
    GameTooltip:AddLine("Click: show it next to the window, to place it or get copies.", 0.4, 1, 0.4, true)
    GameTooltip:Show()
    API.ShowPreview({ id = id, name = info[2] })
end

local function CreateCollection()
    local panel = MakePanel("Collection")
    SmallButton(panel, "PlayerHousingCategoryPrev", "<", 24, function() CycleCategory(-1) end):SetPoint("TOPLEFT", 12, 0)
    categoryText = panel:CreateFontString("PlayerHousingCategoryText", "OVERLAY", "GameFontHighlightSmall")
    categoryText:SetPoint("TOPLEFT", 38, -3)
    categoryText:SetWidth(100)
    SmallButton(panel, "PlayerHousingCategoryNext", ">", 24, function() CycleCategory(1) end):SetPoint("TOPLEFT", 140, 0)

    local unlockedOnly = CreateFrame("CheckButton", "PlayerHousingUnlockedOnly", panel, "UICheckButtonTemplate")
    unlockedOnly:SetWidth(20)
    unlockedOnly:SetHeight(20)
    unlockedOnly:SetPoint("TOPLEFT", 168, 1)
    Label(panel, "Unlocked", 188, -3, "GameFontHighlightSmall")
    unlockedOnly:SetScript("OnClick", function(self)
        collectionView.unlockedOnly = self:GetChecked() and true or false
        collectionView.page = 1
        UpdateCollection()
    end)

    local search = InputBox(panel, "PlayerHousingCollectionSearch", API.WIDTH - 260, 246, 0)
    search:SetScript("OnEnterPressed", search.ClearFocus)
    search:SetScript("OnTextChanged", function(self)
        collectionView.search = (self:GetText() or ""):lower()
        collectionView.page = 1
        UpdateCollection()
    end)

    for index = 1, GRID_COLUMNS * GRID_ROWS do
        local button = CreateFrame("Button", "PlayerHousingCollectionSlot" .. index, panel)
        button:SetWidth(36)
        button:SetHeight(36)
        local column, row = (index - 1) % GRID_COLUMNS, math.floor((index - 1) / GRID_COLUMNS)
        button:SetPoint("TOPLEFT", 14 + column * SLOT, -28 - row * SLOT)
        button:RegisterForClicks("AnyUp")
        button.icon = button:CreateTexture(nil, "ARTWORK")
        button.icon:SetAllPoints()
        button.count = button:CreateFontString(nil, "OVERLAY", "NumberFontNormal")
        button.count:SetPoint("BOTTOMRIGHT", -2, 2)
        button.new = button:CreateFontString(nil, "OVERLAY", "GameFontGreenSmall")
        button.new:SetPoint("TOPLEFT", 1, -1)
        button:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square", "ADD")
        button:SetScript("OnEnter", CollectionTooltip)
        button:SetScript("OnLeave", function()
            GameTooltip_Hide()
            API.HidePreview()
        end)
        button:SetScript("OnClick", function(self)
            if self.info then
                API.Pin({ id = self.info[1], name = self.info[2] })
            end
        end)
        button:Hide()
        collectionSlots[index] = button
    end

    collectionPrev = SmallButton(panel, "PlayerHousingCollectionPrev", "<", 26, function()
        collectionView.page = collectionView.page - 1
        UpdateCollection()
    end)
    collectionPrev:SetPoint("TOPLEFT", 12, -192)
    collectionNext = SmallButton(panel, "PlayerHousingCollectionNext", ">", 26, function()
        collectionView.page = collectionView.page + 1
        UpdateCollection()
    end)
    collectionNext:SetPoint("TOPRIGHT", -12, -192)
    collectionPageText = panel:CreateFontString("PlayerHousingCollectionPageText", "OVERLAY", "GameFontHighlightSmall")
    collectionPageText:SetPoint("TOP", 0, -195)
    collectionStatus = panel:CreateFontString("PlayerHousingCollectionStatus", "OVERLAY", "GameFontNormalSmall")
    collectionStatus:SetPoint("TOPLEFT", 14, -218)
    collectionStatus:SetPoint("TOPRIGHT", -14, -218)
    panel:SetScript("OnShow", UpdateCollection)
end

---------------------------------------------------------------------------------------------
-- Storage: pieces that came back while the bags were full.

local storageList, storageStatus

local function UpdateStorage()
    if not storageList then
        return
    end
    local items, total = {}, 0
    for id, count in pairs(collection.storage) do
        items[#items + 1] = { id = id, count = count, name = PieceName(id) }
        total = total + count
    end
    table.sort(items, function(left, right) return left.name < right.name end)
    storageStatus:SetText(total == 0 and "House Storage is empty." or ("%d pieces in House Storage."):format(total))
    storageList:SetItems(items, function(row, item)
        row.icon:SetTexture(PieceIcon(item.id))
        row.text:SetText(item.count > 1 and ("%s x%d"):format(item.name, item.count) or item.name)
    end, "Pieces come here when your bags are full.")
end

local function CreateStorage()
    local panel = MakePanel("Storage")
    storageStatus = Label(panel, "", 14, -3, "GameFontHighlightSmall")
    SmallButton(panel, "PlayerHousingTakeAll", "Take all", 80, function() Do("take all", "collection") end,
        "Take all", "Everything in House Storage that fits in your bags."):SetPoint("TOPRIGHT", -12, 0)
    storageList = MakeList(panel, -26, 8, {
        { "Take", 50, function(item) Do("take " .. item.id, "collection") end },
    })
end

---------------------------------------------------------------------------------------------
-- Placed: the pieces on the island, nearest first.

local placedList, placedStatus

local function CreatePlaced()
    local panel = MakePanel("Placed")
    placedStatus = Label(panel, "", 14, -3, "GameFontHighlightSmall")
    SmallButton(panel, "PlayerHousingPlacedRefresh", "Refresh", 70, function() Request("Placed") end,
        "Refresh", "Nearest first, from where you stand now."):SetPoint("TOPRIGHT", -12, 0)
    placedList = MakeList(panel, -26, 8, {
        { "Select", 50, function(item) API.Command("select " .. item.id) end },
        { "Here", 42, function(item) Do("here " .. item.id, "placed") end },
        { "Pick up", 54, function(item) API.PickUpPlacement(item.id, PieceName(item.item), IsBuilding(item.item)) end },
    })
    panel:SetScript("OnShow", function()
        if not API.CanEdit() then
            placedStatus:SetText("")
            placedList:SetItems({}, function() end, "Go home (or to an island where you're a roommate) to see its pieces.")
        end
    end)
end

API.OnData("placed", function(list)
    if not placedList then
        return
    end
    local items = {}
    for _, row in ipairs(list.rows) do
        items[#items + 1] = { id = tonumber(row[1]), item = tonumber(row[2]), distance = tonumber(row[3]) or 0 }
    end
    placedStatus:SetText(("%d pieces on the island, nearest first."):format(list.total or #items))
    placedList:SetItems(items, function(row, item)
        row.icon:SetTexture(PieceIcon(item.item))
        row.text:SetText(("%s |cffa0a0a0%.0f yd|r"):format(PieceName(item.item), item.distance))
    end, "Nothing placed yet.")
end)

---------------------------------------------------------------------------------------------
-- Layouts: whole islands, saved and set out again.

local layoutList, layoutStatus, layoutName

StaticPopupDialogs["PLAYERHOUSING_LAYOUT_LOAD"] = {
    text = "Set out %s?\n\nThe pieces on your island now are put away first.",
    button1 = "Set out", button2 = CANCEL,
    OnAccept = function(self, data) Do("layout load " .. data, "layouts") end,
    timeout = 0, whileDead = 1, hideOnEscape = 1,
}
StaticPopupDialogs["PLAYERHOUSING_LAYOUT_DELETE"] = {
    text = "Delete the layout %s?",
    button1 = DELETE or "Delete", button2 = CANCEL,
    OnAccept = function(self, data) Do("layout delete " .. data, "layouts") end,
    timeout = 0, whileDead = 1, hideOnEscape = 1,
}
StaticPopupDialogs["PLAYERHOUSING_LAYOUT_SEND"] = {
    text = "Send %s to whom?",
    button1 = "Send", button2 = CANCEL,
    hasEditBox = 1,
    OnAccept = function(self, data)
        local box = self.editBox or _G[self:GetName() .. "EditBox"]
        local name = strtrim(box:GetText() or "")
        if name ~= "" then
            API.Command(("layout send %s %s"):format(data, name))
        end
    end,
    timeout = 0, whileDead = 1, hideOnEscape = 1,
}

local function Popup(which, text, data)
    local dialog = StaticPopup_Show(which, text)
    if dialog then
        dialog.data = data
    end
end

local function CreateLayouts()
    local panel = MakePanel("Layouts")
    layoutName = InputBox(panel, "PlayerHousingLayoutName", 180, 18, 0)
    local function Save()
        local name = strtrim(layoutName:GetText() or "")
        if name == "" then
            API.Print("give the layout a name first.")
            return
        end
        layoutName:SetText("")
        layoutName:ClearFocus()
        Do("layout save " .. name, "layouts")
    end
    layoutName:SetScript("OnEnterPressed", Save)
    SmallButton(panel, "PlayerHousingLayoutSave", "Save as new", 90, Save,
        "Save the island", "Every piece where it stands, under the name on the left."):SetPoint("TOPLEFT", 206, 0)
    layoutStatus = Label(panel, "", 14, -24, "GameFontHighlightSmall")
    layoutList = MakeList(panel, -44, 7, {
        { "Set out", 56, function(item) Popup("PLAYERHOUSING_LAYOUT_LOAD", item.name, item.id) end },
        { "Send", 42, function(item) Popup("PLAYERHOUSING_LAYOUT_SEND", item.name, item.id) end },
        { "Delete", 50, function(item) Popup("PLAYERHOUSING_LAYOUT_DELETE", item.name, item.id) end },
    })
end

API.OnData("layouts", function(list)
    if not layoutList then
        return
    end
    local items, limit = {}, 0
    for _, row in ipairs(list.rows) do
        if row[1] == "limit" then
            limit = tonumber(row[2]) or 0
        elseif row[1] == "layout" then
            items[#items + 1] = { id = tonumber(row[2]), name = row[3], pieces = tonumber(row[4]) or 0, savedAt = row[5] or "", source = row[6] or "" }
        end
    end
    layoutStatus:SetText(("%d of %d layouts saved."):format(#items, limit))
    layoutList:SetItems(items, function(row, item)
        row.icon:SetTexture("Interface\\Icons\\INV_Misc_Note_01")
        row.text:SetText(("%s |cffa0a0a0(%d pieces, %s)|r"):format(item.name, item.pieces, item.savedAt))
    end, "No saved layouts yet.")
end)

---------------------------------------------------------------------------------------------
-- Guests: who may visit when the island is private, and which of them may decorate.

local guestList, guestName

local function CreateGuests()
    local panel = MakePanel("Guests")
    guestName = InputBox(panel, "PlayerHousingGuestName", 130, 18, 0)
    local function Invite()
        local name = strtrim(guestName:GetText() or "")
        if name ~= "" then
            guestName:SetText("")
            guestName:ClearFocus()
            Do("invite " .. name, "guests")
        end
    end
    guestName:SetScript("OnEnterPressed", Invite)
    SmallButton(panel, "PlayerHousingInvite", "Invite", 56, Invite, "Invite", "Adds them to your guest list; they can visit whatever your privacy."):SetPoint("TOPLEFT", 154, 0)
    SmallButton(panel, "PlayerHousingInviteTarget", "Target", 56, function() Do("invite target", "guests") end,
        "Invite your target"):SetPoint("TOPLEFT", 212, 0)
    SmallButton(panel, "PlayerHousingInviteParty", "Party", 56, function() Do("invite party", "guests") end,
        "Invite your party"):SetPoint("TOPLEFT", 270, 0)
    guestList = MakeList(panel, -26, 8, {
        { "Roommate", 70, function(item) Do((item.roommate and "unroommate " or "roommate ") .. item.name, "guests") end },
        { "Remove", 56, function(item) Do("uninvite " .. item.name, "guests") end },
    })
end

API.OnData("guests", function(list)
    if not guestList then
        return
    end
    local items = {}
    for _, row in ipairs(list.rows) do
        if row[1] == "guest" then
            items[#items + 1] = { name = row[2], roommate = row[3] == "1" }
        end
    end
    guestList:SetItems(items, function(row, item)
        row.icon:SetTexture(item.roommate and "Interface\\Icons\\INV_Misc_Key_03" or "Interface\\Icons\\INV_Letter_02")
        row.text:SetText(item.roommate and (item.name .. " |cff40ff40roommate: can decorate|r") or item.name)
        row.buttons[1]:SetText(item.roommate and "Guest" or "Roommate")
    end, "No guests yet. Invite someone by name.")
end)

---------------------------------------------------------------------------------------------
-- Visit: islands you can go to.

local visitButtons, visitResults, visitName, likeButton = {}, nil, nil, nil

local function ShowVisitList(index)
    visitList = index
    for position, button in ipairs(visitButtons) do
        if position == index then button:LockHighlight() else button:UnlockHighlight() end
    end
    if API.IsKnown() then
        API.RequestData("visits", index - 1)
    end
end

local function CreateVisit()
    local panel = MakePanel("Visit")
    local width = (API.WIDTH - 24 - 8) / 3
    for index, name in ipairs(VISIT_LISTS) do
        local column, row = (index - 1) % 3, math.floor((index - 1) / 3)
        local button = SmallButton(panel, "PlayerHousingVisitList" .. index, name, width, function() ShowVisitList(index) end)
        button:SetPoint("TOPLEFT", 12 + column * (width + 4), -row * 20)
        visitButtons[index] = button
    end
    visitName = InputBox(panel, "PlayerHousingVisitName", 150, 18, -44)
    local function Go()
        local name = strtrim(visitName:GetText() or "")
        if name ~= "" then
            visitName:ClearFocus()
            API.Command("visit " .. name)
        end
    end
    visitName:SetScript("OnEnterPressed", Go)
    SmallButton(panel, "PlayerHousingVisitGo", "Visit", 50, Go, "Visit by name"):SetPoint("TOPLEFT", 174, -44)
    likeButton = SmallButton(panel, "PlayerHousingLike", "Like", 60, function() API.Command("like") end,
        "Like this island", "Once per account per island; click again to take it back.")
    likeButton:SetPoint("TOPRIGHT", -12, -44)
    visitResults = MakeList(panel, -68, 6, {
        { "Visit", 50, function(item) API.Command("visit " .. item.name) end },
    })
    panel:SetScript("OnShow", function()
        local visiting = API.state.islandOwner ~= "" and not API.state.own
        if visiting then likeButton:Show() else likeButton:Hide() end
    end)
end

API.OnData("visits", function(list)
    if not visitResults or tonumber(list.arg or "") ~= visitList - 1 then
        return
    end
    local items = {}
    for _, row in ipairs(list.rows) do
        if row[1] == "island" then
            items[#items + 1] = { name = row[2], likes = tonumber(row[3]) or 0 }
        end
    end
    visitResults:SetItems(items, function(row, item)
        row.icon:SetTexture("Interface\\Icons\\INV_Misc_Rune_01")
        if visitList == 6 then
            row.text:SetText(("%s |cffa0a0a0(%d %s)|r"):format(item.name, item.likes, item.likes == 1 and "like" or "likes"))
        else
            row.text:SetText(item.name)
        end
    end, "None you can visit right now.")
end)

---------------------------------------------------------------------------------------------
-- Island: privacy, greeting, weather, time of day, music.

local privacyButtons, greetingBox, weatherText, timeText, musicText, islandStats = {}, nil, nil, nil, nil, nil

local function UpdateIsland()
    if not weatherText then
        return
    end
    for index, button in ipairs(privacyButtons) do
        if index - 1 == island.privacy then button:LockHighlight() else button:UnlockHighlight() end
    end
    weatherText:SetText(island.weathers[island.weather] or "")
    timeText:SetText(island.times[island.time] or "")
    if not island.musicBox then
        musicText:SetText("|cffa0a0a0place a Music Box first|r")
    elseif island.music == 0 then
        musicText:SetText("none")
    else
        local name = island.music
        for _, track in ipairs(island.tracks) do
            if track.id == island.music then
                name = track.name
            end
        end
        musicText:SetText(name)
    end
end

local function Cycle(which, step)
    -- Weathers and times count from 0, which # leaves out.
    if which == "weather" then
        local count = #island.weathers + 1
        Do(("weather %d"):format((island.weather + step) % count), "island")
    elseif which == "time" then
        local count = #island.times + 1
        Do(("time %d"):format((island.time + step) % count), "island")
    elseif island.musicBox then
        -- None, then each tune.
        local position = 0
        for index, track in ipairs(island.tracks) do
            if track.id == island.music then
                position = index
            end
        end
        position = (position + step) % (#island.tracks + 1)
        Do(position == 0 and "music off" or ("music %d"):format(island.tracks[position].id), "island")
    end
end

local function Chooser(panel, name, label, y, which)
    Label(panel, label, 14, y - 3)
    SmallButton(panel, "PlayerHousing" .. name .. "Prev", "<", 24, function() Cycle(which, -1) end):SetPoint("TOPLEFT", 110, y)
    local text = panel:CreateFontString("PlayerHousing" .. name .. "Text", "OVERLAY", "GameFontHighlightSmall")
    text:SetPoint("TOPLEFT", 136, y - 3)
    text:SetWidth(160)
    SmallButton(panel, "PlayerHousing" .. name .. "Next", ">", 24, function() Cycle(which, 1) end):SetPoint("TOPLEFT", 298, y)
    return text
end

local function CreateIsland()
    local panel = MakePanel("Island")
    Label(panel, "Who can visit", 14, -3)
    local width = (API.WIDTH - 110 - 12 - 8) / 3
    for index, name in ipairs(PRIVACY) do
        local command = ({ "private", "friends", "public" })[index]
        local button = SmallButton(panel, "PlayerHousingPrivacy" .. index, name, width, function() Do("privacy " .. command, "island") end)
        button:SetPoint("TOPLEFT", 110 + (index - 1) * (width + 4), 0)
        privacyButtons[index] = button
    end

    Label(panel, "Greeting", 14, -31)
    greetingBox = InputBox(panel, "PlayerHousingGreeting", 150, 114, -28)
    local function SetGreeting()
        greetingBox:ClearFocus()
        Do("greeting " .. strtrim(greetingBox:GetText() or ""), "island")
    end
    greetingBox:SetScript("OnEnterPressed", SetGreeting)
    SmallButton(panel, "PlayerHousingGreetingSet", "Set", 40, SetGreeting, "Set the greeting", "What visitors read on arrival."):SetPoint("TOPLEFT", 270, -28)
    SmallButton(panel, "PlayerHousingGreetingClear", "Clear", 46, function()
        greetingBox:SetText("")
        Do("greeting clear", "island")
    end):SetPoint("TOPLEFT", 312, -28)

    weatherText = Chooser(panel, "Weather", "Weather", -58, "weather")
    timeText = Chooser(panel, "Time", "Time of day", -82, "time")
    musicText = Chooser(panel, "Music", "Music", -106, "music")

    islandStats = Label(panel, "", 14, -140, "GameFontHighlightSmall")
    SmallButton(panel, "PlayerHousingVisitorLog", "Visitor log", 80, function() API.Command("visitors") end,
        "Visitor log", "Who came lately, in your chat."):SetPoint("TOPRIGHT", -12, -136)
    Label(panel, "These are your island's; visitors see and hear them there.", 14, -170, "GameFontDisableSmall")
end

API.OnData("island", function(list)
    wipe(island.weathers)
    wipe(island.times)
    wipe(island.tracks)
    for _, row in ipairs(list.rows) do
        if row[1] == "settings" then
            island.privacy = tonumber(row[2]) or 0
            island.weather = tonumber(row[3]) or 0
            island.time = tonumber(row[4]) or 0
            island.music = tonumber(row[5]) or 0
            island.musicBox = row[6] == "1"
            if islandStats then
                islandStats:SetText(("%s likes, %s visitors this week."):format(row[7] or "0", row[8] or "0"))
            end
        elseif row[1] == "greeting" then
            if greetingBox and not greetingBox:HasFocus() then
                greetingBox:SetText(row[2] or "")
            end
        elseif row[1] == "weather" then
            island.weathers[tonumber(row[2])] = row[3]
        elseif row[1] == "time" then
            island.times[tonumber(row[2])] = row[3]
        elseif row[1] == "music" then
            island.tracks[#island.tracks + 1] = { id = tonumber(row[2]), name = row[3] }
        end
    end
    UpdateIsland()
end)

---------------------------------------------------------------------------------------------
-- The collection list feeds the Collection and Storage tabs.

API.OnData("collection", function(list)
    collection.loaded = true
    wipe(collection.unlocked)
    wipe(collection.fresh)
    wipe(collection.storage)
    wipe(collection.placed)
    for _, row in ipairs(list.rows) do
        local label = row[1]
        if label == "settings" then
            collection.free = row[2] == "1"
            collection.catalog = row[3] == "1"
            collection.unlockAll = row[6] == "1"
        elseif label == "unlocked" then
            Ranges(row[2], collection.unlocked)
        elseif label == "new" then
            Ranges(row[2], collection.fresh)
        elseif label == "storage" then
            Counts(row[2], collection.storage)
        elseif label == "placed" then
            Counts(row[2], collection.placed)
        end
    end
    UpdateCollection()
    UpdateStorage()
    API.RefreshPin()
end)

-- For the piece pinned next to the window.
function API.DescribePiece(id)
    local info = pieceById[id]
    if not info then
        return {}
    end
    local unlocked   -- nil until the server has sent the collection
    if collection.loaded then
        unlocked = collection.unlocked[id] == true
    end
    return {
        category = PlayerHousing_Categories[info[3]],
        building = info[4] == 1,
        unlocked = unlocked,
        hint = info[7],
        cost = Money(info[5]),
        storage = collection.storage[id] or 0,
        placed = collection.placed[id] or 0,
    }
end

-- The Placed tab keeps up with pieces placed and picked up.
local lastCounts
API.OnState(function(state)
    local counts = state.furnishings .. "/" .. state.buildings
    if current == "Placed" and panels.Placed and panels.Placed:IsShown() and lastCounts and counts ~= lastCounts and API.CanEdit() then
        API.RequestData("placed")
    end
    lastCounts = counts
end)

---------------------------------------------------------------------------------------------

API.OnWindow(function(window)
    frame = window
    panels.Bags = API.GetBagsPanel()
    local width = (API.WIDTH - 24 - 3 * 4) / 4
    for index, name in ipairs(TABS) do
        local button = CreateFrame("Button", "PlayerHousingTab" .. name, frame, "UIPanelButtonTemplate")
        button:SetWidth(width)
        button:SetHeight(20)
        local column, row = (index - 1) % 4, math.floor((index - 1) / 4)
        button:SetPoint("TOPLEFT", 12 + column * (width + 4), -82 - row * 22)
        button:SetText(name)
        button:SetScript("OnClick", function() PlayerHousing_SelectTab(name) end)
        tabButtons[name] = button
    end
    CreateCollection()
    CreateStorage()
    CreatePlaced()
    CreateLayouts()
    CreateGuests()
    CreateVisit()
    CreateIsland()
    ShowVisitList(1)
    PlayerHousing_SelectTab("Bags")
    frame:HookScript("OnShow", function()
        if API.IsKnown() then
            Request(current)
        end
    end)
end)
