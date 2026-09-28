-- Player Housing: an optional window for mod-playerhousing.
--
-- Every button sends one of the .house chat commands a player could type, so nothing here
-- is needed to play: the House Key menus do all of it. What the addon adds is a view of the
-- furnishings in your bags as icons (click one, then click where it goes), undo and redo
-- one click away, and quick controls for the piece you're working on, including turning it
-- with the mouse wheel.
--
-- The server whispers the island state to the player as an addon message with the prefix
-- HOUSING (PlayerHousingMgr::SendAddonState): tab separated, new fields only at the end.

local PREFIX = "HOUSING"

local COLUMNS, ROWS = 8, 4
local SLOT_SIZE, SLOT_GAP = 36, 4
local PAGE_SIZE = COLUMNS * ROWS
local WIDTH = 24 + COLUMNS * (SLOT_SIZE + SLOT_GAP)
local BASE_HEIGHT, SELECTED_HEIGHT = 356, 144
local SEND_INTERVAL = 0.3   -- seconds between mouse wheel commands; turns in between add up

BINDING_HEADER_PLAYERHOUSING = "Player Housing"
BINDING_NAME_PLAYERHOUSING_TOGGLE = "Show or hide the housing window"
BINDING_NAME_PLAYERHOUSING_UNDO = "Undo"
BINDING_NAME_PLAYERHOUSING_REDO = "Redo"
BINDING_NAME_PLAYERHOUSING_DECORATE = "Start or stop decorating"
BINDING_NAME_PLAYERHOUSING_TURN_LEFT = "Turn the selected piece left"
BINDING_NAME_PLAYERHOUSING_TURN_RIGHT = "Turn the selected piece right"
BINDING_NAME_PLAYERHOUSING_SELECT_NEAREST = "Select the nearest piece"

local state = {
    own = false, decorating = false,
    selected = 0, selectedName = "", selectedBuilding = false,
    furnishings = 0, maxFurnishings = 0, buildings = 0, maxBuildings = 0,
    undo = "", redo = "", islandOwner = "",
    pendingMover = 0, pendingCopy = 0,
    roommate = false,       -- decorating someone else's island, with their leave
}

-- Changing things: on your own island, or as a roommate on someone else's.
local function CanEdit()
    return state.own or state.roommate
end

local db                    -- PlayerHousingDB, once loaded
local known = false         -- the server has housing: it sent us a state
local pieces = {}           -- housing items in the bags, one entry per kind of item
local movers = {}           -- Move a Piece items in the bags: item id to "bag slot"
local filter = "all"        -- all, furnishings or buildings
local search = ""
local page = 1
local bagsDirty, layoutPending = true, false
local autoShown = false
local pendingTurn, pendingLift, sinceSend = 0, 0, 0

local frame, statusText, grid, emptyText, pageText, selectedPanel, selectedText
local preview, previewModel, previewName, previewSize, previewNote
local plan, planRect, planBorder, planYou, planYouLabel
local previewFacing = 0
local homeButton, decorateButton, undoButton, redoButton, prevButton, nextButton
local pickUpAllButton, spotButton
local filterButtons = {}
local slots = {}

local function Print(text)
    DEFAULT_CHAT_FRAME:AddMessage("|cffffd000Housing:|r " .. text)
end

-- Move a Piece: one item per size of targeting circle, handed out by .house move.
local function IsMoverItem(id)
    return id >= 901190 and id <= 901199
end

-- Furnishings and buildings from the module's content generator; 902000 is the House Key.
local function IsHousingItem(id)
    return ((id >= 901100 and id <= 901199) or (id >= 902001 and id <= 902999)) and not IsMoverItem(id)
end

function PlayerHousing_Command(command)
    if not known then
        Print("this server hasn't reported player housing yet. Your House Key has every option.")
        return
    end
    if command == nil or command == "" then
        SendChatMessage(".house", "SAY")
    else
        SendChatMessage(".house " .. command, "SAY")
    end
end

local function Command(command)
    return function() PlayerHousing_Command(command) end
end

---------------------------------------------------------------------------------------------
-- Bags

local function ScanBags()
    wipe(pieces)
    wipe(movers)
    local byId = {}
    for bag = 0, NUM_BAG_SLOTS do
        for slot = 1, GetContainerNumSlots(bag) do
            local link = GetContainerItemLink(bag, slot)
            local id = link and tonumber(link:match("item:(%d+)"))
            if id and IsMoverItem(id) then
                movers[id] = bag .. " " .. slot
            elseif id and IsHousingItem(id) then
                local texture, count = GetContainerItemInfo(bag, slot)
                local entry = byId[id]
                if entry then
                    entry.count = entry.count + (count or 1)
                else
                    local fullName = link:match("%[(.-)%]") or ""
                    local name = (fullName:gsub("^Furnishing: ", ""):gsub("^Building: ", ""))
                    entry = {
                        id = id, name = name, lowerName = name:lower(),
                        building = fullName:find("^Building: ") ~= nil,
                        bag = bag, slot = slot, count = count or 1, texture = texture,
                    }
                    byId[id] = entry
                    pieces[#pieces + 1] = entry
                end
            end
        end
    end
    table.sort(pieces, function(left, right)
        if left.building ~= right.building then
            return not left.building
        end
        return left.name < right.name
    end)
    bagsDirty = false
end

local function FilteredPieces()
    local list = {}
    for _, piece in ipairs(pieces) do
        local kindMatches = filter == "all" or (filter == "buildings") == piece.building
        if kindMatches and (search == "" or piece.lowerName:find(search, 1, true)) then
            list[#list + 1] = piece
        end
    end
    return list
end

local function PieceLocation(id)
    for _, piece in ipairs(pieces) do
        if piece.id == id then
            return piece.bag .. " " .. piece.slot
        end
    end
end

-- While a move or a copy waits for its spot, a button uses the item (Move a Piece, or the
-- copy), so there's no need to find it in the bags. It's a secure button too.
local function UpdateSpotButton()
    local location
    if CanEdit() and state.pendingMover > 0 then
        location = movers[state.pendingMover]
        spotButton:SetText("Now pick the spot")
    elseif CanEdit() and state.pendingCopy > 0 then
        location = PieceLocation(state.pendingCopy)
        spotButton:SetText("Now place the copy")
    end
    if location then
        spotButton:SetAttribute("type", "item")
        spotButton:SetAttribute("item", location)
        spotButton:Show()
    else
        spotButton:SetAttribute("item", nil)
        spotButton:Hide()
    end
end

-- The grid holds secure buttons, which can only change out of combat.
local function UpdateGrid()
    if not frame then
        return
    end
    if InCombatLockdown() then
        layoutPending = true
        return
    end
    layoutPending = false
    if bagsDirty then
        ScanBags()
    end

    local list = FilteredPieces()
    local pages = math.max(1, math.ceil(#list / PAGE_SIZE))
    page = math.min(math.max(page, 1), pages)

    for index, button in ipairs(slots) do
        local piece = list[(page - 1) * PAGE_SIZE + index]
        button.piece = piece
        if piece then
            button:SetAttribute("type", "item")
            button:SetAttribute("item", piece.bag .. " " .. piece.slot)
            button.icon:SetTexture(piece.texture or "Interface\\Icons\\INV_Misc_QuestionMark")
            button.count:SetText(piece.count > 1 and piece.count or "")
            button:Show()
        else
            button:SetAttribute("item", nil)
            button:Hide()
        end
    end

    if #pieces == 0 then
        emptyText:SetText("No furnishings in your bags.\nOpen your Collection to get some.")
    elseif #list == 0 then
        emptyText:SetText("Nothing in your bags matches.")
    else
        emptyText:SetText("")
    end
    pageText:SetText(("Page %d of %d"):format(page, pages))
    if page > 1 then prevButton:Enable() else prevButton:Disable() end
    if page < pages then nextButton:Enable() else nextButton:Disable() end
    UpdateSpotButton()
end

---------------------------------------------------------------------------------------------
-- State from the server

local function UpdateButtons()
    if not frame then
        return
    end

    local onIsland = state.own or state.islandOwner ~= ""
    if not known then
        statusText:SetText("Waiting for the server. Your House Key has every option.")
    elseif state.own then
        statusText:SetText(("Your island: %d/%d furnishings, %d/%d buildings.%s"):format(
            state.furnishings, state.maxFurnishings, state.buildings, state.maxBuildings,
            state.decorating and "  |cff40ff40Decorating|r" or ""))
    elseif state.roommate then
        statusText:SetText(("Roommate on %s's island: %d/%d furnishings, %d/%d buildings.%s"):format(
            state.islandOwner, state.furnishings, state.maxFurnishings, state.buildings, state.maxBuildings,
            state.decorating and "  |cff40ff40Decorating|r" or ""))
    elseif state.islandOwner ~= "" then
        statusText:SetText(("Visiting %s's island."):format(state.islandOwner))
    else
        statusText:SetText("Go home to start decorating.")
    end

    homeButton:SetText(onIsland and "Leave" or "Go home")
    decorateButton:SetText(state.decorating and "Done" or "Decorate")
    if CanEdit() then decorateButton:Enable() else decorateButton:Disable() end
    if CanEdit() and state.undo ~= "" then undoButton:Enable() else undoButton:Disable() end
    if CanEdit() and state.redo ~= "" then redoButton:Enable() else redoButton:Disable() end

    local showSelected = CanEdit() and state.selected > 0
    if showSelected then
        selectedText:SetText(("Selected: |cffffffff%s|r"):format(state.selectedName))
        if state.selectedBuilding then
            pickUpAllButton:Show()
        else
            pickUpAllButton:Hide()
        end
        selectedPanel:Show()
    else
        selectedPanel:Hide()
    end

    -- Resizing a window that holds secure buttons also waits for the end of combat.
    if not InCombatLockdown() then
        frame:SetHeight(BASE_HEIGHT + (showSelected and SELECTED_HEIGHT or 0))
        if bagsDirty then
            UpdateGrid()
        else
            UpdateSpotButton()
        end
    else
        layoutPending = true
    end
end

local function SetShown(show)
    if InCombatLockdown() then
        return false
    end
    if show then
        frame:Show()
    else
        frame:Hide()
    end
    return true
end

local function OnState(fields)
    local wasOwn = state.own
    known = true
    if db then
        db.known = true
    end

    state.own = fields[2] == "1"
    state.decorating = fields[3] == "1"
    state.selected = tonumber(fields[4] or "") or 0
    state.selectedName = fields[5] or ""
    state.furnishings = tonumber(fields[6] or "") or 0
    state.maxFurnishings = tonumber(fields[7] or "") or 0
    state.buildings = tonumber(fields[8] or "") or 0
    state.maxBuildings = tonumber(fields[9] or "") or 0
    state.undo = fields[10] or ""
    state.islandOwner = fields[11] or ""
    state.redo = fields[12] or ""
    state.selectedBuilding = fields[13] == "1"
    state.pendingMover = tonumber(fields[14] or "") or 0
    state.pendingCopy = tonumber(fields[15] or "") or 0
    state.roommate = fields[16] == "1"

    -- The window comes up by itself on arriving home, and goes again on leaving.
    if not frame then
        return
    end
    if state.own and not wasOwn and db and db.autoShow and not frame:IsShown() then
        autoShown = SetShown(true)
    elseif not state.own and state.islandOwner == "" and autoShown then
        if SetShown(false) then
            autoShown = false
        end
    end
    UpdateButtons()
end

---------------------------------------------------------------------------------------------
-- The window

local buttonCount = 0

local function ShowButtonTooltip(self)
    local title, text = self.tooltipTitle, self.tooltipText
    if type(title) == "function" then
        title, text = title()
    end
    if not title then
        return
    end
    GameTooltip:SetOwner(self, "ANCHOR_TOP")
    GameTooltip:SetText(title)
    if text and text ~= "" then
        GameTooltip:AddLine(text, 1, 1, 1, true)
    end
    GameTooltip:Show()
end

local function MakeButton(parent, text, width, onClick, tooltipTitle, tooltipText)
    buttonCount = buttonCount + 1
    local button = CreateFrame("Button", "PlayerHousingButton" .. buttonCount, parent, "UIPanelButtonTemplate")
    button:SetWidth(width)
    button:SetHeight(22)
    button:SetText(text)
    button:SetScript("OnClick", onClick)
    button.tooltipTitle, button.tooltipText = tooltipTitle, tooltipText
    button:SetScript("OnEnter", ShowButtonTooltip)
    button:SetScript("OnLeave", GameTooltip_Hide)
    return button
end

-- Buttons side by side, filling the window's width.
local function Row(parent, y, buttons)
    local gap = 4
    local width = (WIDTH - 24 - gap * (#buttons - 1)) / #buttons
    local made = {}
    for index, spec in ipairs(buttons) do
        local button = MakeButton(parent, spec[1], width, spec[2], spec[3], spec[4])
        button:SetPoint("TOPLEFT", parent, "TOPLEFT", 12 + (index - 1) * (width + gap), y)
        made[index] = button
    end
    return made
end

local function SetFilter(which)
    filter = which
    page = 1
    for key, button in pairs(filterButtons) do
        if key == which then button:LockHighlight() else button:UnlockHighlight() end
    end
    UpdateGrid()
end

local function Turn(degrees)
    pendingTurn = pendingTurn + degrees
end

-- The turn buttons: 15 degrees, Shift for 5, Ctrl for 90.
local function TurnButton(direction)
    return function()
        local step = IsShiftKeyDown() and 5 or (IsControlKeyDown() and 90 or 15)
        Turn(direction * step)
    end
end

local function Lift(yards)
    pendingLift = pendingLift + yards
end

local function OnMouseWheel(self, delta)
    if CanEdit() and state.selected > 0 then
        if IsControlKeyDown() then
            Lift(delta * 0.1)
        elseif IsShiftKeyDown() then
            Turn(delta * 5)
        else
            Turn(delta * 15)
        end
    elseif not InCombatLockdown() then
        page = page - delta
        UpdateGrid()
    end
end

StaticPopupDialogs["PLAYERHOUSING_PICKUP_BUILDING"] = {
    text = "Pick up %s?\n\nThe building goes back to your bags. Anything inside stays where it is. Undo puts it back.",
    button1 = "Pick up",
    button2 = CANCEL,
    OnAccept = function(self, data) PlayerHousing_Command("pickup " .. data) end,
    timeout = 0, whileDead = 1, hideOnEscape = 1,
}

StaticPopupDialogs["PLAYERHOUSING_PICKUP_BUILDING_ALL"] = {
    text = "Pick up %s and everything inside it?\n\nIt all goes back to your bags. Undo puts it back.",
    button1 = "Pick up all",
    button2 = CANCEL,
    OnAccept = function(self, data) PlayerHousing_Command("pickup " .. data .. " inside") end,
    timeout = 0, whileDead = 1, hideOnEscape = 1,
}

local function PickUp(withInside)
    if state.selected == 0 then
        return
    end
    if not state.selectedBuilding then
        PlayerHousing_Command("pickup " .. state.selected)
        return
    end
    local which = withInside and "PLAYERHOUSING_PICKUP_BUILDING_ALL" or "PLAYERHOUSING_PICKUP_BUILDING"
    local dialog = StaticPopup_Show(which, state.selectedName)
    if dialog then
        dialog.data = state.selected
    end
end

local function SavePosition()
    if not db then
        return
    end
    local point, _, relativePoint, x, y = frame:GetPoint(1)
    db.point = { point, relativePoint, x, y }
end

---------------------------------------------------------------------------------------------
-- Preview: the piece's model, slowly turning, and its size, while hovering its icon.
-- PlayerHousing_Models (PieceModels.lua) comes from the module's content builder.

local function Yards(value)
    if value < 1 then
        return "under 1 yd"
    end
    return ("%d yd"):format(math.floor(value + 0.5))
end

local function CreatePreview()
    preview = CreateFrame("Frame", "PlayerHousingPreview", UIParent)
    preview:SetWidth(220)
    preview:SetHeight(268)
    preview:SetPoint("TOPRIGHT", frame, "TOPLEFT", -4, 0)
    preview:SetFrameStrata("DIALOG")
    preview:SetBackdrop({
        bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        tile = true, tileSize = 32, edgeSize = 16,
        insets = { left = 4, right = 4, top = 4, bottom = 4 },
    })

    previewName = preview:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    previewName:SetPoint("TOPLEFT", 10, -10)
    previewName:SetPoint("TOPRIGHT", -10, -10)

    previewModel = CreateFrame("PlayerModel", "PlayerHousingPreviewModel", preview)
    previewModel:SetPoint("TOPLEFT", 10, -28)
    previewModel:SetWidth(200)
    previewModel:SetHeight(190)

    previewNote = preview:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    previewNote:SetPoint("TOP", previewModel, "TOP", 0, -2)
    previewNote:SetWidth(180)

    -- The floor plan: what world model buildings get instead of a model.
    plan = CreateFrame("Frame", "PlayerHousingPreviewPlan", preview)
    plan:SetAllPoints(previewModel)
    planBorder = plan:CreateTexture("PlayerHousingPreviewPlanBorder", "BORDER")
    planBorder:SetTexture(0.15, 0.1, 0.05, 1)
    planRect = plan:CreateTexture("PlayerHousingPreviewPlanRect", "ARTWORK")
    planRect:SetTexture(0.6, 0.45, 0.25, 0.9)
    planYou = plan:CreateTexture("PlayerHousingPreviewPlanYou", "OVERLAY")
    planYou:SetTexture("Interface\\Minimap\\MinimapArrow")
    planYouLabel = plan:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    planYouLabel:SetPoint("LEFT", planYou, "RIGHT", 2, 0)
    plan:Hide()

    previewSize = preview:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    previewSize:SetPoint("BOTTOMLEFT", 10, 12)
    previewSize:SetPoint("BOTTOMRIGHT", -10, 12)

    preview:SetScript("OnUpdate", function(self, elapsed)
        previewFacing = (previewFacing + elapsed * 0.6) % (2 * math.pi)
        previewModel:SetFacing(previewFacing)
    end)
    preview:Hide()
end

-- The building seen from above, as big as fits, with you next to it for scale.
local function ShowFloorPlan(length, depth)
    local room = { 170, 140 }
    local perYard = math.min(room[1] / math.max(length, 1), room[2] / math.max(depth, 1))
    local width, height = math.max(4, length * perYard), math.max(4, depth * perYard)

    planRect:ClearAllPoints()
    planRect:SetPoint("CENTER", plan, "CENTER", 8, 0)
    planRect:SetWidth(width)
    planRect:SetHeight(height)
    planBorder:ClearAllPoints()
    planBorder:SetPoint("CENTER", planRect, "CENTER")
    planBorder:SetWidth(width + 2)
    planBorder:SetHeight(height + 2)

    -- A person takes about a yard. Below 10 pixels the marker would vanish, so it's drawn
    -- bigger then, and says so.
    local you = perYard
    planYouLabel:SetText("you")
    if you < 10 then
        you = 10
        planYouLabel:SetText("you (drawn bigger)")
    end
    planYou:SetWidth(you)
    planYou:SetHeight(you)
    planYou:ClearAllPoints()
    planYou:SetPoint("TOPRIGHT", planRect, "BOTTOMLEFT", -2, -2)

    previewNote:SetText("Seen from above: the most room it takes")
    plan:Show()
end

local function ShowPreview(piece)
    if not preview or not piece then
        return
    end

    -- { model, length, depth, height }: yards.
    local data = PlayerHousing_Models and PlayerHousing_Models[piece.id]
    previewName:SetText(piece.name)
    previewSize:SetText(data and (Yards(data[2]) .. " by " .. Yards(data[3]) .. ", " .. Yards(data[4]) .. " tall") or "")
    previewNote:SetText("")
    plan:Hide()

    local model = data and data[1]
    if model == "player" then
        previewModel:SetUnit("player")
        previewModel:Show()
    elseif model then
        previewModel:ClearModel()
        previewModel:SetModel(model)
        -- Big and small pieces both fill the frame.
        previewModel:SetModelScale(math.min(1.5, 2.5 / math.max(data[2], data[3], data[4], 0.5)))
        previewModel:SetPosition(0, 0, 0)
        previewModel:Show()
    elseif data then
        previewModel:Hide()
        ShowFloorPlan(data[2], data[3])
    else
        previewModel:Hide()
    end
    preview:Show()
end

local function HidePreview()
    if preview then
        preview:Hide()
    end
end

local function MakeSlot(index)
    local button = CreateFrame("Button", "PlayerHousingSlot" .. index, grid, "SecureActionButtonTemplate")
    button:SetWidth(SLOT_SIZE)
    button:SetHeight(SLOT_SIZE)
    local column = (index - 1) % COLUMNS
    local row = math.floor((index - 1) / COLUMNS)
    button:SetPoint("TOPLEFT", grid, "TOPLEFT", column * (SLOT_SIZE + SLOT_GAP), -row * (SLOT_SIZE + SLOT_GAP))
    button:RegisterForClicks("AnyUp")
    button:RegisterForDrag("LeftButton")

    button.icon = button:CreateTexture(nil, "ARTWORK")
    button.icon:SetAllPoints()
    button.count = button:CreateFontString(nil, "OVERLAY", "NumberFontNormal")
    button.count:SetPoint("BOTTOMRIGHT", -2, 2)
    button:SetPushedTexture("Interface\\Buttons\\UI-Quickslot-Depress")
    button:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square", "ADD")

    button:SetScript("OnEnter", function(self)
        if not self.piece then
            return
        end
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        GameTooltip:SetBagItem(self.piece.bag, self.piece.slot)
        GameTooltip:AddLine("Click it, then click where it should go.", 0.4, 1, 0.4)
        GameTooltip:AddLine("Drag it to an action bar to keep it handy.", 0.7, 0.7, 0.7)
        GameTooltip:Show()
        ShowPreview(self.piece)
    end)
    button:SetScript("OnLeave", function()
        GameTooltip_Hide()
        HidePreview()
    end)
    -- Onto an action bar, like dragging it out of the bag.
    button:SetScript("OnDragStart", function(self)
        if self.piece and not InCombatLockdown() then
            PickupContainerItem(self.piece.bag, self.piece.slot)
        end
    end)
    button:Hide()
    return button
end

local function CreateWindow()
    frame = CreateFrame("Frame", "PlayerHousingFrame", UIParent)
    frame:SetWidth(WIDTH)
    frame:SetHeight(BASE_HEIGHT)
    frame:SetPoint("CENTER", UIParent, "CENTER", 300, 0)
    frame:SetFrameStrata("MEDIUM")
    frame:SetToplevel(true)
    frame:SetClampedToScreen(true)
    frame:SetMovable(true)
    frame:EnableMouse(true)
    frame:EnableMouseWheel(true)
    frame:RegisterForDrag("LeftButton")
    frame:SetBackdrop({
        bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
        edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
        tile = true, tileSize = 32, edgeSize = 32,
        insets = { left = 11, right = 12, top = 12, bottom = 11 },
    })
    frame:SetScript("OnDragStart", function(self)
        if not InCombatLockdown() then
            self:StartMoving()
        end
    end)
    frame:SetScript("OnDragStop", function(self)
        self:StopMovingOrSizing()
        SavePosition()
    end)
    frame:SetScript("OnMouseWheel", OnMouseWheel)
    frame:SetScript("OnShow", function()
        UpdateGrid()
        UpdateButtons()
    end)
    frame:Hide()

    local title = frame:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
    title:SetPoint("TOP", 0, -16)
    title:SetText("Player Housing")

    local close = CreateFrame("Button", "PlayerHousingCloseButton", frame, "UIPanelCloseButton")
    close:SetPoint("TOPRIGHT", -4, -4)
    close:SetScript("OnClick", function()
        if SetShown(false) then
            autoShown = false
        end
    end)

    statusText = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    statusText:SetPoint("TOPLEFT", 14, -40)
    statusText:SetPoint("TOPRIGHT", -14, -40)
    statusText:SetJustifyH("LEFT")

    local row = Row(frame, -56, {
        { "Go home", function() PlayerHousing_Command((state.own or state.islandOwner ~= "") and "leave" or "home") end,
          "Go home or leave", "Takes you to your island, or back to where you came from." },
        { "Decorate", Command("decorate"),
          "Decorate", "While decorating, click any piece to turn it, nudge it or pick it up. Blue runes on tables take small pieces." },
        { "Undo", Command("undo"), function()
            return "Undo", state.undo ~= "" and ("Undo: " .. state.undo) or "Nothing to undo."
        end },
        { "Redo", Command("redo"), function()
            return "Redo", state.redo ~= "" and ("Redo: " .. state.redo) or "Nothing to redo."
        end },
    })
    homeButton, decorateButton, undoButton, redoButton = row[1], row[2], row[3], row[4]

    Row(frame, -80, {
        { "Collection", Command("collection"), "Collection", "Everything you can own, and how to earn the rest. Unlocked pieces give you a copy." },
        { "Storage", Command("storage"), "House Storage", "Pieces that came back while your bags were full." },
        { "Visit", Command("visit"), "Visit an island", "Islands of your party, guild, friends, and the ones you're invited to." },
        { "Menu", Command(""), "Housing menu", "The House Key menu: settings, guests, pack up and more." },
    })

    local filters = { { "all", "All" }, { "furnishings", "Furnishings" }, { "buildings", "Buildings" } }
    for index, spec in ipairs(filters) do
        local button = MakeButton(frame, spec[2], 76, function() SetFilter(spec[1]) end)
        button:SetHeight(20)
        button:SetPoint("TOPLEFT", 12 + (index - 1) * 78, -108)
        filterButtons[spec[1]] = button
    end

    local searchBox = CreateFrame("EditBox", "PlayerHousingSearchBox", frame, "InputBoxTemplate")
    searchBox:SetWidth(WIDTH - 24 - 3 * 78 - 10)
    searchBox:SetHeight(20)
    searchBox:SetPoint("TOPRIGHT", -14, -108)
    searchBox:SetAutoFocus(false)
    searchBox:SetScript("OnEscapePressed", searchBox.ClearFocus)
    searchBox:SetScript("OnEnterPressed", searchBox.ClearFocus)
    searchBox:SetScript("OnTextChanged", function(self)
        search = (self:GetText() or ""):lower()
        page = 1
        UpdateGrid()
    end)
    searchBox:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_TOP")
        GameTooltip:SetText("Search your furnishings by name")
        GameTooltip:Show()
    end)
    searchBox:SetScript("OnLeave", GameTooltip_Hide)

    grid = CreateFrame("Frame", "PlayerHousingGrid", frame)
    grid:SetPoint("TOPLEFT", 14, -136)
    grid:SetWidth(COLUMNS * (SLOT_SIZE + SLOT_GAP))
    grid:SetHeight(ROWS * (SLOT_SIZE + SLOT_GAP))
    for index = 1, PAGE_SIZE do
        slots[index] = MakeSlot(index)
    end
    emptyText = grid:CreateFontString(nil, "OVERLAY", "GameFontDisable")
    emptyText:SetPoint("CENTER")

    prevButton = MakeButton(frame, "<", 28, function()
        page = page - 1
        UpdateGrid()
    end)
    prevButton:SetPoint("TOPLEFT", 12, -300)
    nextButton = MakeButton(frame, ">", 28, function()
        page = page + 1
        UpdateGrid()
    end)
    nextButton:SetPoint("TOPRIGHT", -12, -300)
    pageText = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    pageText:SetPoint("TOP", 0, -305)

    local hint = frame:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    hint:SetPoint("TOPLEFT", 14, -326)
    hint:SetPoint("TOPRIGHT", -14, -326)
    hint:SetText("Click a furnishing, then click where it goes. Undo gives it back.")

    selectedPanel = CreateFrame("Frame", "PlayerHousingSelected", frame)
    selectedPanel:SetPoint("TOPLEFT", 0, -BASE_HEIGHT + 12)
    selectedPanel:SetWidth(WIDTH)
    selectedPanel:SetHeight(SELECTED_HEIGHT)
    selectedText = selectedPanel:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    selectedText:SetPoint("TOPLEFT", 14, 0)
    selectedText:SetPoint("TOPRIGHT", -14, 0)
    selectedText:SetJustifyH("LEFT")

    Row(selectedPanel, -18, {
        { "Turn left", TurnButton(1), "Turn left", "15 degrees. Shift: 5. Ctrl: 90. The mouse wheel over this window turns it too." },
        { "Turn right", TurnButton(-1), "Turn right", "15 degrees. Shift: 5. Ctrl: 90." },
        { "Face me", Command("face"), "Face me", "Turns it to face you." },
        { "Here", Command("here"), "Move here", "Moves it to where you're standing." },
        { "Move", Command("move"), "Move with the targeting circle",
          "A button appears below: click it, then click the new spot. What's on it moves too." },
        { "Pick up", function() PickUp(false) end, "Pick up", "Back to your bags. Undo puts it back." },
    })

    Row(selectedPanel, -42, {
        { "Fwd", Command("nudge forward"), "Nudge forward", "A quarter yard, the way you're facing." },
        { "Back", Command("nudge back"), "Nudge back", "A quarter yard toward you." },
        { "Left", Command("nudge left"), "Nudge left", "A quarter yard to your left." },
        { "Right", Command("nudge right"), "Nudge right", "A quarter yard to your right." },
        { "Up", Command("up"), "Raise", "A tenth of a yard. Ctrl and the mouse wheel does it too." },
        { "Down", Command("down"), "Lower", "A tenth of a yard." },
    })

    Row(selectedPanel, -66, {
        { "Bigger", function() PlayerHousing_Command(IsShiftKeyDown() and "size normal" or "size bigger") end,
          "Bigger", "A tenth bigger, up to the server's limit. What stands on it keeps its place. Shift: normal size." },
        { "Smaller", function() PlayerHousing_Command(IsShiftKeyDown() and "size normal" or "size smaller") end,
          "Smaller", "A tenth smaller. Shift: normal size." },
        { "Tilt fwd", function() PlayerHousing_Command(IsShiftKeyDown() and "tilt straight" or "tilt forward") end,
          "Tilt forward", "5 degrees, its front down. Shift: stand it straight." },
        { "Tilt back", function() PlayerHousing_Command(IsShiftKeyDown() and "tilt straight" or "tilt back") end,
          "Tilt back", "5 degrees. Shift: stand it straight." },
        { "Tilt L", function() PlayerHousing_Command(IsShiftKeyDown() and "tilt straight" or "tilt left") end,
          "Tilt to its left", "5 degrees, toward its own left. Shift: stand it straight." },
        { "Tilt R", function() PlayerHousing_Command(IsShiftKeyDown() and "tilt straight" or "tilt right") end,
          "Tilt to its right", "5 degrees. Shift: stand it straight." },
    })

    local anotherButton = MakeButton(selectedPanel, "Another", 70, Command("another"),
        "Place another like this", "A button appears next to this one: click it, then click the spot. The new one gets this one's turn, size and tilt.")
    anotherButton:SetPoint("TOPLEFT", 12, -90)

    pickUpAllButton = MakeButton(selectedPanel, "Pick up all", 90, function() PickUp(true) end,
        "Pick up with contents", "The building and everything inside it go back to your bags. Undo puts it all back.")
    pickUpAllButton:SetPoint("TOPRIGHT", -12, -90)
    pickUpAllButton:Hide()

    local help = selectedPanel:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    help:SetPoint("TOPLEFT", 14, -118)
    help:SetPoint("TOPRIGHT", -14, -118)
    help:SetJustifyH("LEFT")
    help:SetText("Mouse wheel: turn. Shift: finer. Ctrl: up and down.")

    spotButton = CreateFrame("Button", "PlayerHousingSpotButton", selectedPanel, "SecureActionButtonTemplate,UIPanelButtonTemplate")
    spotButton:SetWidth(150)
    spotButton:SetHeight(22)
    spotButton:SetPoint("TOPLEFT", 86, -90)
    spotButton:SetText("Now pick the spot")
    spotButton:RegisterForClicks("AnyUp")
    spotButton.tooltipTitle = "Pick the new spot"
    spotButton.tooltipText = "The targeting circle is as big as the piece. Right-click or Escape cancels the circle; the menu can start over."
    spotButton:SetScript("OnEnter", ShowButtonTooltip)
    spotButton:SetScript("OnLeave", GameTooltip_Hide)
    spotButton:Hide()
    selectedPanel:Hide()

    SetFilter("all")
    CreatePreview()
    frame:HookScript("OnHide", HidePreview)
end

function PlayerHousing_Toggle()
    if not frame then
        return
    end
    if InCombatLockdown() then
        Print("the housing window can't open or close in combat.")
        return
    end
    autoShown = false
    SetShown(not frame:IsShown())
end

---------------------------------------------------------------------------------------------
-- Events

local driver = CreateFrame("Frame")

driver:SetScript("OnUpdate", function(self, elapsed)
    sinceSend = sinceSend + elapsed
    if sinceSend < SEND_INTERVAL then
        return
    end
    -- Mouse wheel turns add up between commands, so a fast spin is one command and one undo.
    if pendingTurn % 360 ~= 0 then
        PlayerHousing_Command(("rotate %d"):format(pendingTurn))
        sinceSend = 0
    elseif pendingLift ~= 0 and math.abs(pendingLift) >= 0.05 then
        PlayerHousing_Command(("nudge %s %.2f"):format(pendingLift > 0 and "up" or "down", math.abs(pendingLift)))
        sinceSend = 0
        pendingLift = 0
    end
    pendingTurn = 0
    if bagsDirty and frame and frame:IsShown() then
        UpdateGrid()
    end
end)

local function RequestState()
    if known then
        PlayerHousing_Command("state")
    end
end

driver:SetScript("OnEvent", function(self, event, ...)
    if event == "ADDON_LOADED" then
        if ... ~= "PlayerHousing" then
            return
        end
        PlayerHousingDB = PlayerHousingDB or {}
        db = PlayerHousingDB
        if db.autoShow == nil then
            db.autoShow = true
        end
        known = known or db.known == true
        CreateWindow()
        if db.point then
            frame:ClearAllPoints()
            frame:SetPoint(db.point[1], UIParent, db.point[2], db.point[3], db.point[4])
        end
        UpdateButtons()
    elseif event == "PLAYER_ENTERING_WORLD" or event == "ZONE_CHANGED_NEW_AREA" then
        RequestState()
    elseif event == "BAG_UPDATE" then
        bagsDirty = true
    elseif event == "PLAYER_REGEN_ENABLED" then
        if layoutPending then
            UpdateGrid()
            UpdateButtons()
        end
    elseif event == "CHAT_MSG_ADDON" then
        local prefix, message, channel, sender = ...
        -- Only the server's whisper to us counts, never another player's.
        if prefix ~= PREFIX or channel ~= "WHISPER" or sender ~= UnitName("player") then
            return
        end
        local fields = {}
        for field in (message .. "\t"):gmatch("([^\t]*)\t") do
            fields[#fields + 1] = field
        end
        if fields[1] == "state" then
            OnState(fields)
        end
    end
end)

for _, event in ipairs({ "ADDON_LOADED", "PLAYER_ENTERING_WORLD", "ZONE_CHANGED_NEW_AREA", "BAG_UPDATE",
                         "PLAYER_REGEN_ENABLED", "CHAT_MSG_ADDON" }) do
    driver:RegisterEvent(event)
end

SLASH_PLAYERHOUSING1 = "/housing"
SlashCmdList["PLAYERHOUSING"] = function(message)
    message = strtrim(message or "")
    if message == "" then
        PlayerHousing_Toggle()
    elseif message == "auto" then
        db.autoShow = not db.autoShow
        Print(db.autoShow and "the window opens by itself when you arrive home." or "the window only opens with /housing.")
    else
        -- Anything else is a .house command: /housing undo, /housing rotate 90, ...
        PlayerHousing_Command(message)
    end
end
