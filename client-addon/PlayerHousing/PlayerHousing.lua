-- Player Housing: an optional window for mod-playerhousing.
--
-- Every button sends one of the .house chat commands a player could type, so nothing here
-- is needed to play: the House Key menus do all of it. What the addon adds is the whole
-- Collection as icons (the ones in your bags placed with a click: then click where it goes),
-- undo and redo one click away with the history on a right-click, and quick controls for the
-- pieces you're working on, including turning them with the mouse wheel.
--
-- The server whispers the island state to the player as an addon message with the prefix
-- HOUSING (PlayerHousingMgr::SendAddonState): tab separated, new fields only at the end.

local PREFIX = "HOUSING"

local COLUMNS, SLOT_SIZE, SLOT_GAP = 9, 36, 4
local WIDTH = 24 + COLUMNS * (SLOT_SIZE + SLOT_GAP)
local BASE_HEIGHT, SELECTED_HEIGHT = 400, 168
local CONTENT_TOP, CONTENT_HEIGHT = -130, 258  -- where the tabs' panels go
local SEND_INTERVAL = 0.15  -- seconds between moves sent; key presses and wheel turns in between add up

BINDING_HEADER_PLAYERHOUSING = "Player Housing"
BINDING_NAME_PLAYERHOUSING_TOGGLE = "Show or hide the housing window"
BINDING_NAME_PLAYERHOUSING_UNDO = "Undo"
BINDING_NAME_PLAYERHOUSING_REDO = "Redo"
BINDING_NAME_PLAYERHOUSING_DECORATE = "Start or stop decorating"
BINDING_NAME_PLAYERHOUSING_EDIT = "Start or stop edit mode (move pieces with the keys)"
BINDING_NAME_PLAYERHOUSING_TURN_LEFT = "Turn the selected piece left"
BINDING_NAME_PLAYERHOUSING_TURN_RIGHT = "Turn the selected piece right"
BINDING_NAME_PLAYERHOUSING_SELECT_NEAREST = "Select the nearest piece"

local state = {
    own = false, decorating = false,
    selected = 0, selectedName = "", selectedBuilding = false,
    furnishings = 0, maxFurnishings = 0, buildings = 0, maxBuildings = 0,
    undo = "", redo = "", islandOwner = "",
    pendingMover = 0,       -- Move a Piece waiting for its circle (setting down a saved set)
    roommate = false,       -- decorating someone else's island, with their leave
    editMode = false,       -- keys move the selected piece (EditMode.lua)
    grid = 0,               -- yards; 0 is off
    groupSize = 0,          -- pieces selected together (Ctrl-click); 1 or 0: just the one
    ghostItem = 0,          -- the piece following you until you set it down (0: none)
    ghostMove = false,      -- moving pieces already placed (else placing a new one)
    ghosts = false,         -- see-through ghosts (else pieces are carried as they are)
}

-- Changing things: on your own island, or as a roommate on someone else's.
local function CanEdit()
    return state.own or state.roommate
end

local db                    -- PlayerHousingDB, once loaded
local known = false         -- the server has housing: it sent us a state
local pieces = {}           -- housing items in the bags, one entry per kind of item
local movers = {}           -- Move a Piece items in the bags: item id to "bag slot"
local bagsDirty, layoutPending = true, false
local autoShown = false
-- Moves waiting to go: yards forward, left and up (the player's way), and degrees of turn.
local pending = { forward = 0, left = 0, up = 0, turn = 0 }
local sinceSend = 0
local stateHooks, windowHooks, dataHooks, bagHooks, combatEndHooks, messageHooks = {}, {}, {}, {}, {}, {}
local lists = {}            -- lists from the server for the window's tabs, by kind
local registered = false    -- told the server this session that the addon is here

local frame, statusText, selectedPanel, selectedText
local preview, previewModel, previewName, previewSize, previewNote
local plan, planRect, planBorder, planYou, planYouLabel, picture
local UpdateDetails         -- the pinned piece's details, next to the window (defined there)
local homeButton, decorateButton, undoButton, redoButton
local pickUpAllButton, spotButton
local groupButtons = {}     -- the selected panel's buttons for several pieces

local function Print(text)
    DEFAULT_CHAT_FRAME:AddMessage("|cffffd000Housing:|r " .. text)
end

-- Move a Piece: one item per size of targeting circle, handed out by .house move.
local function IsMoverItem(id)
    return id >= 901190 and id <= 901199
end

-- Furnishings and buildings from the module's content generator (902000 is the House Key),
-- and the catalog of every object (940000 and up).
local function IsHousingItem(id)
    return ((id >= 901100 and id <= 901199) or (id >= 902001 and id <= 902999) or (id >= 940000 and id <= 944999))
        and not IsMoverItem(id)
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

local function PieceLocation(id)
    for _, piece in ipairs(pieces) do
        if piece.id == id then
            return piece.bag .. " " .. piece.slot
        end
    end
end

-- While a saved set waits for its spot, a button uses its Move a Piece, so there's no need
-- to find it in the bags. It's a secure button too.
local function UpdateSpotButton()
    local location
    if CanEdit() and state.pendingMover > 0 then
        location = movers[state.pendingMover]
        spotButton:SetText("Now pick the spot")
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

-- Where each piece is in the bags, for the buttons that use them: secure ones, which only
-- change out of combat. The Collection (Window.lua) follows through OnBags.
local function UpdateBags()
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
        for _, hook in ipairs(bagHooks) do
            hook()
        end
    end
    UpdateSpotButton()
    UpdateDetails()
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
    decorateButton:SetText(state.editMode and "Done" or "Edit")
    if CanEdit() then decorateButton:Enable() else decorateButton:Disable() end
    if CanEdit() and state.undo ~= "" then undoButton:Enable() else undoButton:Disable() end
    if CanEdit() and state.redo ~= "" then redoButton:Enable() else redoButton:Disable() end

    local showSelected = CanEdit() and state.selected > 0
    if showSelected then
        if state.groupSize > 1 then
            selectedText:SetText(("Selected: |cffffffff%s|r and %d more"):format(state.selectedName, state.groupSize - 1))
        else
            selectedText:SetText(("Selected: |cffffffff%s|r  |cffa0a0a0Ctrl-click others to add them|r"):format(state.selectedName))
        end
        if state.selectedBuilding and state.groupSize <= 1 then
            pickUpAllButton:Show()
        else
            pickUpAllButton:Hide()
        end
        for _, button in ipairs(groupButtons) do
            if state.groupSize >= (button.needs or 1) then button:Enable() else button:Disable() end
        end
        selectedPanel:Show()
    else
        selectedPanel:Hide()
    end

    -- Resizing a window that holds secure buttons also waits for the end of combat.
    if not InCombatLockdown() then
        frame:SetHeight(BASE_HEIGHT + (showSelected and SELECTED_HEIGHT or 0))
        UpdateBags()
    else
        layoutPending = true
    end
    UpdateDetails()
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
    state.roommate = fields[16] == "1"
    state.editMode = fields[17] == "1"
    state.grid = tonumber(fields[18] or "") or 0
    state.groupSize = tonumber(fields[20] or "") or 0
    state.ghostItem = tonumber(fields[21] or "") or 0
    state.ghostMove = fields[22] == "move"
    state.ghosts = fields[23] == "1"
    -- Once a session: the server learns the addon is here, and whether the House Key should
    -- open this window instead of the menu.
    if not registered and db then
        registered = true
        PlayerHousing_Command("addon 1 " .. (db.keyWindow == false and "0" or "1"))
    end
    for _, hook in ipairs(stateHooks) do
        hook(state)
    end

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

local function Shift(forward, left, up, turn)
    pending.forward = pending.forward + (forward or 0)
    pending.left = pending.left + (left or 0)
    pending.up = pending.up + (up or 0)
    pending.turn = pending.turn + (turn or 0)
end

local function Turn(degrees)
    Shift(0, 0, 0, degrees)
end

-- The turn buttons: 15 degrees, Shift for 5, Ctrl for 90.
local function TurnButton(direction)
    return function()
        local step = IsShiftKeyDown() and 5 or (IsControlKeyDown() and 90 or 15)
        Turn(direction * step)
    end
end

local function Lift(yards)
    Shift(0, 0, yards, 0)
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

StaticPopupDialogs["PLAYERHOUSING_PICKUP_GROUP"] = {
    text = "Pick up the %s selected pieces?\n\nThey go back to your bags, with what stands on them. Undo puts them back.",
    button1 = "Pick up",
    button2 = CANCEL,
    OnAccept = function() PlayerHousing_Command("pickup") end,
    timeout = 0, whileDead = 1, hideOnEscape = 1,
}

local function PickUp(withInside)
    if state.selected == 0 then
        return
    end
    -- Several selected: all of them, after asking.
    if state.groupSize > 1 then
        StaticPopup_Show("PLAYERHOUSING_PICKUP_GROUP", state.groupSize)
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
-- Preview: the piece's model and size, next to the window, while hovering its icon. A piece
-- clicked in the Collection stays here (pinned) with what you can do with it; hovering others
-- shows them for a moment. Drag the model to turn it, the mouse wheel zooms, right-drag moves
-- it up and down. PlayerHousing_Models (PieceModels.lua) comes from the content builder.

local PREVIEW_HEIGHT, PINNED_HEIGHT = 268, 360
local view = { facing = 0, zoom = 1, lift = 0, spin = true }
local shownPiece, pinned        -- { id = item, name = text }
local dragging, dragX, dragY
local detailsText, detailsCounts, detailsHint, placeButton, getOneButton, getFiveButton, takeButton

local function Yards(value)
    if value < 1 then
        return "under 1 yd"
    end
    return ("%d yd"):format(math.floor(value + 0.5))
end

-- Model frames draw the model from its base; this turns it about its middle and fits its
-- longest side to the frame. The builder measured both from the model's bounds.
local function FrameModel()
    local data = shownPiece and PlayerHousing_Models and PlayerHousing_Models[shownPiece.id]
    local model = data and data[1]
    if type(model) ~= "string" then
        return
    end
    previewModel:SetFacing(view.facing)
    if model == "player" or model:find("^creature:") then
        -- These fit themselves; the wheel and right-drag move them nearer and up or down.
        previewModel:SetPosition(math.max(-1, math.min(1.5, (view.zoom - 1) * 0.8)), 0, view.lift)
        return
    end
    local fit, midX, midY, midZ = data[5] or 0, data[6] or 0, data[7] or 0, data[8] or 0
    local scale
    if fit > 0 then
        scale = math.min(3, 2.2 / fit) * view.zoom
    else
        scale = math.min(1.5, 2.5 / math.max(data[2], data[3], data[4], 0.5)) * view.zoom
    end
    -- db.framing "model": offsets in the model's own units (for clients that scale them).
    local offset = (db and db.framing == "model") and 1 or scale
    local c, s = math.cos(view.facing), math.sin(view.facing)
    previewModel:SetModelScale(scale)
    previewModel:SetPosition(-(midX * c - midY * s) * offset, -(midX * s + midY * c) * offset, -midZ * offset + view.lift)
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

function UpdateDetails()
    if not preview then
        return
    end
    if not pinned then
        placeButton:Hide()
        return
    end
    local piece = pinned
    local info = PlayerHousingAPI.DescribePiece and PlayerHousingAPI.DescribePiece(piece.id) or {}
    local status
    if info.unlocked then
        status = "|cff40ff40Unlocked.|r " .. (info.cost and ("A copy is " .. info.cost .. ".") or "")
    elseif info.unlocked == false then
        status = "|cffff8040Locked|r" .. ((info.hint and info.hint ~= "") and (": " .. info.hint) or ".")
    else
        status = "Open the Collection tab for how to unlock it."
    end
    detailsText:SetText((info.category and (info.category .. (info.building and ", building" or "") .. "\n") or "") .. status)
    detailsCounts:SetText(("Bags: %d   Storage: %d   Placed: %d"):format(GetItemCount(piece.id), info.storage or 0, info.placed or 0))
    if info.unlocked then getOneButton:Enable() getFiveButton:Enable() else getOneButton:Disable() getFiveButton:Disable() end
    if (info.storage or 0) > 0 then takeButton:Enable() else takeButton:Disable() end
    -- Place: a ghost of it follows you (one from the bags or House Storage, else a new copy).
    if CanEdit() and (GetItemCount(piece.id) > 0 or (info.storage or 0) > 0 or info.unlocked) then
        placeButton:Enable()
    else
        placeButton:Disable()
    end
end

local function SetPreviewHeight()
    preview:SetHeight(pinned and PINNED_HEIGHT or PREVIEW_HEIGHT)
end

local function ShowPiece(piece)
    if not shownPiece or shownPiece.id ~= piece.id then
        view.facing, view.zoom, view.lift, view.spin = 0, 1, 0, true
        dragging = nil
    end
    shownPiece = piece

    -- { model, length, depth, height, then the model's longest side and middle }: yards.
    local data = PlayerHousing_Models and PlayerHousing_Models[piece.id]
    previewName:SetText(piece.name)
    previewSize:SetText(data and (Yards(data[2]) .. " by " .. Yards(data[3]) .. ", " .. Yards(data[4]) .. " tall") or "")
    previewNote:SetText("")
    plan:Hide()
    picture:Hide()

    local model = data and data[1]
    local creature = type(model) == "string" and tonumber(model:match("^creature:(%d+)$"))
    if model == "player" then
        previewModel:SetUnit("player")
        previewModel:Show()
    elseif creature then
        -- Figurines: the creature's model (drawn once the client has seen that creature).
        previewModel:ClearModel()
        previewModel:SetCreature(creature)
        previewModel:SetModelScale(1)
        previewModel:Show()
    elseif model then
        previewModel:ClearModel()
        previewModel:SetModel(model)
        previewModel:Show()
    elseif data and PlayerHousing_Pictures and PlayerHousing_Pictures[piece.id] then
        -- A building with a picture (Pictures.lua).
        previewModel:Hide()
        picture:SetTexture("Interface\\AddOns\\PlayerHousing\\Pictures\\" .. piece.id)
        picture:Show()
    elseif data then
        previewModel:Hide()
        ShowFloorPlan(data[2], data[3])
    else
        previewModel:Hide()
    end
    FrameModel()

    local isPinned = pinned and pinned.id == piece.id
    if isPinned then
        UpdateDetails()
    end
    for _, region in ipairs({ detailsText, detailsCounts, getOneButton, getFiveButton, takeButton }) do
        if isPinned then region:Show() else region:Hide() end
    end
    if isPinned then placeButton:Show() else placeButton:Hide() end
    detailsHint:SetText(pinned and not isPinned and "Click it to show it here instead." or "")
    SetPreviewHeight()
    preview:Show()
end

-- Hovering: shows the piece until the mouse moves on.
local function ShowPreview(piece)
    if preview and piece then
        ShowPiece(piece)
    end
end

local function HidePreview()
    if not preview then
        return
    end
    if pinned then
        ShowPiece(pinned)
    else
        preview:Hide()
    end
end

local function Pin(piece)
    if not preview or not piece then
        return
    end
    pinned = piece
    ShowPiece(piece)
end

local function Unpin()
    pinned = nil
    dragging = nil
    if not preview then
        return
    end
    preview:Hide()
    placeButton:Hide()
end

local function DetailsButton(name, text, width, onClick, tooltipTitle, tooltipText)
    local button = CreateFrame("Button", name, preview, "UIPanelButtonTemplate")
    button:SetWidth(width)
    button:SetHeight(22)
    button:SetText(text)
    button:SetScript("OnClick", onClick)
    button.tooltipTitle, button.tooltipText = tooltipTitle, tooltipText
    button:SetScript("OnEnter", ShowButtonTooltip)
    button:SetScript("OnLeave", GameTooltip_Hide)
    return button
end

-- A ghost of the piece follows you until you set it down (G), from the bags or House
-- Storage, or a new copy from the Collection.
local function StartGhost(id)
    if not CanEdit() then
        Print("pieces go on your own island (or one you're a roommate on). House Key, Go home.")
        return
    end
    PlayerHousing_Command("ghost " .. id)
end

local function GetCopies(count)
    return function()
        if pinned then
            PlayerHousing_Command(("get %d %d"):format(pinned.id, count))
            PlayerHousing_Command("data collection")
        end
    end
end

local function CreatePreview()
    preview = CreateFrame("Frame", "PlayerHousingPreview", UIParent)
    preview:SetWidth(220)
    preview:SetHeight(PREVIEW_HEIGHT)
    preview:SetPoint("TOPRIGHT", frame, "TOPLEFT", -4, 0)
    preview:SetFrameStrata("DIALOG")
    preview:EnableMouse(true)
    preview:SetBackdrop({
        bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        tile = true, tileSize = 32, edgeSize = 16,
        insets = { left = 4, right = 4, top = 4, bottom = 4 },
    })

    previewName = preview:CreateFontString("PlayerHousingPreviewName", "OVERLAY", "GameFontNormal")
    previewName:SetPoint("TOPLEFT", 10, -10)
    previewName:SetPoint("TOPRIGHT", -28, -10)
    previewName:SetJustifyH("LEFT")

    local close = CreateFrame("Button", "PlayerHousingPreviewClose", preview, "UIPanelCloseButton")
    close:SetPoint("TOPRIGHT", 2, 2)
    close:SetScript("OnClick", Unpin)

    previewModel = CreateFrame("PlayerModel", "PlayerHousingPreviewModel", preview)
    previewModel:SetPoint("TOPLEFT", 10, -28)
    previewModel:SetWidth(200)
    previewModel:SetHeight(190)
    previewModel:EnableMouse(true)
    previewModel:EnableMouseWheel(true)
    previewModel:SetScript("OnMouseDown", function(self, button)
        dragging = button
        dragX, dragY = GetCursorPosition()
        view.spin = false
    end)
    previewModel:SetScript("OnMouseUp", function()
        dragging = nil
    end)
    previewModel:SetScript("OnMouseWheel", function(self, delta)
        view.zoom = math.min(4, math.max(0.3, view.zoom * (delta > 0 and 1.2 or 1 / 1.2)))
        FrameModel()
    end)

    previewNote = preview:CreateFontString("PlayerHousingPreviewNote", "OVERLAY", "GameFontDisableSmall")
    previewNote:SetPoint("TOP", previewModel, "TOP", 0, -2)
    previewNote:SetWidth(180)

    -- A building's picture (Pictures.lua: rendered from its model, or a GM's photo tour) or
    -- else its floor plan: what world model buildings get instead of a model. The square
    -- picture fills the 200 by 190 area, a sliver off its top and bottom.
    picture = preview:CreateTexture("PlayerHousingPreviewPicture", "ARTWORK")
    picture:SetAllPoints(previewModel)
    picture:SetTexCoord(0, 1, 0.025, 0.975)
    picture:Hide()
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

    previewSize = preview:CreateFontString("PlayerHousingPreviewSize", "OVERLAY", "GameFontHighlightSmall")
    previewSize:SetPoint("TOPLEFT", 10, -226)
    previewSize:SetPoint("TOPRIGHT", -10, -226)

    detailsHint = preview:CreateFontString("PlayerHousingPreviewHint", "OVERLAY", "GameFontDisableSmall")
    detailsHint:SetPoint("TOPLEFT", 10, -246)
    detailsHint:SetPoint("TOPRIGHT", -10, -246)

    -- The pinned piece's details and buttons.
    detailsText = preview:CreateFontString("PlayerHousingDetailsText", "OVERLAY", "GameFontHighlightSmall")
    detailsText:SetPoint("TOPLEFT", 10, -246)
    detailsText:SetPoint("TOPRIGHT", -10, -246)
    detailsText:SetJustifyH("LEFT")
    detailsText:SetHeight(36)
    detailsText:SetJustifyV("TOP")
    detailsCounts = preview:CreateFontString("PlayerHousingDetailsCounts", "OVERLAY", "GameFontNormalSmall")
    detailsCounts:SetPoint("TOPLEFT", 10, -284)
    detailsCounts:SetPoint("TOPRIGHT", -10, -284)
    detailsCounts:SetJustifyH("LEFT")

    placeButton = CreateFrame("Button", "PlayerHousingDetailsPlace", frame, "UIPanelButtonTemplate")
    placeButton:SetWidth(76)
    placeButton:SetHeight(22)
    placeButton:SetPoint("TOPLEFT", frame, "TOPLEFT", -214, -302)
    placeButton:SetFrameStrata("DIALOG")
    placeButton:SetFrameLevel(preview:GetFrameLevel() + 10)
    placeButton:SetText("Place")
    placeButton:RegisterForClicks("AnyUp")
    placeButton.tooltipTitle = "Place one"
    placeButton.tooltipText = "It follows you: walk it where it goes (arrows: farther, nearer, sideways; wheel: turn), then G sets it down. From your bags or House Storage, or a new copy. On your island (or as a roommate) only."
    placeButton:SetScript("OnEnter", ShowButtonTooltip)
    placeButton:SetScript("OnLeave", GameTooltip_Hide)
    placeButton:SetScript("OnClick", function()
        if pinned then
            StartGhost(pinned.id)
        end
    end)
    getOneButton = DetailsButton("PlayerHousingDetailsGetOne", "Get 1", 60, GetCopies(1), "Get a copy", "Into your bags.")
    getOneButton:SetPoint("TOPLEFT", 88, -302)
    getFiveButton = DetailsButton("PlayerHousingDetailsGetFive", "Get 5", 60, GetCopies(5), "Get five copies", "Into your bags.")
    getFiveButton:SetPoint("LEFT", getOneButton, "RIGHT", 2, 0)
    takeButton = DetailsButton("PlayerHousingDetailsTake", "Take from storage", 200, function()
        if pinned then
            PlayerHousing_Command("take " .. pinned.id)
            PlayerHousing_Command("data collection")
        end
    end, "Take from House Storage", "The ones that came back while your bags were full.")
    takeButton:SetPoint("TOPLEFT", 10, -326)
    placeButton:Hide()

    preview:SetScript("OnUpdate", function(self, elapsed)
        if dragging then
            local x, y = GetCursorPosition()
            if dragging == "LeftButton" then
                view.facing = (view.facing + (x - dragX) * 0.015) % (2 * math.pi)
            else
                view.lift = math.max(-3, math.min(3, view.lift + (y - dragY) * 0.01))
            end
            dragX, dragY = x, y
            FrameModel()
        elseif view.spin then
            view.facing = (view.facing + elapsed * 0.6) % (2 * math.pi)
            FrameModel()
        end
    end)
    preview:Hide()
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
        UpdateBags()
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
        { "Edit", Command("edit"),
          "Edit mode", "Click a piece (or press Tab) and move it with the keys: arrows slide it, the mouse wheel turns it, Page Up and Page Down raise and lower it. Escape when you're done." },
        { "Undo", function(self, mouseButton)
            if mouseButton == "RightButton" and PlayerHousingAPI.ShowHistory then
                PlayerHousingAPI.ShowHistory(self)
            else
                PlayerHousing_Command("undo")
            end
        end, function()
            return "Undo", (state.undo ~= "" and ("Undo: " .. state.undo) or "Nothing to undo.") .. "\nRight-click: the last changes, to undo back to any of them."
        end },
        { "Redo", Command("redo"), function()
            return "Redo", state.redo ~= "" and ("Redo: " .. state.redo) or "Nothing to redo."
        end },
        { "Menu", Command(""), "Housing menu", "The House Key menu: everything the window has, and pack up, help and more." },
    })
    homeButton, decorateButton, undoButton, redoButton = row[1], row[2], row[3], row[4]
    undoButton:RegisterForClicks("LeftButtonUp", "RightButtonUp")

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
        { "Move", Command("ghost move"), "Move it",
          "It follows you: walk it to its new spot, then G sets it down (Escape: never mind). What's on it moves too." },
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

    -- Several pieces at once (Ctrl-click them), and rows and sets.
    groupButtons = Row(selectedPanel, -90, {
        { "Row...", function() if PlayerHousingAPI.ShowRowDialog then PlayerHousingAPI.ShowRowDialog() end end,
          "A row of copies", "Copies of the selected piece in a straight row: how many, how far apart, and which way." },
        { "Height", Command("match height"), "Same height", "The other selected pieces go to the first one's height." },
        { "Turn", Command("match turn"), "Same turn", "The other selected pieces turn the way the first one faces." },
        { "Line up", Command("match line"), "Line up", "The selected pieces line up in a straight row across your view, through the first one." },
        { "Space", Command("match space"), "Space evenly", "The selected pieces spread out evenly between the two at the ends (three or more)." },
        { "Save set", function() StaticPopup_Show("PLAYERHOUSING_SAVE_SET") end, "Save as a set",
          "The selected pieces, and what stands on them, saved together to set down anywhere (Layouts tab, Sets)." },
    })
    groupButtons[1].needs = 1
    groupButtons[2].needs, groupButtons[3].needs, groupButtons[4].needs = 2, 2, 2
    groupButtons[5].needs = 3
    groupButtons[6].needs = 1

    local anotherButton = MakeButton(selectedPanel, "Another", 70, Command("another"),
        "Place another like this", "A button appears next to this one: click it, then click the spot. The new one gets this one's turn, size and tilt.")
    anotherButton:SetPoint("TOPLEFT", 12, -114)

    pickUpAllButton = MakeButton(selectedPanel, "Pick up all", 90, function() PickUp(true) end,
        "Pick up with contents", "The building and everything inside it go back to your bags. Undo puts it all back.")
    pickUpAllButton:SetPoint("TOPRIGHT", -12, -114)
    pickUpAllButton:Hide()

    local help = selectedPanel:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    help:SetPoint("TOPLEFT", 14, -142)
    help:SetPoint("TOPRIGHT", -14, -142)
    help:SetJustifyH("LEFT")
    help:SetText("Mouse wheel: turn. Shift: finer. Ctrl: up and down. Ctrl-click pieces: several at once.")

    spotButton = CreateFrame("Button", "PlayerHousingSpotButton", selectedPanel, "SecureActionButtonTemplate,UIPanelButtonTemplate")
    spotButton:SetWidth(150)
    spotButton:SetHeight(22)
    spotButton:SetPoint("TOPLEFT", 86, -114)
    spotButton:SetText("Now pick the spot")
    spotButton:RegisterForClicks("AnyUp")
    spotButton.tooltipTitle = "Pick the spot"
    spotButton.tooltipText = "The targeting circle is as big as the set. Right-click or Escape cancels the circle."
    spotButton:SetScript("OnEnter", ShowButtonTooltip)
    spotButton:SetScript("OnLeave", GameTooltip_Hide)
    spotButton:Hide()
    selectedPanel:Hide()

    CreatePreview()
    frame:HookScript("OnHide", Unpin)
    for _, hook in ipairs(windowHooks) do
        hook(frame)
    end
end

-- The House Key, with the addon: open the window.
function PlayerHousing_Open()
    if not frame then
        return
    end
    if InCombatLockdown() then
        Print("the housing window can't open in combat.")
        return
    end
    autoShown = false
    SetShown(true)
end

-- A piece by its number (the Placed tab): buildings ask first, like the selected one.
local function PickUpPlacement(id, name, building)
    if not building then
        PlayerHousing_Command("pickup " .. id)
        return
    end
    local dialog = StaticPopup_Show("PLAYERHOUSING_PICKUP_BUILDING", name)
    if dialog then
        dialog.data = id
    end
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
    -- Key presses and wheel turns add up between commands; the server makes a quick run of
    -- them one undo step.
    local turn = pending.turn % 360 == 0 and 0 or pending.turn
    if math.abs(pending.forward) >= 0.005 or math.abs(pending.left) >= 0.005 or math.abs(pending.up) >= 0.005 or turn ~= 0 then
        -- With a piece following you, the same steps move the ghost.
        PlayerHousing_Command(("%s %.2f %.2f %.2f %d"):format(state.ghostItem > 0 and "ghost adjust" or "shift",
            pending.forward, pending.left, pending.up, turn))
        sinceSend = 0
    end
    pending.forward, pending.left, pending.up, pending.turn = 0, 0, 0, 0
    -- Even with the window shut: edit mode's G uses the spot button.
    if bagsDirty and frame then
        UpdateBags()
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
            UpdateBags()
            UpdateButtons()
        end
        for _, hook in ipairs(combatEndHooks) do
            hook()
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
        local kind = fields[1]
        if kind == "state" then
            OnState(fields)
        elseif kind == "open" then
            PlayerHousing_Open()
        elseif kind == "begin" then
            -- A list for the window: begin, a row a message, end.
            lists[fields[2]] = { rows = {}, arg = fields[3] }
        elseif kind == "row" then
            local list = lists[fields[2]]
            if list and not list.done then
                local row = {}
                for index = 3, #fields do
                    row[#row + 1] = fields[index]
                end
                list.rows[#list.rows + 1] = row
            end
        elseif kind == "end" then
            local list = lists[fields[2]]
            if list and not list.done then
                list.done = true
                list.total = tonumber(fields[3] or "")
                for _, hook in ipairs(dataHooks[fields[2]] or {}) do
                    hook(list)
                end
            end
        else
            -- Other messages (the photo tour's) for whoever listens.
            for _, hook in ipairs(messageHooks[kind] or {}) do
                hook(fields)
            end
        end
    end
end)

for _, event in ipairs({ "ADDON_LOADED", "PLAYER_ENTERING_WORLD", "ZONE_CHANGED_NEW_AREA", "BAG_UPDATE",
                         "PLAYER_REGEN_ENABLED", "CHAT_MSG_ADDON" }) do
    driver:RegisterEvent(event)
end

-- For EditMode.lua and Window.lua.
PlayerHousingAPI = {
    state = state,
    Command = PlayerHousing_Command,
    CanEdit = CanEdit,
    Shift = Shift,
    PickUp = PickUp,
    PickUpPlacement = PickUpPlacement,
    Print = Print,
    MakeButton = MakeButton,
    ShowPreview = ShowPreview,
    HidePreview = HidePreview,
    -- The Collection's click: the piece stays next to the window with its buttons.
    Pin = Pin,
    Unpin = Unpin,
    StartGhost = StartGhost,
    -- The name of a piece by its item.
    PieceName = function(id)
        for _, info in ipairs(PlayerHousing_Pieces or {}) do
            if info[1] == id then
                return info[2]
            end
        end
        return "piece"
    end,
    RefreshPin = function() UpdateDetails() end,
    -- Window.lua fills this in: function(item) returning { category, building, unlocked,
    -- hint, cost, storage, placed }.
    DescribePiece = nil,
    WIDTH = WIDTH,
    CONTENT_TOP = CONTENT_TOP,
    CONTENT_HEIGHT = CONTENT_HEIGHT,
    GetFrame = function() return frame end,
    -- "bag slot" of a piece in the bags, or nil (the bags are scanned first if they changed).
    PieceLocation = function(id)
        if bagsDirty and not InCombatLockdown() then
            ScanBags()
        end
        return PieceLocation(id)
    end,
    -- Runs when the bags change (out of combat), and when combat ends.
    OnBags = function(hook) bagHooks[#bagHooks + 1] = hook end,
    OnCombatEnd = function(hook) combatEndHooks[#combatEndHooks + 1] = hook end,
    OnMessage = function(kind, hook)
        messageHooks[kind] = messageHooks[kind] or {}
        table.insert(messageHooks[kind], hook)
    end,
    -- Extras.lua fills these in: the Undo history under a button, and the row dialog.
    ShowHistory = nil,
    ShowRowDialog = nil,
    IsKnown = function() return known end,
    OnState = function(hook) stateHooks[#stateHooks + 1] = hook end,
    -- Runs once the window exists (at ADDON_LOADED).
    OnWindow = function(hook) windowHooks[#windowHooks + 1] = hook end,
    OnData = function(kind, hook)
        dataHooks[kind] = dataHooks[kind] or {}
        table.insert(dataHooks[kind], hook)
    end,
    RequestData = function(kind, argument)
        PlayerHousing_Command("data " .. kind .. (argument and (" " .. argument) or ""))
    end,
}

SLASH_PLAYERHOUSING1 = "/housing"
SlashCmdList["PLAYERHOUSING"] = function(message)
    message = strtrim(message or "")
    if message == "" then
        PlayerHousing_Toggle()
    elseif message == "auto" then
        db.autoShow = not db.autoShow
        Print(db.autoShow and "the window opens by itself when you arrive home." or "the window only opens with /housing.")
    elseif message == "minimap" then
        PlayerHousing_ToggleMinimapButton()
    elseif message == "phototour" or message == "phototour stop" then
        PlayerHousing_PhotoTour(message == "phototour")
    elseif message == "framing" then
        -- Previews center each model from its measured middle. If they sit too high or low
        -- on some client, this tries the other way of reading the model frame's offsets.
        db.framing = db.framing ~= "model" and "model" or nil
        FrameModel()
        Print(db.framing and "previews use the second way of centering. /housing framing goes back." or "previews use the usual centering.")
    elseif message == "key" then
        db.keyWindow = db.keyWindow == false
        PlayerHousing_Command("addon 1 " .. (db.keyWindow and "1" or "0"))
        Print(db.keyWindow and "the House Key opens this window." or "the House Key opens its menu.")
    else
        -- Anything else is a .house command: /housing undo, /housing rotate 90, ...
        PlayerHousing_Command(message)
    end
end
