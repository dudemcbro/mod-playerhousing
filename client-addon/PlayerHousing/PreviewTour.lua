-- The preview tour: the preview, in the middle of the screen on black, shows each piece of a
-- task list (PreviewTask.lua) exactly as told and takes a screenshot of it, a little over a
-- second apart (screenshots are named to the second). Under the model a strip of squares
-- spells out which piece and which shot it is, so each screenshot reads back without any
-- bookkeeping. tools/pictures/preview_fit.py measures where each model landed and writes
-- PreviewFix.lua: how near and how high each piece starts in the preview.
--
-- It runs by itself once after logging in or /reload when the task list has a new run name;
-- /housing previewtour runs it again, /housing previewtour stop stops it.

local API = PlayerHousingAPI
local BITS = 24             -- 20 for the piece's item (up to 1048575), 4 for the shot
local SQUARE = 8            -- UI units per square
local SHOW_TIME, SHOT_TIME = 0.6, 0.75   -- seconds: model drawn, then screenshot taken

local driver = CreateFrame("Frame", "PlayerHousingPreviewTour")
local squares = {}
local queue, index, phase, waitUntil
local strip

local function Code(value)
    for bit = 1, BITS do
        local on = math.floor(value / 2 ^ (BITS - bit)) % 2 == 1
        squares[bit]:SetTexture(on and 1 or 0, on and 1 or 0, on and 1 or 0, 1)
    end
end

local function MakeStrip(preview)
    if strip then
        return
    end
    -- A white square at each end marks where the strip starts and ends.
    strip = CreateFrame("Frame", "PlayerHousingPreviewTourStrip", preview)
    strip:SetWidth((BITS + 2) * SQUARE)
    strip:SetHeight(SQUARE)
    strip:SetPoint("TOP", preview, "TOP", 0, -228)
    local back = strip:CreateTexture(nil, "BACKGROUND")
    back:SetAllPoints()
    back:SetTexture(0.5, 0.5, 0.5, 1)
    for i = 0, BITS + 1 do
        local square = strip:CreateTexture(nil, "ARTWORK")
        square:SetWidth(SQUARE)
        square:SetHeight(SQUARE)
        square:SetPoint("LEFT", strip, "LEFT", i * SQUARE, 0)
        if i == 0 or i == BITS + 1 then
            square:SetTexture(1, 0, 0, 1)   -- red: the ends
        else
            squares[i] = square
        end
    end
end

-- The game's "Screen Captured" notice shows in the middle of the screen, over the preview, for
-- the next shot: quiet during the tour.
local function QuietNotices(quiet)
    if ActionStatus then
        for _, event in ipairs({ "SCREENSHOT_SUCCEEDED", "SCREENSHOT_FAILED" }) do
            if quiet then ActionStatus:UnregisterEvent(event) else ActionStatus:RegisterEvent(event) end
        end
        if quiet then ActionStatus:Hide() end
    end
end

local function Stop(message)
    queue = nil
    QuietNotices(false)
    driver:Hide()
    if strip then
        strip:Hide()
    end
    API.PreviewTourMode(false)
    API.Print(message)
end

local function Start()
    local task = PlayerHousing_PreviewTask
    if not task or not task.shots or #task.shots == 0 then
        API.Print("no preview tour to run (PreviewTask.lua is empty).")
        return
    end
    if InCombatLockdown() then
        API.Print("the preview tour waits for the end of combat: /housing previewtour.")
        return
    end
    local preview = API.PreviewTourMode(true)
    if not preview then
        return
    end
    MakeStrip(preview)
    strip:Show()
    QuietNotices(true)
    queue, index, phase, waitUntil = task.shots, 1, "show", GetTime() + 1
    driver:Show()
    API.Print(("preview tour %s: %d shots, about %d minutes. /housing previewtour stop stops it."):format(
        tostring(task.run), #queue, math.ceil(#queue * (SHOW_TIME + SHOT_TIME) / 60)))
end

driver:SetScript("OnUpdate", function(self, elapsed)
    if not queue or GetTime() < waitUntil then
        return
    end
    if InCombatLockdown() then
        Stop("preview tour stopped for combat.")
        return
    end
    local shot = queue[index]
    if not shot then
        if PlayerHousingDB then
            PlayerHousingDB.previewTourDone = PlayerHousing_PreviewTask.run
        end
        Stop(("preview tour done: %d shots."):format(#queue))
        return
    end
    if phase == "show" then
        -- { item, shot number (0-15), zoom, lift, facing }
        API.PreviewShot(shot[1], shot[3], shot[4], shot[5])
        Code(shot[1] * 16 + shot[2])
        phase, waitUntil = "shoot", GetTime() + SHOW_TIME
    else
        Screenshot()
        index = index + 1
        phase, waitUntil = "show", GetTime() + SHOT_TIME
    end
end)
driver:Hide()

-- Once after logging in or /reload, for a task list it hasn't run.
local starter = CreateFrame("Frame")
starter:RegisterEvent("PLAYER_ENTERING_WORLD")
starter:SetScript("OnEvent", function(self)
    self:UnregisterEvent("PLAYER_ENTERING_WORLD")
    local task = PlayerHousing_PreviewTask
    if task and task.run and PlayerHousingDB and PlayerHousingDB.previewTourDone ~= task.run then
        local wait = 5
        self:SetScript("OnUpdate", function(_, elapsed)
            wait = wait - elapsed
            if wait <= 0 then
                self:SetScript("OnUpdate", nil)
                Start()
            end
        end)
    end
end)

API.PreviewTour = function(stop)
    if stop then
        if queue then Stop("preview tour stopped.") end
    else
        Start()
    end
end
