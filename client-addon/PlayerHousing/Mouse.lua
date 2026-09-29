-- Placing with the mouse: with PlayerHousing.dll (client-dll/), a piece being placed or moved
-- follows the mouse over the world, and a click sets it down there (Shift-click: then another
-- of the same). The game can't say where the mouse points; the DLL adds
-- PlayerHousing_CursorWorld, and this sends that point to the server a few times a second.
--
-- Clicks on the window or the banner stay theirs; a click on the world sets the piece down.
-- Holding a mouse button down turns the camera, as ever: the piece waits, and a drag isn't a
-- click. Without the DLL none of this runs, and the piece follows you instead.

local API = PlayerHousingAPI
local SEND_EVERY = 0.1   -- seconds between points while the mouse moves
local MIN_MOVE = 0.05    -- yards: a point closer than this to the last one isn't sent
local CLICK_TIME = 0.4   -- seconds: a press longer than this is a drag (turning the camera)
local DRAG_MOVE = 0.5    -- yards: the point under the mouse moved this much while held: a drag

local driver = CreateFrame("Frame", "PlayerHousingMouse", UIParent)
local sinceSend = 0
local lastX, lastY, lastZ
local press   -- the left button went down over the world: { time, x, y, z }

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

-- Where the mouse points in the world (and which way the surface there faces), or nil.
local function Pointed()
    local fx, fy = CursorFraction()
    if not fx then
        return nil
    end
    return PlayerHousing_CursorWorld(fx, fy)
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

driver:SetScript("OnUpdate", function(self, elapsed)
    local state = API.state
    if state.ghostItem == 0 or not API.CanEdit() or not API.HasMouse() then
        press, lastX = nil, nil
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
                if API.Feedback then
                    API.Feedback(another and "Shift-click" or "Click", another and "set down; another follows" or "set down")
                end
                return
            end
        end
    end
    if down or IsMouseButtonDown("RightButton") then
        return  -- turning the camera: the piece waits
    end

    sinceSend = sinceSend + elapsed
    if sinceSend < SEND_EVERY or not OverWorld() then
        return
    end
    sinceSend = 0
    local x, y, z, nx, ny, nz = Pointed()
    if x then
        Send(x, y, z, nx, ny, nz)
    end
end)
