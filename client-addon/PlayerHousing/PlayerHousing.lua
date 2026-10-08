-- Player Housing: the window for mod-playerhousing, and the way into housing (the House Key,
-- Krook and /housing open it).
--
-- Every button sends one of the .house chat commands a player could type. One way to do each
-- thing: a piece goes on the mouse (a click on its icon in the Collection, or a right-click
-- on it on the island), a left-click sets it down, a right-click or Escape puts it back. While
-- one is held, every change to it is the mouse wheel with a modifier (EditMode.lua); the held
-- panel only shows it. Undo and Redo are at the top, the history on a right-click.
-- While the window is open on your island you're decorating: chairs and chests pick up
-- instead of working.
--
-- The server whispers the island state to the player as an addon message with the prefix
-- HOUSING (PlayerHousingMgr::SendAddonState): tab separated, new fields only at the end.

local PREFIX = "HOUSING"

local COLUMNS, SLOT_SIZE, SLOT_GAP = 9, 36, 4
local WIDTH = 24 + COLUMNS * (SLOT_SIZE + SLOT_GAP)
local BASE_HEIGHT, SELECTED_HEIGHT = 400, 150
local CONTENT_TOP, CONTENT_HEIGHT = -130, 258  -- where the tabs' panels go
local SEND_INTERVAL = 0.15  -- seconds between moves sent; key presses and wheel turns in between add up

BINDING_HEADER_PLAYERHOUSING = "Player Housing"
BINDING_NAME_PLAYERHOUSING_TOGGLE = "Show or hide the housing window"
BINDING_NAME_PLAYERHOUSING_UNDO = "Undo"
BINDING_NAME_PLAYERHOUSING_REDO = "Redo"

local state = {
    own = false, decorating = false,
    selected = 0, selectedName = "", selectedBuilding = false,
    selectedStand = false,  -- the selected piece is a mannequin (it can be dressed)
    furnishings = 0, maxFurnishings = 0, buildings = 0, maxBuildings = 0,
    undo = "", redo = "", islandOwner = "",
    roommate = false,       -- decorating someone else's island, with their leave
    grid = 0,               -- yards; 0 is off
    groupSize = 0,          -- pieces selected together (Ctrl-right-click); 1 or 0: just the one
    ghostItem = 0,          -- the piece following you until you set it down (0: none)
    ghostMove = false,      -- moving pieces already placed (else placing a new one)
    ghosts = false,         -- see-through ghosts (else pieces are carried as they are)
    ghostNote = "",         -- why the ghost isn't where the mouse points ("off your island")
    ghostSize = 100,        -- the held piece: size (percent), tilt (degrees), pieces with it
    ghostPitch = 0, ghostRoll = 0, ghostCount = 0, ghostFront = 0,
}

-- Changing things: on your own island, or as a roommate on someone else's.
local function CanEdit()
    return state.own or state.roommate
end

local SyncDecorating
local db                    -- PlayerHousingDB, once loaded
local known = false         -- the server has housing: it sent us a state
local layoutPending = false
local autoShown = false
-- Moves waiting to go: yards forward, left and up (the player's way), and degrees of turn.
local pending = { forward = 0, left = 0, up = 0, turn = 0 }
local sinceSend = 0
local stateHooks, windowHooks, dataHooks, combatEndHooks, messageHooks = {}, {}, {}, {}, {}
local lists = {}            -- lists from the server for the window's tabs, by kind
local registered = false    -- told the server this session that the addon is here

local frame, statusText, selectedPanel
local preview, previewModel, previewName, previewSize, previewNote
local plan, planRect, planBorder, planYou, planYouLabel, picture
local UpdateDetails         -- the pinned piece's details, next to the window (defined there)
local homeButton, undoButton, redoButton
local heldText, heldShape, heldHelp, heldKeys
local heldButtons = {}      -- the second row while a piece is held: { button, applies }
local groupButtons = {}     -- the buttons for several selected pieces (none held)
local GROUP_NEEDS = { 1 }  -- how many selected each of those needs
local decoratingAsked       -- what the window last asked the server: decorating on or off

local function Print(text)
    DEFAULT_CHAT_FRAME:AddMessage("|cffffd000Housing:|r " .. text)
end

-- Commands go over AzerothCore's addon command channel: chat's flood limit doesn't count it
-- (holding a key, or the mouse moving a piece, never gets anyone muted) and nothing shows in
-- the chat box. It's there unless the server turned it off (AddonChannel = 0): a ping when
-- the server first reports housing, and its answer, say so; until then, and without it,
-- commands go as chat.
local commandChannel = false
local pinged = false
local fastCount = 0

local function PingChannel()
    if not pinged then
        pinged = true
        SendAddonMessage("AzerothCore", "p0000", "WHISPER", UnitName("player"))
    end
end

-- Over the channel only: nothing when it isn't there (where the mouse points, many a second).
local function FastCommand(command)
    if not known or not commandChannel then
        return
    end
    fastCount = fastCount % 9999 + 1
    SendAddonMessage("AzerothCore", ("i%04dhouse%s"):format(fastCount, command ~= "" and (" " .. command) or ""), "WHISPER", UnitName("player"))
end

-- PlayerHousing.dll (client-dll/, started with PlayerHousingLauncher.exe) says where the mouse
-- points in the world: then a piece being placed follows the mouse (Mouse.lua), when the
-- command channel is there to send it on.
local function HasMouse()
    return type(PlayerHousing_CursorWorld) == "function" and commandChannel
end

-- Version 2 of the DLL moves the ghost on this screen itself, every frame (Mouse.lua); the
-- server then only follows along. /housing localghost turns that off (the server moves it).
local function HasLocalGhosts()
    return HasMouse() and type(PlayerHousing_PlaceUnit) == "function" and not (db and db.localGhosts == false)
end

function PlayerHousing_Command(command)
    if not known then
        Print("this server hasn't reported player housing yet. Try again in a moment.")
        return
    end
    command = command or ""
    if commandChannel then
        FastCommand(command)
    elseif command == "" then
        SendChatMessage(".house", "SAY")
    else
        SendChatMessage(".house " .. command, "SAY")
    end
end

-- Typed (/housing <command>): as chat, so usage and errors show in the chat box.
local function TypedCommand(command)
    if not known then
        Print("this server hasn't reported player housing yet. Try again in a moment.")
        return
    end
    SendChatMessage(".house " .. command, "SAY")
end

local function Command(command)
    return function() PlayerHousing_Command(command) end
end

-- The server learns the addon is here, whether the mouse can place pieces, and whether this
-- client moves the ghost itself.
local function Register()
    PlayerHousing_Command("addon 2" .. (HasMouse() and " mouse" or "") .. (HasLocalGhosts() and " local" or ""))
end

---------------------------------------------------------------------------------------------
-- State from the server

local function UpdateButtons()
    if not frame then
        return
    end

    local onIsland = state.own or state.islandOwner ~= ""
    if not known then
        statusText:SetText("Waiting for the server...")
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
    if CanEdit() and state.undo ~= "" then undoButton:Enable() else undoButton:Disable() end
    if CanEdit() and state.redo ~= "" then redoButton:Enable() else redoButton:Disable() end

    -- Holding a piece: what it is, its size and tilt, and what can be done with it. Several
    -- selected (Ctrl-right-click), none held: what can be done with them together.
    local holding = CanEdit() and state.ghostItem > 0
    local several = CanEdit() and not holding and state.groupSize > 1
    local showSelected = holding or several
    if holding then
        local name = PlayerHousingAPI.PieceName(state.ghostItem)
        if state.ghostCount > 1 then
            name = ("%s and %d more"):format(name, state.ghostCount - 1)
        end
        heldText:SetText(("%s |cffffffff%s|r"):format(state.ghostMove and "Moving:" or "Placing:", name))
        local front = state.ghostFront
        local frontText
        if math.abs(front) <= 22 then
            frontText = "away from you"
        elseif math.abs(front) >= 158 then
            frontText = "toward you"
        elseif front > 0 then
            frontText = front <= 112 and "to your left" or "back-left"
        else
            frontText = front >= -112 and "to your right" or "back-right"
        end
        local shape = ("Size %d%%   Front points %s"):format(state.ghostSize, frontText)
        if state.ghostPitch ~= 0 or state.ghostRoll ~= 0 then
            shape = shape .. ("   Tilted %d\194\176 forward, %d\194\176 to its side"):format(state.ghostPitch, state.ghostRoll)
        end
        shape = shape .. (state.grid > 0 and ("   Grid %s yd"):format(state.grid) or "")
        if state.ghostNote ~= "" then
            shape = shape .. "   |cffff6060" .. state.ghostNote .. "|r"
        end
        heldShape:SetText(shape)
        -- What the mouse does: a key in gold, what it does in white, one to a line, in two
        -- columns (the left one the clicks, the right one the wheel).
        local function Lines(rows)
            local text = {}
            for _, row in ipairs(rows) do
                text[#text + 1] = "|cffffd100" .. row[1] .. "|r  " .. row[2]
            end
            return table.concat(text, "\n")
        end
        if PlayerHousingAPI.HasMouse() then
            heldHelp:SetText(Lines({
                { "Left-click", "set it down" }, { "Shift+left-click", "and another" },
                { "Right-click, Esc", "never mind" }, { "Shift+right-click", "put it away" },
                { "Middle-click", "stand it straight" }, { "Ctrl+middle", "normal size" }, { "Shift+middle", "grid" },
            }))
        else
            heldHelp:SetText(Lines({
                { "G", "set it down" }, { "Shift+G", "and another" }, { "Esc", "never mind" },
                { "Arrows", "farther, nearer, sideways" },
                { "Middle-click", "stand it straight" }, { "Ctrl+middle", "normal size" }, { "Shift+middle", "grid" },
            }))
        end
        heldKeys:SetText(Lines({
            { "Wheel", "zoom (as ever)" }, { "Shift+wheel", "turn" }, { "Ctrl+wheel", "up, down" },
            { "Ctrl+Shift+wheel", "turn finely" }, { "Alt+wheel", "tilt forward, back" },
            { "Alt+Shift+wheel", "tilt to its side" }, { "Ctrl+Alt+wheel", "size" },
        }))
        -- The second row: the ones that apply, side by side.
        local x = 12
        for _, held in ipairs(heldButtons) do
            local button = held[1]
            if held[2]() then
                button:ClearAllPoints()
                button:SetPoint("TOPLEFT", selectedPanel, "TOPLEFT", x, -124)
                button:Show()
                x = x + button:GetWidth() + 4
            else
                button:Hide()
            end
        end
    elseif several then
        heldText:SetText(("Selected: |cffffffff%d pieces|r"):format(state.groupSize))
        heldShape:SetText("")
        heldHelp:SetText("|cffffd100Right-click|r one of them: they all go on the mouse together.\n|cffffd100Ctrl+right-click|r: add a piece, or take it out.")
        heldKeys:SetText("")
    end
    for _, held in ipairs(heldButtons) do
        if not holding then held[1]:Hide() end
    end
    for index, button in ipairs(groupButtons) do
        if several then button:Show() else button:Hide() end
        if state.groupSize >= GROUP_NEEDS[index] then button:Enable() else button:Disable() end
    end
    if showSelected then selectedPanel:Show() else selectedPanel:Hide() end

    -- Resizing a window that holds secure buttons also waits for the end of combat.
    if not InCombatLockdown() then
        frame:SetHeight(BASE_HEIGHT + (showSelected and SELECTED_HEIGHT or 0))
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

-- The window open on an island you may change: decorating (chairs, chests and the rest pick
-- up instead of working). Closed: they work again, once nothing is held.
SyncDecorating = function()
    if not frame or not known or not CanEdit() then
        decoratingAsked = nil
        return
    end
    local want = frame:IsShown() or state.ghostItem > 0
    if want == state.decorating then
        decoratingAsked = nil
    elseif decoratingAsked ~= want then
        decoratingAsked = want
        PlayerHousing_Command(want and "decorate on" or "decorate off")
    end
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
    state.roommate = fields[16] == "1"
    state.grid = tonumber(fields[18] or "") or 0
    state.groupSize = tonumber(fields[20] or "") or 0
    state.ghostItem = tonumber(fields[21] or "") or 0
    state.ghostMove = fields[22] == "move"
    state.ghosts = fields[23] == "1"
    state.ghostNote = fields[24] or ""
    state.selectedStand = fields[25] == "1"
    state.ghostSize = tonumber(fields[26] or "") or 100
    state.ghostPitch = tonumber(fields[27] or "") or 0
    state.ghostRoll = tonumber(fields[28] or "") or 0
    state.ghostCount = tonumber(fields[29] or "") or 0
    state.ghostFront = tonumber(fields[30] or "") or 0
    -- Once a session: the server learns the addon is here.
    if not registered and db then
        registered = true
        Register()
        PingChannel()
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
    SyncDecorating()
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

-- Straight away, whatever it is: Undo puts it back.
local function PickUp(withInside)
    if state.selected == 0 then
        return
    end
    if state.groupSize > 1 then
        PlayerHousing_Command("pickup")  -- the selected pieces, with what stands on them
    elseif state.selectedBuilding and withInside then
        PlayerHousing_Command("pickup " .. state.selected .. " inside")
    else
        PlayerHousing_Command("pickup " .. state.selected)
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
local detailsText, detailsCounts, detailsHint, getOneButton, getFiveButton

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
    detailsCounts:SetText(("Owned: %d   Placed: %d"):format(info.owned or 0, info.placed or 0))
    if info.unlocked then getOneButton:Enable() getFiveButton:Enable() else getOneButton:Disable() getFiveButton:Disable() end
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
    for _, region in ipairs({ detailsText, detailsCounts, getOneButton, getFiveButton }) do
        if isPinned then region:Show() else region:Hide() end
    end
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

-- A ghost of the piece follows you (or your mouse) until you set it down: one you own, or a
-- new copy from the Collection.
local function StartGhost(id)
    if not CanEdit() then
        Print("pieces go on your own island (or one you're a roommate on): Go home first.")
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

    getOneButton = DetailsButton("PlayerHousingDetailsGetOne", "Buy 1", 60, GetCopies(1), "Buy a copy", "Into your Collection, to place any time.")
    getOneButton:SetPoint("TOPLEFT", 88, -302)
    getFiveButton = DetailsButton("PlayerHousingDetailsGetFive", "Buy 5", 60, GetCopies(5), "Buy five copies", "Into your Collection.")
    getFiveButton:SetPoint("LEFT", getOneButton, "RIGHT", 2, 0)

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
    frame:SetScript("OnShow", function()
        UpdateButtons()
        SyncDecorating()
    end)
    frame:HookScript("OnHide", function() SyncDecorating() end)
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
        { "Help", function() PlayerHousing_Help() end, "How housing works", "A few lines in your chat." },
    })
    homeButton, undoButton, redoButton = row[1], row[2], row[3]
    undoButton:RegisterForClicks("LeftButtonUp", "RightButtonUp")

    -- The held panel: the piece on the mouse (or following you), until it's set down. With
    -- several pieces selected and none held, what can be done with them together.
    selectedPanel = CreateFrame("Frame", "PlayerHousingSelected", frame)
    selectedPanel:SetPoint("TOPLEFT", 0, -BASE_HEIGHT + 12)
    selectedPanel:SetWidth(WIDTH)
    selectedPanel:SetHeight(SELECTED_HEIGHT)
    heldText = selectedPanel:CreateFontString("PlayerHousingHeldText", "OVERLAY", "GameFontNormal")
    heldText:SetPoint("TOPLEFT", 14, 0)
    heldText:SetPoint("TOPRIGHT", -14, 0)
    heldText:SetJustifyH("LEFT")
    heldShape = selectedPanel:CreateFontString("PlayerHousingHeldShape", "OVERLAY", "GameFontHighlightSmall")
    heldShape:SetPoint("TOPLEFT", 14, -16)
    heldShape:SetPoint("TOPRIGHT", -14, -16)
    heldShape:SetJustifyH("LEFT")

    -- Everything done to a held piece is the mouse's (EditMode.lua, Mouse.lua): the panel only
    -- says what it is and which wheel does what. A mannequin's clothes aren't placing: Dress.
    local function HeldButton(text, width, onClick, applies, tooltipTitle, tooltipText)
        local button = MakeButton(selectedPanel, text, width, onClick, tooltipTitle, tooltipText)
        button:Hide()
        heldButtons[#heldButtons + 1] = { button, applies }
        return button
    end
    HeldButton("Dress...", 70, function()
        if PlayerHousingAPI.ShowDress then
            PlayerHousingAPI.ShowDress(state.selected)
        end
    end, function() return state.ghostMove and state.selectedStand and state.ghostCount <= 1 end,
        "Dress the mannequin", "Put gear from your bags on it, take it off, or change its figure.")

    -- Several selected (Ctrl-right-click), none held: moving them is a right-click on one.
    groupButtons = Row(selectedPanel, -70, {
        { "Save as a set", function() StaticPopup_Show("PLAYERHOUSING_SAVE_SET") end, "Save as a set",
          "The selected pieces, and what stands on them, saved together to set down anywhere (Layouts tab, Sets)." },
    })

    heldHelp = selectedPanel:CreateFontString("PlayerHousingHeldHelp", "OVERLAY", "GameFontHighlightSmall")
    heldHelp:SetPoint("TOPLEFT", 14, -34)
    heldHelp:SetWidth(WIDTH / 2 - 14)
    heldHelp:SetJustifyH("LEFT")
    heldHelp:SetJustifyV("TOP")
    heldKeys = selectedPanel:CreateFontString("PlayerHousingHeldKeys", "OVERLAY", "GameFontHighlightSmall")
    heldKeys:SetPoint("TOPLEFT", WIDTH / 2, -34)
    heldKeys:SetWidth(WIDTH / 2 - 14)
    heldKeys:SetJustifyH("LEFT")
    heldKeys:SetJustifyV("TOP")

    selectedPanel:Hide()

    CreatePreview()
    frame:HookScript("OnHide", Unpin)
    for _, hook in ipairs(windowHooks) do
        hook(frame)
    end
end

-- The House Key, Krook, a click on a piece: open the window (on a tab, when given).
function PlayerHousing_Open(tab)
    if not frame then
        return
    end
    if InCombatLockdown() then
        Print("the housing window can't open in combat.")
        return
    end
    autoShown = false
    SetShown(true)
    if tab and tab ~= "" and PlayerHousing_SelectTab then
        PlayerHousing_SelectTab(tab)
    end
end

function PlayerHousing_Help()
    Print("Placing: click a piece in the Collection and it goes on your mouse. Left-click sets it down, right-click or Escape puts it back.")
    Print("Moving: right-click a piece on your island and it goes on your mouse the same way. Buildings: the Placed tab's Move.")
    Print("While you hold it, the wheel zooms as ever, and with a key it changes the piece: turn (Shift), up and down (Ctrl), fine turn (Ctrl+Shift), tilt forward and back (Alt), tilt to its side (Alt+Shift), size (Ctrl+Alt).")
    Print("Middle-click stands it straight, Ctrl+middle-click: normal size, Shift+middle-click: grid. Shift+right-click puts it away. Undo takes back anything.")
    Print("Ctrl-right-click pieces to select several, then right-click one of them to move them all.")
    Print("/housing opens this window; /housing <command> runs any .house command; .house help lists them.")
end

-- A piece by its number (the Placed tab), straight away like the selected one.
local function PickUpPlacement(id)
    PlayerHousing_Command("pickup " .. id)
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
    elseif event == "PLAYER_REGEN_ENABLED" then
        if layoutPending then
            UpdateButtons()
        end
        for _, hook in ipairs(combatEndHooks) do
            hook()
        end
    elseif event == "CHAT_MSG_ADDON" then
        local prefix, message, channel, sender = ...
        -- The command channel answers (its ping, or a command): it's there.
        if prefix == "AzerothCore" and channel == "WHISPER" and sender == UnitName("player") then
            if not commandChannel and message:sub(1, 1) == "a" then
                commandChannel = true
                if HasMouse() then
                    Register()  -- the server hears the mouse is there
                    RequestState()
                end
            end
            return
        end
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
            PlayerHousing_Open(fields[2])
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

for _, event in ipairs({ "ADDON_LOADED", "PLAYER_ENTERING_WORLD", "ZONE_CHANGED_NEW_AREA",
                         "PLAYER_REGEN_ENABLED", "CHAT_MSG_ADDON" }) do
    driver:RegisterEvent(event)
end

-- For EditMode.lua and Window.lua.
PlayerHousingAPI = {
    state = state,
    Command = PlayerHousing_Command,
    FastCommand = FastCommand,
    HasMouse = HasMouse,
    HasLocalGhosts = HasLocalGhosts,
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
    -- hint, cost, owned, placed }.
    DescribePiece = nil,
    WIDTH = WIDTH,
    CONTENT_TOP = CONTENT_TOP,
    CONTENT_HEIGHT = CONTENT_HEIGHT,
    GetFrame = function() return frame end,
    -- Runs when combat ends.
    OnCombatEnd = function(hook) combatEndHooks[#combatEndHooks + 1] = hook end,
    OnMessage = function(kind, hook)
        messageHooks[kind] = messageHooks[kind] or {}
        table.insert(messageHooks[kind], hook)
    end,
    -- Extras.lua fills these in: the Undo history under a button, and a mannequin's dress list.
    ShowHistory = nil,
    ShowDress = nil,
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
    elseif message == "help" then
        PlayerHousing_Help()
    elseif message == "localghost" then
        -- The DLL moving the ghost on this screen itself, or the server moving it.
        db.localGhosts = db.localGhosts == false
        Register()
        Print(db.localGhosts ~= false and "a piece being placed moves on your screen as fast as your mouse (needs PlayerHousing.dll 2)."
            or "a piece being placed is moved by the server (smoother on a fast connection only).")
    else
        -- Anything else is a .house command: /housing undo, /housing rotate 90, ...
        TypedCommand(message)
    end
end
