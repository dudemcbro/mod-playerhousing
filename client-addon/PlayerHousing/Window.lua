-- The housing window's tabs: Collection, Placed, Layouts (and sets), Guests (and the
-- guestbook), Visit and Island. Each asks the server for its list when shown (.house data
-- <kind>, answered in addon messages) and acts through .house commands, so the window can't
-- do anything a player couldn't type.

local API = PlayerHousingAPI
local TABS = { "Collection", "Placed", "Layouts", "Guests", "Visit", "Island" }
local ROW_HEIGHT = 22
local GRID_COLUMNS, GRID_ROWS, SLOT = 9, 4, 40
local VISIT_LISTS = { "Party", "Guild", "Friends", "Invited", "Public", "Most liked" }
local PRIVACY = { "Private", "Friends", "Public" }   -- friends and guild, on the server
local FAVORITES, RECENT = "favorites", "recent"      -- the category picker's own entries, after All
local SORTS = { { "order", "Sort: collection" }, { "name", "Sort: name" }, { "cost", "Sort: cost" }, { "owned", "Sort: owned" } }

local frame
local tabButtons, panels = {}, {}
local current = "Collection"
local pieceById, pieceOrder = {}, {}
-- owned: how many of each the player has to place (the server's counts; nothing is a bag item).
local collection = { unlocked = {}, fresh = {}, owned = {}, placed = {}, recent = {}, free = false, catalog = false, unlockAll = false }
local collectionView = { category = 0, unlockedOnly = false, ownedOnly = false, search = "", page = 1, sort = 1 }
local island = { weathers = {}, times = {}, tracks = {}, privacy = 0, weather = 0, time = 0, music = 0, musicBox = false, door = false, newNotes = 0 }
local visitList = 1
local UpdateIsland  -- the Island tab, below; the guestbook clears its "new" count

for index, info in ipairs(PlayerHousing_Pieces or {}) do
    pieceById[info[1]] = info
    pieceOrder[info[1]] = index
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

local function CheckBox(parent, name, label, x, y, onClick)
    local check = CreateFrame("CheckButton", name, parent, "UICheckButtonTemplate")
    check:SetWidth(20)
    check:SetHeight(20)
    check:SetPoint("TOPLEFT", x, y + 1)
    check.label = Label(parent, label, x + 20, y - 3, "GameFontHighlightSmall")
    check:SetScript("OnClick", function(self) onClick(self:GetChecked() and true or false) end)
    return check
end

local function Popup(which, text, data)
    local dialog = StaticPopup_Show(which, text)
    if dialog then
        dialog.data = data
    end
end

-- A list of rows, a page at a time: an icon, a line of text and buttons on the right.
-- tooltip: optional function(item) returning the text shown while hovering a row.
local function MakeList(panel, top, rows, buttons, tooltip)
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
            end, spec[4], spec[5])
            button:SetPoint("RIGHT", row, "RIGHT", -right, 0)
            right = right + spec[2] + 2
            row.buttons[position] = button
        end
        row.text:SetWidth(width - 22 - right - 4)
        if tooltip then
            row:EnableMouse(true)
            row:SetScript("OnEnter", function(self)
                if self.item then
                    GameTooltip:SetOwner(self, "ANCHOR_TOP")
                    GameTooltip:SetText(tooltip(self.item), 1, 1, 1, 1, true)
                    GameTooltip:Show()
                end
            end)
            row:SetScript("OnLeave", GameTooltip_Hide)
        end
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
            if item and not self.hidden then
                self.render(row, item)
                row:Show()
            else
                row:Hide()
            end
        end
        self.empty:SetText(#self.items == 0 and not self.hidden and (self.emptyText or "") or "")
        self.pageText:SetText(pages > 1 and not self.hidden and ("Page %d of %d"):format(self.page, pages) or "")
        if self.page > 1 then self.prev:Enable() else self.prev:Disable() end
        if self.page < pages then self.next:Enable() else self.next:Disable() end
        if self.hidden then self.prev:Hide() self.next:Hide() else self.prev:Show() self.next:Show() end
    end

    -- Two lists can share a panel, one shown at a time.
    function list:SetHidden(hidden)
        self.hidden = hidden
        self:Refresh()
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

-- Two buttons that switch a tab between its two pages.
local function PageSwitch(panel, prefix, names, onSwitch)
    local buttons = {}
    for index, text in ipairs(names) do
        local button = SmallButton(panel, prefix .. index, text, 110, function() onSwitch(index) end)
        button:SetPoint("TOPLEFT", 12 + (index - 1) * 112, 0)
        buttons[index] = button
    end
    return function(which)
        for index, button in ipairs(buttons) do
            if index == which then button:LockHighlight() else button:UnlockHighlight() end
        end
    end
end

---------------------------------------------------------------------------------------------
-- Tabs

local layoutsPage, guestsPage = 1, 1  -- 1: layouts, guests; 2: sets, the guestbook

local function Owned(id)
    return collection.owned[id] or 0
end

local function Request(name)
    if name == "Collection" then
        API.RequestData("collection")
    elseif name == "Placed" then
        if API.CanEdit() then
            API.RequestData("placed")
        end
    elseif name == "Layouts" then
        API.RequestData(layoutsPage == 1 and "layouts" or "sets")
    elseif name == "Guests" then
        API.RequestData(guestsPage == 1 and "guests" or "guestbook")
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
-- Collection: everything there is, unlocked or not, with how many of each you own. A click
-- shows a piece in the preview with its buttons and, on the island, a ghost of it follows you
-- for any unlocked piece (one you own, else a new copy, paid for when it's set down).

local collectionSlots, categoryText, collectionPageText, collectionStatus, collectionPrev, collectionNext, sortButton, collectionCount =
    {}, nil, nil, nil, nil, nil, nil, nil

-- One to place without buying: one owned, or free (FreeMode, unlocked).
local function HaveOne(id)
    return Owned(id) > 0 or (collection.free and collection.unlocked[id]) and true or false
end

-- Right-click stars a piece; the stars are kept per character.
local function Favorites()
    PlayerHousingDB = PlayerHousingDB or {}
    PlayerHousingDB.favorites = PlayerHousingDB.favorites or {}
    return PlayerHousingDB.favorites
end

local function CollectionPieces()
    local list = {}
    local search = collectionView.search
    local favorites = Favorites()
    local category = collectionView.category
    local recentRank = {}
    for rank, id in ipairs(collection.recent) do
        recentRank[id] = rank
    end
    for _, info in ipairs(PlayerHousing_Pieces or {}) do
        local id = info[1]
        local catalog = PlayerHousing_Categories[info[3]] == "Catalog"
        if (not catalog or collection.catalog)
            and (category == 0 or category == info[3] or (category == FAVORITES and favorites[id]) or (category == RECENT and recentRank[id]))
            and (not collectionView.unlockedOnly or collection.unlocked[id])
            and (not collectionView.ownedOnly or Owned(id) > 0)
            and (search == "" or info[2]:lower():find(search, 1, true)) then
            list[#list + 1] = info
        end
    end

    local sort = SORTS[collectionView.sort][1]
    if category == RECENT then
        table.sort(list, function(left, right) return recentRank[left[1]] < recentRank[right[1]] end)
    elseif sort == "name" then
        table.sort(list, function(left, right) return left[2] < right[2] end)
    elseif sort == "cost" then
        table.sort(list, function(left, right)
            if left[5] ~= right[5] then return left[5] < right[5] end
            return pieceOrder[left[1]] < pieceOrder[right[1]]
        end)
    elseif sort == "owned" then
        table.sort(list, function(left, right)
            local leftHave, rightHave = Owned(left[1]) > 0, Owned(right[1]) > 0
            if leftHave ~= rightHave then return leftHave end
            return pieceOrder[left[1]] < pieceOrder[right[1]]
        end)
    end
    return list
end

local function CategoryName(category)
    if category == 0 then
        return "All categories"
    elseif category == FAVORITES then
        return "Favorites"
    elseif category == RECENT then
        return "Recently placed"
    end
    return PlayerHousing_Categories[category]
end

local function UpdateCollection()
    if not categoryText then
        return
    end
    categoryText:SetText(CategoryName(collectionView.category))
    sortButton:SetText(SORTS[collectionView.sort][2])

    local favorites = Favorites()
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
            local have = Owned(id)
            button.count:SetText(have > 0 and have or "")
            button.new:SetText(collection.fresh[id] and "New" or "")
            if favorites[id] then button.star:Show() else button.star:Hide() end
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
    collectionCount:SetText(("%d of %d unlocked"):format(unlocked, total))
    if #list == 0 and collectionView.category == FAVORITES then
        collectionStatus:SetText("No favorites yet: right-click a piece to star it.")
    elseif #list == 0 and collectionView.ownedOnly then
        collectionStatus:SetText("You own none of these: click a piece, then Buy 1.")
    elseif #list == 0 and collectionView.category == RECENT then
        collectionStatus:SetText("Nothing placed yet.")
    else
        collectionStatus:SetText("Click: place it (one you have follows you) or show it. Right-click: favorite.")
    end
end

-- All, Favorites, Recently placed, then each category (the Catalog only when the server
-- offers it).
local function CycleCategory(step)
    local order = { 0, FAVORITES, RECENT }
    for index, name in ipairs(PlayerHousing_Categories) do
        if name ~= "Catalog" or collection.catalog then
            order[#order + 1] = index
        end
    end
    local at = 1
    for position, category in ipairs(order) do
        if category == collectionView.category then
            at = position
        end
    end
    collectionView.category = order[(at - 1 + step) % #order + 1]
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
    GameTooltip:AddLine(("Owned: %d. Placed: %d."):format(Owned(id), collection.placed[id] or 0), 1, 1, 1, true)
    if API.CanEdit() and (HaveOne(id) or collection.unlocked[id]) then
        GameTooltip:AddLine("Click: it goes on your mouse; a click sets it down, a right-click puts it back." ..
            (HaveOne(id) and "" or " A new copy is bought when it's set down."), 0.4, 1, 0.4, true)
    else
        GameTooltip:AddLine("Click: show it next to the window, to get copies.", 0.4, 1, 0.4, true)
    end
    GameTooltip:AddLine(Favorites()[id] and "Right-click: take the star off." or "Right-click: star it as a favorite.", 0.7, 0.7, 0.7)
    GameTooltip:Show()
    API.ShowPreview({ id = id, name = info[2] })
end

local function CreateCollection()
    local panel = MakePanel("Collection")
    SmallButton(panel, "PlayerHousingCategoryPrev", "<", 24, function() CycleCategory(-1) end):SetPoint("TOPLEFT", 12, 0)
    categoryText = panel:CreateFontString("PlayerHousingCategoryText", "OVERLAY", "GameFontHighlightSmall")
    categoryText:SetPoint("TOPLEFT", 38, -3)
    categoryText:SetWidth(96)
    SmallButton(panel, "PlayerHousingCategoryNext", ">", 24, function() CycleCategory(1) end):SetPoint("TOPLEFT", 136, 0)
    sortButton = SmallButton(panel, "PlayerHousingCollectionSort", "Sort: collection", 92, function()
        collectionView.sort = collectionView.sort % #SORTS + 1
        collectionView.page = 1
        UpdateCollection()
    end, "Sort", "The Collection's own order, by name, by what a copy costs, or the ones you own first.")
    sortButton:SetPoint("TOPLEFT", 164, 0)

    local search = InputBox(panel, "PlayerHousingCollectionSearch", API.WIDTH - 280, 266, 0)
    search:SetScript("OnEnterPressed", search.ClearFocus)
    search:SetScript("OnTextChanged", function(self)
        collectionView.search = (self:GetText() or ""):lower()
        collectionView.page = 1
        UpdateCollection()
    end)

    CheckBox(panel, "PlayerHousingUnlockedOnly", "Unlocked", 12, -22, function(on)
        collectionView.unlockedOnly = on
        collectionView.page = 1
        UpdateCollection()
    end)
    CheckBox(panel, "PlayerHousingOwnedOnly", "Owned", 96, -22, function(on)
        collectionView.ownedOnly = on
        collectionView.page = 1
        UpdateCollection()
    end)
    collectionCount = panel:CreateFontString("PlayerHousingCollectionCount", "OVERLAY", "GameFontDisableSmall")
    collectionCount:SetPoint("TOPRIGHT", -14, -25)

    for index = 1, GRID_COLUMNS * GRID_ROWS do
        local button = CreateFrame("Button", "PlayerHousingCollectionSlot" .. index, panel)
        button:SetWidth(36)
        button:SetHeight(36)
        local column, row = (index - 1) % GRID_COLUMNS, math.floor((index - 1) / GRID_COLUMNS)
        button:SetPoint("TOPLEFT", 14 + column * SLOT, -46 - row * SLOT)
        button:RegisterForClicks("AnyUp")
        button.icon = button:CreateTexture(nil, "ARTWORK")
        button.icon:SetAllPoints()
        button.count = button:CreateFontString(nil, "OVERLAY", "NumberFontNormal")
        button.count:SetPoint("BOTTOMRIGHT", -2, 2)
        button.new = button:CreateFontString(nil, "OVERLAY", "GameFontGreenSmall")
        button.new:SetPoint("TOPLEFT", 1, -1)
        button.star = button:CreateTexture(nil, "OVERLAY")
        button.star:SetWidth(14)
        button.star:SetHeight(14)
        button.star:SetPoint("TOPRIGHT", 1, 1)
        button.star:SetTexture("Interface\\TargetingFrame\\UI-RaidTargetingIcon_1")
        button.star:Hide()
        button:SetPushedTexture("Interface\\Buttons\\UI-Quickslot-Depress")
        button:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square", "ADD")
        button:SetScript("OnEnter", CollectionTooltip)
        button:SetScript("OnLeave", function()
            GameTooltip_Hide()
            API.HidePreview()
        end)
        -- A click shows it next to the window and, on the island, a ghost of it follows you to
        -- where it goes; a right-click stars it.
        button:SetScript("OnClick", function(self, mouseButton)
            if not self.info then
                return
            end
            local id = self.info[1]
            if mouseButton == "RightButton" then
                local favorites = Favorites()
                favorites[id] = not favorites[id] or nil
                UpdateCollection()
                -- In Favorites, the piece under the mouse may have just gone.
                if self.info then CollectionTooltip(self) else GameTooltip_Hide() end
            else
                -- On your island, a ghost of it straight away (a new copy is paid for only when
                -- it's set down).
                API.Pin({ id = id, name = self.info[2] })
                if API.CanEdit() and (HaveOne(id) or collection.unlocked[id]) then
                    API.StartGhost(id)
                end
            end
        end)
        button:Hide()
        collectionSlots[index] = button
    end

    collectionPrev = SmallButton(panel, "PlayerHousingCollectionPrev", "<", 26, function()
        collectionView.page = collectionView.page - 1
        UpdateCollection()
    end)
    collectionPrev:SetPoint("TOPLEFT", 12, -210)
    collectionNext = SmallButton(panel, "PlayerHousingCollectionNext", ">", 26, function()
        collectionView.page = collectionView.page + 1
        UpdateCollection()
    end)
    collectionNext:SetPoint("TOPRIGHT", -12, -210)
    collectionPageText = panel:CreateFontString("PlayerHousingCollectionPageText", "OVERLAY", "GameFontHighlightSmall")
    collectionPageText:SetPoint("TOP", 0, -213)
    collectionStatus = panel:CreateFontString("PlayerHousingCollectionStatus", "OVERLAY", "GameFontNormalSmall")
    collectionStatus:SetPoint("TOPLEFT", 14, -236)
    collectionStatus:SetPoint("TOPRIGHT", -14, -236)
    panel:SetScript("OnShow", UpdateCollection)
    panel:EnableMouseWheel(true)
    panel:SetScript("OnMouseWheel", function(self, delta)
        -- Over the grid: pages.
        collectionView.page = collectionView.page - delta
        UpdateCollection()
    end)
end

---------------------------------------------------------------------------------------------
-- Placed: the pieces on the island, nearest first, as another way to find and move them.
-- With the housing window open, buildings are clickable just like furnishings.

local placedList, placedStatus, placedItems, placedSearch = nil, nil, {}, ""

local function ShowPlaced()
    if not placedList then
        return
    end
    local items = {}
    for _, item in ipairs(placedItems) do
        if placedSearch == "" or PieceName(item.item):lower():find(placedSearch, 1, true) then
            items[#items + 1] = item
        end
    end
    placedList:SetItems(items, function(row, item)
        row.icon:SetTexture(PieceIcon(item.item))
        row.text:SetText(("%s |cffa0a0a0%.0f yd|r"):format(PieceName(item.item), item.distance))
    end, placedSearch == "" and "Nothing placed yet." or "Nothing placed matches.")
end

local function CreatePlaced()
    local panel = MakePanel("Placed")
    local search = InputBox(panel, "PlayerHousingPlacedSearch", 150, 18, 0)
    search:SetScript("OnEnterPressed", search.ClearFocus)
    search:SetScript("OnTextChanged", function(self)
        placedSearch = (self:GetText() or ""):lower()
        placedList.page = 1
        ShowPlaced()
    end)
    SmallButton(panel, "PlayerHousingPlacedRefresh", "Refresh", 70, function() Request("Placed") end,
        "Refresh", "Nearest first, from where you stand now."):SetPoint("TOPRIGHT", -12, 0)
    placedStatus = Label(panel, "", 14, -24, "GameFontHighlightSmall")
    placedList = MakeList(panel, -42, 8, {
        { "Move", 46, function(item)
            API.Command("select " .. item.id)
            API.Command("ghost move " .. item.id)
        end, "Move it", "It goes on your mouse, like a right-click on it: a click sets it down." },
        { "Go", 30, function(item) API.Command("goto " .. item.id) end, "Go to it", "Takes you next to it." },
        { "Put away", 60, function(item) API.PickUpPlacement(item.id) end, "Put it away", "Back to your Collection. Undo puts it back." },
    })
    panel:SetScript("OnShow", function()
        if not API.CanEdit() then
            placedStatus:SetText("")
            wipe(placedItems)
            placedList:SetItems({}, function() end, "Go home (or to an island where you're a roommate) to see its pieces.")
        end
    end)
end

API.OnData("placed", function(list)
    if not placedList then
        return
    end
    wipe(placedItems)
    for _, row in ipairs(list.rows) do
        placedItems[#placedItems + 1] = { id = tonumber(row[1]), item = tonumber(row[2]), distance = tonumber(row[3]) or 0 }
    end
    placedStatus:SetText(("%d pieces on the island, nearest first."):format(list.total or #placedItems))
    ShowPlaced()
end)

---------------------------------------------------------------------------------------------
-- Layouts: whole islands, saved and set out again. Sets: a few pieces saved together.

local layoutList, setList, layoutStatus, layoutName, layoutSave, showLayoutsPage, copyableCheck
local layoutMenuFrame = CreateFrame("Frame", "PlayerHousingLayoutMenu", UIParent, "UIDropDownMenuTemplate")

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
StaticPopupDialogs["PLAYERHOUSING_LAYOUT_OVERWRITE"] = {
    text = "Save your island as it is now over %s?",
    button1 = SAVE or "Save", button2 = CANCEL,
    OnAccept = function(self, data) Do("layout overwrite " .. data, "layouts") end,
    timeout = 0, whileDead = 1, hideOnEscape = 1,
}
StaticPopupDialogs["PLAYERHOUSING_LAYOUT_RENAME"] = {
    text = "A new name for %s:",
    button1 = OKAY or "OK", button2 = CANCEL,
    hasEditBox = 1,
    OnAccept = function(self, data)
        local box = self.editBox or _G[self:GetName() .. "EditBox"]
        local name = strtrim(box:GetText() or "")
        if name ~= "" then
            Do(("layout rename %s %s"):format(data, name), "layouts")
        end
    end,
    timeout = 0, whileDead = 1, hideOnEscape = 1,
}
StaticPopupDialogs["PLAYERHOUSING_LAYOUT_MISSING"] = {
    text = "%s",
    button1 = "Buy them", button2 = CANCEL,
    OnAccept = function(self, data) Do("layout missing " .. data, "layouts") end,
    timeout = 0, whileDead = 1, hideOnEscape = 1,
}
StaticPopupDialogs["PLAYERHOUSING_SET_DELETE"] = {
    text = "Delete the set %s?",
    button1 = DELETE or "Delete", button2 = CANCEL,
    OnAccept = function(self, data) Do("set delete " .. data, "sets") end,
    timeout = 0, whileDead = 1, hideOnEscape = 1,
}

local function CreateLayouts()
    local panel = MakePanel("Layouts")
    local highlight = PageSwitch(panel, "PlayerHousingLayoutsPage", { "Island layouts", "Sets" }, function(which) showLayoutsPage(which) end)
    layoutName = InputBox(panel, "PlayerHousingLayoutName", 180, 18, -24)
    local function Save()
        local name = strtrim(layoutName:GetText() or "")
        if name == "" then
            API.Print(layoutsPage == 1 and "give the layout a name first." or "give the set a name first.")
            return
        end
        layoutName:SetText("")
        layoutName:ClearFocus()
        if layoutsPage == 1 then
            Do("layout save " .. name, "layouts")
        else
            Do("set save " .. name, "sets")
        end
    end
    layoutName:SetScript("OnEnterPressed", Save)
    layoutSave = SmallButton(panel, "PlayerHousingLayoutSave", "Save as new", 110, Save, function()
        if layoutsPage == 1 then
            return "Save the island", "Every piece where it stands, under the name on the left."
        end
        return "Save the selection", "The selected pieces (Ctrl-right-click to select several) and what stands on them, as a set."
    end)
    layoutSave:SetPoint("TOPLEFT", 206, -24)
    layoutStatus = Label(panel, "", 14, -48, "GameFontHighlightSmall")
    -- Set out, and the rest in a small menu: save over it, rename, send, buy what's missing, delete.
    local function LayoutMenu(item)
        local menu = {
            { text = item.name, isTitle = true, notCheckable = true },
            { text = "Save the island over it", notCheckable = true, func = function() Popup("PLAYERHOUSING_LAYOUT_OVERWRITE", item.name, item.id) end },
            { text = "Rename", notCheckable = true, func = function() Popup("PLAYERHOUSING_LAYOUT_RENAME", item.name, item.id) end },
            { text = "Send to someone", notCheckable = true, func = function() Popup("PLAYERHOUSING_LAYOUT_SEND", item.name, item.id) end },
            { text = "Delete", notCheckable = true, func = function() Popup("PLAYERHOUSING_LAYOUT_DELETE", item.name, item.id) end },
        }
        if item.missing > 0 then
            table.insert(menu, 2, { text = ("Buy the %d pieces it needs (%s)"):format(item.missing, Money(item.cost)), notCheckable = true,
                func = function()
                    Popup("PLAYERHOUSING_LAYOUT_MISSING", ("Buy the %d pieces %s needs for %s? They go into your Collection."):format(
                        item.missing, item.name, Money(item.cost)), item.id)
                end })
        end
        EasyMenu(menu, layoutMenuFrame, "cursor", 0, 0, "MENU")
    end
    layoutList = MakeList(panel, -66, 7, {
        { "Set out", 56, function(item) Popup("PLAYERHOUSING_LAYOUT_LOAD", item.name, item.id) end },
        { "More", 44, LayoutMenu, "More", "Save over it, rename, send, buy what it needs, delete." },
    }, function(item)
        local text = ("%s: %d pieces, saved %s"):format(item.name, item.pieces, item.savedAt)
        if item.source ~= "" then
            text = text .. ", from " .. item.source
        end
        if item.missing > 0 then
            text = text .. ("\nSetting it out needs %d more (%s); Buy them under More."):format(item.missing, Money(item.cost))
        end
        if item.locked > 0 then
            text = text .. ("\n%d of its pieces are still locked."):format(item.locked)
        end
        return text
    end)
    copyableCheck = CheckBox(panel, "PlayerHousingLayoutCopyable", "Visitors may copy it", 236, -46, function(on)
        Do("layout copyable " .. (on and "on" or "off"), "layouts")
    end)
    local setsPage = CreateFrame("Frame", "PlayerHousingSetsPage", panel)
    setsPage:SetAllPoints()
    setList = MakeList(setsPage, -66, 7, {
        { "Place", 50, function(item) API.Command("set place " .. item.id) end, "Set it down",
          "The whole set goes on your mouse, like one piece: a click sets it down, the wheel turns it. Pieces you don't own are bought." },
        { "Delete", 50, function(item) Popup("PLAYERHOUSING_SET_DELETE", item.name, item.id) end },
    })

    function showLayoutsPage(which)
        layoutsPage = which
        highlight(which)
        layoutList:SetHidden(which ~= 1)
        setList:SetHidden(which ~= 2)
        layoutSave:SetText(which == 1 and "Save as new" or "Save selection")
        layoutStatus:SetText("")
        if which == 1 then
            copyableCheck:Show()
            copyableCheck.label:Show()
        else
            copyableCheck:Hide()
            copyableCheck.label:Hide()
        end
        if API.IsKnown() and current == "Layouts" then
            Request("Layouts")
        end
    end
    showLayoutsPage(1)
end

API.OnData("layouts", function(list)
    if not layoutList then
        return
    end
    local items, limit, copyable = {}, 0, false
    for _, row in ipairs(list.rows) do
        if row[1] == "limit" then
            limit = tonumber(row[2]) or 0
            copyable = row[3] == "1"
        elseif row[1] == "layout" then
            items[#items + 1] = { id = tonumber(row[2]), name = row[3], pieces = tonumber(row[4]) or 0, savedAt = row[5] or "", source = row[6] or "",
                                  missing = tonumber(row[7] or "") or 0, cost = tonumber(row[8] or "") or 0, locked = tonumber(row[9] or "") or 0 }
        end
    end
    if layoutsPage == 1 then
        layoutStatus:SetText(("%d of %d layouts saved."):format(#items, limit))
    end
    copyableCheck:SetChecked(copyable)
    layoutList:SetItems(items, function(row, item)
        row.icon:SetTexture("Interface\\Icons\\INV_Misc_Note_01")
        local needs = item.missing > 0 and (" |cffffd000needs %d|r"):format(item.missing) or ""
        row.text:SetText(("%s |cffa0a0a0(%d pieces, %s)|r%s"):format(item.name, item.pieces, item.savedAt, needs))
    end, "No saved layouts yet.")
end)

API.OnData("sets", function(list)
    if not setList then
        return
    end
    local items, limit = {}, 0
    for _, row in ipairs(list.rows) do
        if row[1] == "limit" then
            limit = tonumber(row[2]) or 0
        elseif row[1] == "set" then
            items[#items + 1] = { id = tonumber(row[2]), name = row[3], pieces = tonumber(row[4]) or 0, savedAt = row[5] or "" }
        end
    end
    if layoutsPage == 2 then
        layoutStatus:SetText(("%d of %d sets. Select pieces (Ctrl-right-click), name them, Save selection."):format(#items, limit))
    end
    setList:SetItems(items, function(row, item)
        row.icon:SetTexture("Interface\\Icons\\INV_Crate_02")
        row.text:SetText(("%s |cffa0a0a0(%d pieces, %s)|r"):format(item.name, item.pieces, item.savedAt))
    end, "No sets yet.")
end)

---------------------------------------------------------------------------------------------
-- Guests: who may visit when the island is private, and which of them may decorate. The
-- guestbook: notes visitors left.

local guestList, noteList, guestName, guestControls, guestbookStatus, showGuestsPage

StaticPopupDialogs["PLAYERHOUSING_PACKUP"] = {
    text = "Pack up every piece on your island? They go back into your Collection, and Undo puts them all back.",
    button1 = "Pack up", button2 = CANCEL,
    OnAccept = function() API.Command("packup") end,
    timeout = 0, whileDead = 1, hideOnEscape = 1,
}
StaticPopupDialogs["PLAYERHOUSING_NOTE_DELETE"] = {
    text = "Throw out %s's note?",
    button1 = DELETE or "Delete", button2 = CANCEL,
    OnAccept = function(self, data) Do("guestbook delete " .. data, "guestbook") end,
    timeout = 0, whileDead = 1, hideOnEscape = 1,
}

local function CreateGuests()
    local panel = MakePanel("Guests")
    local highlight = PageSwitch(panel, "PlayerHousingGuestsPage", { "Guests", "Guestbook" }, function(which) showGuestsPage(which) end)
    guestControls = CreateFrame("Frame", "PlayerHousingGuestControls", panel)
    guestControls:SetAllPoints()
    guestName = InputBox(guestControls, "PlayerHousingGuestName", 130, 18, -24)
    local function Invite()
        local name = strtrim(guestName:GetText() or "")
        if name ~= "" then
            guestName:SetText("")
            guestName:ClearFocus()
            Do("invite " .. name, "guests")
        end
    end
    guestName:SetScript("OnEnterPressed", Invite)
    SmallButton(guestControls, "PlayerHousingInvite", "Invite", 56, Invite, "Invite", "Adds them to your guest list; they can visit whatever your privacy."):SetPoint("TOPLEFT", 154, -24)
    SmallButton(guestControls, "PlayerHousingInviteTarget", "Target", 56, function() Do("invite target", "guests") end,
        "Invite your target"):SetPoint("TOPLEFT", 212, -24)
    SmallButton(guestControls, "PlayerHousingInviteParty", "Party", 56, function() Do("invite party", "guests") end,
        "Invite your party"):SetPoint("TOPLEFT", 270, -24)
    guestList = MakeList(panel, -48, 8, {
        { "Roommate", 70, function(item) Do((item.roommate and "unroommate " or "roommate ") .. item.name, "guests") end },
        { "Remove", 56, function(item) Do("uninvite " .. item.name, "guests") end },
    })

    local notesPage = CreateFrame("Frame", "PlayerHousingGuestbookPage", panel)
    notesPage:SetAllPoints()
    guestbookStatus = Label(notesPage, "", 14, -27, "GameFontHighlightSmall")
    noteList = MakeList(notesPage, -48, 8, {
        { "Delete", 50, function(item) Popup("PLAYERHOUSING_NOTE_DELETE", item.author, item.id) end },
    }, function(item) return ("%s, %s:\n%s"):format(item.author, item.when, item.text) end)

    function showGuestsPage(which)
        guestsPage = which
        highlight(which)
        if which == 1 then guestControls:Show() else guestControls:Hide() end
        guestList:SetHidden(which ~= 1)
        noteList:SetHidden(which ~= 2)
        guestbookStatus:SetText("")
        if API.IsKnown() and current == "Guests" then
            Request("Guests")
        end
    end
    showGuestsPage(1)
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

API.OnData("guestbook", function(list)
    if not noteList then
        return
    end
    local items, fresh = {}, 0
    for _, row in ipairs(list.rows) do
        if row[1] == "note" then
            items[#items + 1] = { id = tonumber(row[2]), author = row[3], when = row[4], fresh = row[5] == "1", text = row[6] or "" }
            if row[5] == "1" then
                fresh = fresh + 1
            end
        elseif row[1] == "more" then
            -- The rest of a note too long for one message.
            local item = items[#items]
            if item and item.id == tonumber(row[2]) then
                item.text = item.text .. (row[3] or "")
            end
        end
    end
    island.newNotes = 0
    UpdateIsland()
    guestbookStatus:SetText(#items == 0 and "" or (fresh > 0 and ("%d notes, %d new. Hover one to read it all."):format(#items, fresh)
        or ("%d notes. Hover one to read it all."):format(#items)))
    noteList:SetItems(items, function(row, item)
        row.icon:SetTexture("Interface\\Icons\\INV_Misc_Note_02")
        row.text:SetText(("%s%s: |cffffffff%s|r"):format(item.fresh and "|cff40ff40new|r " or "", item.author, item.text))
    end, "No notes yet: visitors sign it in this window's Visit tab.")
end)

---------------------------------------------------------------------------------------------
-- Visit: islands you can go to, and (while visiting) the island's guestbook and likes.

local visitButtons, visitResults, visitName, likeButton, copyButton, signBox, signButton, signLabel = {}, nil, nil, nil, nil, nil, nil, nil

local function ShowVisitList(index)
    visitList = index
    for position, button in ipairs(visitButtons) do
        if position == index then button:LockHighlight() else button:UnlockHighlight() end
    end
    if API.IsKnown() then
        API.RequestData("visits", index - 1)
    end
end

local function UpdateVisitControls()
    if not likeButton then
        return
    end
    local visiting = API.state.islandOwner ~= "" and not API.state.own
    for _, region in ipairs({ likeButton, copyButton, signBox, signButton, signLabel }) do
        if visiting then region:Show() else region:Hide() end
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
    copyButton = SmallButton(panel, "PlayerHousingCopyLayout", "Copy", 50, function() API.Command("layout copy") end,
        "Save a copy of this island's layout", "Into your Layouts, when its owner lets visitors copy it.")
    copyButton:SetPoint("RIGHT", likeButton, "LEFT", -4, 0)

    signLabel = Label(panel, "Guestbook", 14, -71, "GameFontNormalSmall")
    signBox = InputBox(panel, "PlayerHousingSignNote", 214, 82, -68)
    local function Sign()
        local note = strtrim(signBox:GetText() or "")
        if note ~= "" then
            signBox:SetText("")
            signBox:ClearFocus()
            API.Command("sign " .. note)
        end
    end
    signBox:SetScript("OnEnterPressed", Sign)
    signButton = SmallButton(panel, "PlayerHousingSign", "Sign", 50, Sign, "Sign the guestbook",
        "A short note for the island's owner, once a day.")
    signButton:SetPoint("TOPRIGHT", -12, -68)

    visitResults = MakeList(panel, -92, 5, {
        { "Visit", 50, function(item) API.Command("visit " .. item.name) end },
    })
    panel:SetScript("OnShow", UpdateVisitControls)
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
-- Island: privacy, greeting, weather, time of day, music, the door and the guestbook.

local privacyButtons, greetingBox, weatherText, timeText, musicText, islandStats, doorText, guestbookButton =
    {}, nil, nil, nil, nil, nil, nil, nil

function UpdateIsland()
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
    doorText:SetText(island.door and "Visitors arrive at your door." or "Visitors arrive at the landing spot.")
    guestbookButton:SetText(island.newNotes > 0 and ("Guestbook (%d new)"):format(island.newNotes) or "Guestbook")
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

    doorText = Label(panel, "", 14, -137, "GameFontHighlightSmall")
    SmallButton(panel, "PlayerHousingDoorHere", "Door here", 76, function() Do("door here", "island") end,
        "The door", "Visitors arrive where you stand now, facing the way you face."):SetPoint("TOPRIGHT", -92, -134)
    SmallButton(panel, "PlayerHousingDoorReset", "Landing", 76, function() Do("door reset", "island") end,
        "The landing spot", "Visitors arrive at the landing spot again."):SetPoint("TOPRIGHT", -12, -134)

    islandStats = Label(panel, "", 14, -165, "GameFontHighlightSmall")
    guestbookButton = SmallButton(panel, "PlayerHousingGuestbookButton", "Guestbook", 110, function()
        PlayerHousing_SelectTab("Guests")
        showGuestsPage(2)
    end, "Guestbook", "Notes visitors left.")
    guestbookButton:SetPoint("TOPRIGHT", -94, -162)
    SmallButton(panel, "PlayerHousingVisitorLog", "Visitor log", 80, function() API.Command("visitors") end,
        "Visitor log", "Who came lately, in your chat."):SetPoint("TOPRIGHT", -12, -162)
    Label(panel, "These are your island's; visitors see and hear them there.", 14, -194, "GameFontDisableSmall")

    -- The rest of what the old House Key menu had.
    SmallButton(panel, "PlayerHousingPackUp", "Pack up all", 90, function() StaticPopup_Show("PLAYERHOUSING_PACKUP") end,
        "Pack up everything", "Every piece back into your Collection (mannequins' gear to your bags). Undo puts it all back."):SetPoint("TOPLEFT", 12, -214)
    SmallButton(panel, "PlayerHousingUnstuck", "Unstuck", 70, function() API.Command("unstuck") end,
        "Unstuck", "Back to the island's landing spot."):SetPoint("TOPLEFT", 106, -214)
    SmallButton(panel, "PlayerHousingNewKey", "New House Key", 110, function() API.Command("key") end,
        "A new House Key", "If yours is gone. Only one at a time."):SetPoint("TOPRIGHT", -12, -214)
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
            island.door = row[9] == "1"
            island.newNotes = tonumber(row[10] or "") or 0
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
-- The collection list feeds the Collection tab and the pinned piece.

API.OnData("collection", function(list)
    collection.loaded = true
    wipe(collection.unlocked)
    wipe(collection.fresh)
    wipe(collection.owned)
    wipe(collection.placed)
    wipe(collection.recent)
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
            Counts(row[2], collection.owned)
        elseif label == "placed" then
            Counts(row[2], collection.placed)
        elseif label == "recent" then
            for id in (row[2] or ""):gmatch("%d+") do
                collection.recent[#collection.recent + 1] = tonumber(id)
            end
        end
    end
    UpdateCollection()
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
        owned = Owned(id),
        placed = collection.placed[id] or 0,
    }
end

-- The Placed tab keeps up with pieces placed and picked up, and the Collection with the counts
-- they change; the Collection also with arriving on an island (its clicks place pieces there).
local lastCounts, lastCanEdit
API.OnState(function(state)
    local counts = state.furnishings .. "/" .. state.buildings
    if lastCounts and counts ~= lastCounts and API.CanEdit() then
        if current == "Placed" and panels.Placed and panels.Placed:IsShown() then
            API.RequestData("placed")
        elseif current == "Collection" and panels.Collection and panels.Collection:IsShown() then
            API.RequestData("collection")
        end
    end
    lastCounts = counts
    if lastCanEdit ~= API.CanEdit() then
        lastCanEdit = API.CanEdit()
        UpdateCollection()
    end
    UpdateVisitControls()
end)

---------------------------------------------------------------------------------------------

API.OnWindow(function(window)
    frame = window
    local width = (API.WIDTH - 24 - 2 * 4) / 3
    for index, name in ipairs(TABS) do
        local button = CreateFrame("Button", "PlayerHousingTab" .. name, frame, "UIPanelButtonTemplate")
        button:SetWidth(width)
        button:SetHeight(20)
        local column, row = (index - 1) % 3, math.floor((index - 1) / 3)
        button:SetPoint("TOPLEFT", 12 + column * (width + 4), -82 - row * 22)
        button:SetText(name)
        button:SetScript("OnClick", function() PlayerHousing_SelectTab(name) end)
        tabButtons[name] = button
    end
    CreateCollection()
    CreatePlaced()
    CreateLayouts()
    CreateGuests()
    CreateVisit()
    CreateIsland()
    ShowVisitList(1)
    PlayerHousing_SelectTab("Collection")
    frame:HookScript("OnShow", function()
        if API.IsKnown() then
            Request(current)
        end
    end)
end)
