-- A button on the minimap's edge: a click opens or closes the housing window.
-- Drag it around the edge; /housing minimap hides or shows it. Where it sits is saved.

local API = PlayerHousingAPI
local button

local function Place()
    local angle = math.rad(PlayerHousingDB.minimapAngle or 200)
    button:ClearAllPoints()
    button:SetPoint("CENTER", Minimap, "CENTER", 80 * math.cos(angle), 80 * math.sin(angle))
end

local function FollowCursor()
    local centerX, centerY = Minimap:GetCenter()
    local cursorX, cursorY = GetCursorPosition()
    local scale = Minimap:GetEffectiveScale()
    PlayerHousingDB.minimapAngle = math.deg(math.atan2(cursorY / scale - centerY, cursorX / scale - centerX)) % 360
    Place()
end

local function ShowTooltip(self)
    GameTooltip:SetOwner(self, "ANCHOR_LEFT")
    GameTooltip:SetText("Player Housing")
    GameTooltip:AddLine("Click: the housing window.", 1, 1, 1)
    GameTooltip:AddLine("Drag to move. /housing minimap hides it.", 0.7, 0.7, 0.7)
    GameTooltip:Show()
end

local function Create()
    button = CreateFrame("Button", "PlayerHousingMinimapButton", Minimap)
    button:SetWidth(31)
    button:SetHeight(31)
    button:SetFrameStrata("MEDIUM")
    button:SetFrameLevel(8)
    button:SetHighlightTexture("Interface\\Minimap\\UI-Minimap-ZoomButton-Highlight")
    local icon = button:CreateTexture("PlayerHousingMinimapButtonIcon", "BACKGROUND")
    icon:SetWidth(20)
    icon:SetHeight(20)
    icon:SetPoint("TOPLEFT", 7, -5)
    icon:SetTexture("Interface\\Icons\\INV_Misc_Key_11")
    local border = button:CreateTexture(nil, "OVERLAY")
    border:SetWidth(53)
    border:SetHeight(53)
    border:SetPoint("TOPLEFT")
    border:SetTexture("Interface\\Minimap\\MiniMap-TrackingBorder")

    button:RegisterForClicks("LeftButtonUp")
    button:RegisterForDrag("LeftButton")
    button:SetScript("OnClick", function() PlayerHousing_Toggle() end)
    button:SetScript("OnDragStart", function(self)
        self:SetScript("OnUpdate", FollowCursor)
    end)
    button:SetScript("OnDragStop", function(self)
        self:SetScript("OnUpdate", nil)
    end)
    button:SetScript("OnEnter", ShowTooltip)
    button:SetScript("OnLeave", GameTooltip_Hide)
    Place()
    if PlayerHousingDB.minimapHidden then
        button:Hide()
    end
end

function PlayerHousing_ToggleMinimapButton()
    PlayerHousingDB.minimapHidden = not PlayerHousingDB.minimapHidden
    if PlayerHousingDB.minimapHidden then
        button:Hide()
        API.Print("the minimap button is hidden. /housing minimap brings it back.")
    else
        button:Show()
    end
end

API.OnWindow(Create)
