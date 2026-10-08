-- Placing with the mouse: with PlayerHousing.dll (client-dll/), a piece being placed or moved
-- follows the mouse over the world, a click sets it down there (Shift-click: then another of
-- the same), a right-click puts it back (as Escape does), and Shift+right-click puts a moved
-- piece away in the Collection. The game can't say where the mouse points; the DLL adds
-- PlayerHousing_CursorWorld, and this sends that point to the server a few times a second.
--
-- With version 2 of the DLL (PlayerHousing_PlaceUnit) the ghost moves on this screen itself,
-- every frame, instead of waiting for the server to move it there: the server tells which
-- creatures make up the ghost (gpiece) and, for each point it takes, where its own rules put
-- the piece (gpose: the grid, a table's top, the ground under a building, a wall). While the
-- mouse moves the ghost follows it by those rules; once the mouse rests it sits exactly where
-- the server has it, which is where a click sets it down.
--
-- Clicks on the window stay its own; a click on the world sets the piece down. Holding a
-- mouse button down turns the camera, as ever: the piece waits, and a drag isn't a click. Without the DLL none of this runs, and the piece follows you instead.

local API = PlayerHousingAPI
local SEND_EVERY = 0.1   -- seconds between points while the mouse moves
local MIN_MOVE = 0.05    -- yards: a point closer than this to the last one isn't sent
local CLICK_TIME = 0.4   -- seconds: a press longer than this is a drag (turning the camera)
local DRAG_MOVE = 0.5    -- yards: the point under the mouse moved this much while held: a drag
local STILL_TIME = 0.3   -- seconds the mouse rests before the ghost shows the server's spot exactly
local WALL_STEEPNESS = 0.6  -- a surface facing up less than this is a wall (the server's rule too)

local driver = CreateFrame("Frame", "PlayerHousingMouse", UIParent)
local sinceSend = 0
local lastX, lastY, lastZ
local press      -- the left button went down over the world: { time, x, y, z }
local rightPress -- the right button went down over the world: { time, cursor x, cursor y }
local inWorld = false  -- from entering the world until leaving it: never during a loading screen

-- The ghost as the server described it: its creatures (guid halves and where each sits from the
-- lead), and the last pose.
local ghost = { pieces = {}, pose = nil }
local hitX, hitY, hitZ, hitMovedAt  -- the point under the mouse, and when it last moved

local buildings = {}
for _, info in ipairs(PlayerHousing_Pieces or {}) do
    if info[4] == 1 then
        buildings[info[1]] = true
    end
end

local function ForgetGhost()
    wipe(ghost.pieces)
    ghost.pose = nil
    hitX, hitMovedAt = nil, nil
end

-- The mouse as a fraction of the world's view, from its bottom-left corner.
local function CursorFraction()
    local left, bottom = WorldFrame:GetLeft(), WorldFrame:GetBottom()
    local width, height = WorldFrame:GetWidth(), WorldFrame:GetHeight()
    if not left or not bottom or not width or not height or width <= 0 or height <= 0 then
        return nil
    end
    local scale = WorldFrame:GetEffectiveScale()
    local cursorX, cursorY = GetCursorPosition()
    return (cursorX / scale - left) / width, (cursorY / scale - bottom) / height
end

-- The held piece itself, where it's shown now: a tilted one is the real object, which the mouse
-- can hit. A point on it would pull the piece toward the camera, a little more each time
-- (each new point on its near side), so such a point is taken where the mouse's ray meets the
-- floor the piece stands on instead (PlayerHousing.dll 3), or not at all.
local function OnHeldPiece(x, y, z)
    local pose = ghost.pose
    local data = pose and PlayerHousing_Models and PlayerHousing_Models[API.state.ghostItem]
    if not data then
        return false
    end
    local size = (API.state.ghostSize or 100) / 100
    local length, depth, height = (data[2] or 1) * size, (data[3] or 1) * size, (data[4] or 1) * size
    local radius = 0.5 * math.sqrt(length * length + depth * depth + height * height) + 0.3
    local cx, cy, cz = pose.x, pose.y, pose.z + height / 2
    return (x - cx) ^ 2 + (y - cy) ^ 2 + (z - cz) ^ 2 <= radius * radius
end

local function OnFloorUnder(fx, fy)
    if type(PlayerHousing_CursorRay) ~= "function" then
        return nil
    end
    local ox, oy, oz, dx, dy, dz = PlayerHousing_CursorRay(fx, fy)
    if not dz or dz > -0.02 then
        return nil  -- looking level or up: the ray never meets the floor
    end
    local pose = ghost.pose
    local floor = pose.z - pose.lift - pose.zfix
    local t = (floor - oz) / dz
    return ox + dx * t, oy + dy * t, floor, 0, 0, 1
end

-- Where the mouse points in the world (and which way the surface there faces), or nil.
local function Pointed()
    local fx, fy = CursorFraction()
    if not fx then
        return nil
    end
    local x, y, z, nx, ny, nz = PlayerHousing_CursorWorld(fx, fy)
    if x and OnHeldPiece(x, y, z) then
        return OnFloorUnder(fx, fy)
    end
    return x, y, z, nx, ny, nz
end

-- x y z, and which way the surface faces when the DLL could tell.
local function PointText(x, y, z, nx, ny, nz)
    local text = ("%.2f %.2f %.2f"):format(x, y, z)
    if nx then
        text = text .. (" %.2f %.2f %.2f"):format(nx, ny, nz)
    end
    return text
end

local function Send(x, y, z, nx, ny, nz)
    if lastX and math.abs(x - lastX) < MIN_MOVE and math.abs(y - lastY) < MIN_MOVE and math.abs(z - lastZ) < MIN_MOVE then
        return
    end
    lastX, lastY, lastZ = x, y, z
    API.FastCommand("ghost at " .. PointText(x, y, z, nx, ny, nz))
end

local function OverWorld()
    return GetMouseFocus() == WorldFrame and not IsMouselooking()
end

-- This client moves the ghost itself: the DLL can, the player didn't turn it off, and the server
-- has said what the ghost is.
local function MovesLocally()
    return API.HasLocalGhosts and API.HasLocalGhosts() and ghost.pose ~= nil and #ghost.pieces > 0
end

-- Where the lead goes for the point under the mouse, by the server's rules: on the grid (not on
-- a wall), lifted, raised or lowered as the server last did (a table's top, the ground under a
-- building), facing out from a wall or as turned.
local function LeadSpot(x, y, z, nx, ny, nz)
    local pose = ghost.pose
    local onWall = false
    if nx and not buildings[API.state.ghostItem] then
        local length = math.sqrt(nx * nx + ny * ny + nz * nz)
        onWall = length > 0.5 and length < 1.5 and math.abs(nz / length) < WALL_STEEPNESS
    end
    local grid = API.state.grid or 0
    if grid > 0 and not onWall then
        x = math.floor(x / grid + 0.5) * grid
        y = math.floor(y / grid + 0.5) * grid
    end
    local facing = onWall and (math.atan2(ny, nx) + pose.wallTurn) or pose.turn
    return x, y, z + pose.lift + pose.zfix, facing
end

local function DrawLocally(x, y, z, nx, ny, nz)
    local now = GetTime()
    if x then
        if not hitX or math.abs(x - hitX) + math.abs(y - hitY) + math.abs(z - hitZ) > 0.02 then
            hitMovedAt = now
        end
        hitX, hitY, hitZ = x, y, z
    end
    -- Resting (or pointing where it can't go): exactly where the server has it, which is where a
    -- click puts it; moving: under the mouse.
    local pose = ghost.pose
    local lx, ly, lz, lo = pose.x, pose.y, pose.z, pose.o
    if x and hitMovedAt and now - hitMovedAt < STILL_TIME and (API.state.ghostNote or "") == "" then
        lx, ly, lz, lo = LeadSpot(x, y, z, nx, ny, nz)
    end
    local c, s = math.cos(lo), math.sin(lo)
    for _, piece in ipairs(ghost.pieces) do
        if piece.hi ~= 0 or piece.lo ~= 0 then
            PlayerHousing_PlaceUnit(piece.hi, piece.lo, lx + piece.dx * c - piece.dy * s, ly + piece.dx * s + piece.dy * c,
                lz + piece.dz, lo + piece.dO)
        end
    end
end

driver:SetScript("OnUpdate", function(self, elapsed)
    local state = API.state
    if state.ghostItem == 0 or not API.CanEdit() or not API.HasMouse() or not inWorld then
        press, rightPress, lastX = nil, nil, nil
        return
    end

    -- A click on the world: the piece goes there (the point goes with the click, so a spot it
    -- can't go isn't swapped for an older one). A long press, one that turned the camera, or
    -- one that started on the sky is a drag.
    local down = IsMouseButtonDown("LeftButton")
    if down and not press and OverWorld() then
        local x, y, z = Pointed()
        press = { time = GetTime(), x = x, y = y, z = z }
    elseif not down and press then
        local started = press
        press = nil
        if GetTime() - started.time <= CLICK_TIME and OverWorld() then
            local x, y, z, nx, ny, nz = Pointed()
            local turned = not started.x or (x and (math.abs(x - started.x) + math.abs(y - started.y) + math.abs(z - started.z)) > DRAG_MOVE)
            if x and not turned then
                lastX, lastY, lastZ = x, y, z
                local another = IsShiftKeyDown() and not state.ghostMove
                API.Command((another and "ghost place another at " or "ghost place at ") .. PointText(x, y, z, nx, ny, nz))
                return
            end
        end
    end

    -- A right-click on the world (not a drag turning the camera): never mind.
    local rightDown = IsMouseButtonDown("RightButton")
    if rightDown and not rightPress and OverWorld() then
        -- The right button turns the player while held (the cursor stays put): a turn is a drag.
        local cx, cy = GetCursorPosition()
        rightPress = { time = GetTime(), x = cx, y = cy, facing = GetPlayerFacing() or 0 }
    elseif not rightDown and rightPress then
        local started = rightPress
        rightPress = nil
        local cx, cy = GetCursorPosition()
        local turned = math.abs(((GetPlayerFacing() or 0) - started.facing + math.pi) % (2 * math.pi) - math.pi) > 0.02
        if GetTime() - started.time <= CLICK_TIME and not turned and math.abs(cx - started.x) + math.abs(cy - started.y) < 8 then
            API.Command("ghost cancel")
            -- Shift: a piece being moved goes back to the Collection (Undo puts it back).
            if IsShiftKeyDown() and state.ghostMove and API.PickUp then
                API.PickUp(true)
            end
            return
        end
    end

    local busy = down or rightDown  -- turning the camera: the piece waits
    local x, y, z, nx, ny, nz
    if not busy and OverWorld() then
        x, y, z, nx, ny, nz = Pointed()
    end
    if MovesLocally() then
        DrawLocally(x, y, z, nx, ny, nz)
    end
    if busy then
        return
    end

    sinceSend = sinceSend + elapsed
    if sinceSend < SEND_EVERY or not x then
        return
    end
    sinceSend = 0
    Send(x, y, z, nx, ny, nz)
end)

driver:SetScript("OnEvent", function(self, event)
    if event == "PLAYER_ENTERING_WORLD" then
        inWorld = true
    else
        -- A loading screen is coming: the world isn't there to point at.
        inWorld = false
        ForgetGhost()
    end
end)
driver:RegisterEvent("PLAYER_ENTERING_WORLD")
driver:RegisterEvent("PLAYER_LEAVING_WORLD")

-- What the server says about the ghost a client moves itself.
API.OnMessage("gpiece", function(fields)
    local index, count = tonumber(fields[2]), tonumber(fields[3])
    if not index or not count then
        return
    end
    for extra = count + 1, #ghost.pieces do
        ghost.pieces[extra] = nil
    end
    ghost.pieces[index] = {
        hi = tonumber(fields[4] or "", 16) or 0, lo = tonumber(fields[5] or "", 16) or 0,
        dx = tonumber(fields[6]) or 0, dy = tonumber(fields[7]) or 0, dz = tonumber(fields[8]) or 0, dO = tonumber(fields[9]) or 0,
    }
end)

API.OnMessage("gpose", function(fields)
    ghost.pose = {
        x = tonumber(fields[2]) or 0, y = tonumber(fields[3]) or 0, z = tonumber(fields[4]) or 0, o = tonumber(fields[5]) or 0,
        lift = tonumber(fields[6]) or 0, turn = tonumber(fields[7]) or 0, wallTurn = tonumber(fields[8]) or 0,
        zfix = tonumber(fields[9]) or 0,
    }
end)

API.OnState(function(state)
    if state.ghostItem == 0 then
        ForgetGhost()
    end
end)
