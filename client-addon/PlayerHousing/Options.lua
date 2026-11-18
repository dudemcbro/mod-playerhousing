-- The options page (Interface > AddOns > Player Housing): the keys that change a held piece,
-- each with its default. A click on a key waits for the new one: a key with any of Ctrl, Alt
-- and Shift, the wheel, or the middle and side mouse buttons (left and right are the mouse's
-- own, Mouse.lua). A right-click clears it; a click again stops waiting. The game's Defaults
-- button puts them all back. The keys are EditMode.lua's.

local API = PlayerHousingAPI
local ROW_HEIGHT, ROWS_TOP = 18, -96
local LABEL_X, KEY_X, KEY_WIDTH, DEFAULT_X = 16, 196, 150, 352

local panel = CreateFrame("Frame", "PlayerHousingOptions", UIParent)
panel.name = "Player Housing"
panel:Hide()

local waiting      -- the action waiting for its new key
local rows = {}    -- action -> its button
local note         -- what the last change did ("taken from ...")

local BARE_MODIFIERS = { LSHIFT = true, RSHIFT = true, LCTRL = true, RCTRL = true, LALT = true, RALT = true }
local MOUSE_BUTTONS = { MiddleButton = "BUTTON3", Button4 = "BUTTON4", Button5 = "BUTTON5" }

local function LabelOf(action)
    for _, entry in ipairs(API.KEY_ACTIONS) do
        if entry[1] == action then
            return entry[2]
        end
    end
end

local function Refresh()
    for _, entry in ipairs(API.KEY_ACTIONS) do
        local button = rows[entry[1]]
        if waiting == entry[1] then
            button:SetText("|cffffd100Press a key...|r")
        else
            button:SetText(API.KeyText(API.KeyFor(entry[1])) or "|cff808080none|r")
        end
    end
end

local function StopWaiting()
    waiting = nil
    panel:EnableKeyboard(false)
    Refresh()
end

-- The new key, with the modifiers held now.
local function Take(base)
    local action = waiting
    local key = API.JoinKey({ ALT = IsAltKeyDown(), CTRL = IsControlKeyDown(), SHIFT = IsShiftKeyDown() }, base)
    -- Setting down with Shift held sets down another: its own key can't have Shift.
    if action == "SetDown" and IsShiftKeyDown() then
        note:SetText(("|cffff6060%s can't have Shift:|r Shift with its key sets down another."):format(LabelOf(action)))
        StopWaiting()
        return
    end
    local taken = API.SetKey(action, key)
    note:SetText(taken and ("%s is now %s; |cffff6060%s|r has no key."):format(API.KeyText(key), LabelOf(action), LabelOf(taken)) or "")
    StopWaiting()
end

panel:SetScript("OnKeyDown", function(self, key)
    if waiting and not BARE_MODIFIERS[key] then
        Take(key)
    end
end)
panel:SetScript("OnHide", StopWaiting)

local title = panel:CreateFontString(nil, "ARTWORK", "GameFontNormalLarge")
title:SetPoint("TOPLEFT", 16, -16)
title:SetText("Player Housing")
local help = panel:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
help:SetPoint("TOPLEFT", title, "BOTTOMLEFT", 0, -8)
help:SetPoint("RIGHT", panel, "RIGHT", -16, 0)
help:SetJustifyH("LEFT")
help:SetText("The keys that change a piece while you hold it; the rest of the time they're the game's own. "
    .. "Click a key, then press the new one, turn the wheel or click a mouse button. Right-click: no key.")
note = panel:CreateFontString("PlayerHousingOptionsNote", "ARTWORK", "GameFontHighlightSmall")
note:SetPoint("TOPLEFT", help, "BOTTOMLEFT", 0, -6)
note:SetText("")

local function Header(text, y)
    local header = panel:CreateFontString(nil, "ARTWORK", "GameFontNormal")
    header:SetPoint("TOPLEFT", LABEL_X, y)
    header:SetText(text)
    local default = panel:CreateFontString(nil, "ARTWORK", "GameFontNormalSmall")
    default:SetPoint("TOPLEFT", DEFAULT_X, y)
    default:SetText("Default")
end

local y = ROWS_TOP
Header("Holding a piece", y)
y = y - ROW_HEIGHT
for index, entry in ipairs(API.KEY_ACTIONS) do
    local action = entry[1]
    if entry.follow and not API.KEY_ACTIONS[index - 1].follow then
        y = y - 6
        Header("Without PlayerHousing.dll (the piece follows you)", y)
        y = y - ROW_HEIGHT
    end
    local label = panel:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
    label:SetPoint("TOPLEFT", LABEL_X, y - 3)
    label:SetText(entry[2])
    local button = CreateFrame("Button", "PlayerHousingOptionsKey" .. action, panel, "UIPanelButtonTemplate")
    button:SetPoint("TOPLEFT", KEY_X, y)
    button:SetWidth(KEY_WIDTH)
    button:SetHeight(ROW_HEIGHT)
    button:RegisterForClicks("AnyUp")
    button:EnableMouseWheel(true)
    button:SetScript("OnClick", function(self, mouseButton)
        if waiting == action then
            if MOUSE_BUTTONS[mouseButton] then
                Take(MOUSE_BUTTONS[mouseButton])
            else
                StopWaiting()
            end
        elseif mouseButton == "RightButton" then
            note:SetText("")
            API.SetKey(action, "")
            StopWaiting()
        elseif mouseButton == "LeftButton" then
            note:SetText("")
            waiting = action
            panel:EnableKeyboard(true)
            Refresh()
        end
    end)
    button:SetScript("OnMouseWheel", function(self, delta)
        if waiting == action then
            Take(delta > 0 and "MOUSEWHEELUP" or "MOUSEWHEELDOWN")
        end
    end)
    rows[action] = button
    local default = panel:CreateFontString(nil, "ARTWORK", "GameFontDisableSmall")
    default:SetPoint("TOPLEFT", DEFAULT_X, y - 3)
    default:SetText(API.KeyText(entry[3]))
    y = y - ROW_HEIGHT
end

panel.refresh = Refresh
panel.default = function()
    note:SetText("")
    API.ResetKeys()
    StopWaiting()
end
-- Okay and Cancel: every change is kept as it's made.
panel.okay = function() end
panel.cancel = function() end

InterfaceOptions_AddCategory(panel)
API.OnWindow(Refresh)
