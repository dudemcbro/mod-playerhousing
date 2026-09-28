-- Runs PlayerHousing.lua outside the game against a few stubbed WoW 3.3.5 API calls and
-- checks what it does with state messages, bag contents, clicks and the mouse wheel.
--
--   lua5.1 client-addon/test/harness.lua client-addon/PlayerHousing/PieceModels.lua client-addon/PlayerHousing/PlayerHousing.lua
--
-- (luajit works too.) It can't show how the window looks; that needs the real client.
local sent, printed = {}, {}
local frames = {}
local combat = false

local Widget = {}
Widget.__index = function(t, k)
  if k == "protected" or k == "count" or k == "icon" or k == "piece" or k == "enabled" then return nil end
  local v = rawget(Widget, k)
  if v then return v end
  return function(self, ...) return nil end   -- any unknown method: no-op
end
function Widget:SetScript(name, fn) self.scripts[name] = fn end
function Widget:GetScript(name) return self.scripts[name] end
function Widget:SetAttribute(k, v) if combat and self.protected then error("protected attribute in combat") end self.attrs[k] = v end
function Widget:GetAttribute(k) return self.attrs[k] end
function Widget:Show() if combat and self.protected then error("protected show in combat") end self.shown = true; if self.scripts.OnShow then self.scripts.OnShow(self) end end
function Widget:Hide() if combat and self.protected then error("protected hide in combat") end self.shown = false end
function Widget:IsShown() return self.shown end
function Widget:SetText(t) self.text = t end
function Widget:GetText() return self.text end
function Widget:SetHeight(h) self.height = h end
function Widget:RegisterEvent(e) self.events[e] = true end
function Widget:CreateFontString() return setmetatable({scripts={}, attrs={}, events={}, shown=true}, Widget) end
function Widget:CreateTexture(name)
  local t = setmetatable({scripts={}, attrs={}, events={}, shown=true}, Widget)
  if name then _G[name] = t end
  return t
end
function Widget:SetWidth(w) self.width = w end
function Widget:GetPoint() return "CENTER", nil, "CENTER", 10, 20 end
function Widget:Enable() self.enabled = true end
function Widget:SetModel(path) self.modelPath = path end
function Widget:ClearModel() self.modelPath = nil end
function Widget:SetUnit(unit) self.unit = unit end
function Widget:Disable() self.enabled = false end

function CreateFrame(kind, name, parent, template)
  local f = setmetatable({scripts={}, attrs={}, events={}, shown=true, name=name, parent=parent, template=template}, Widget)
  if template and template:find("Secure") then f.protected = true end
  if name then _G[name] = f end
  frames[#frames+1] = f
  return f
end
UIParent = CreateFrame("Frame", "UIParent")
GameTooltip = CreateFrame("GameTooltip", "GameTooltip")
function GameTooltip_Hide() end
DEFAULT_CHAT_FRAME = { AddMessage = function(_, m) printed[#printed+1] = m end }
function SendChatMessage(msg, kind) sent[#sent+1] = msg end
function InCombatLockdown() return combat end
function wipe(t) for k in pairs(t) do t[k] = nil end return t end
function strtrim(s) return (s:gsub("^%s+", ""):gsub("%s+$", "")) end
NUM_BAG_SLOTS = 4
CANCEL = "Cancel"
StaticPopupDialogs = {}
local popups = {}
function StaticPopup_Show(which, a) popups[#popups+1] = {which, a}; return {} end
SlashCmdList = {}
function UnitName() return "Krookowner" end
function IsControlKeyDown() return false end
function IsShiftKeyDown() return false end
function PickupContainerItem() end
local bags = {
  [0] = { [1] = {901105, 3}, [2] = {902200, 1}, [5] = {902000, 1} },
  [1] = { [3] = {901105, 2}, [4] = {902101, 1} },
}
local names = { [901105] = "Furnishing: Westfall Chair", [902200] = "Building: Broken Cart", [902000] = "House Key", [902101] = "Furnishing: Barrel",
  [901190] = "Move a Piece", [901193] = "Move a Piece" }
function GetContainerNumSlots(bag) return bags[bag] and 16 or 0 end
function GetContainerItemLink(bag, slot)
  local it = bags[bag] and bags[bag][slot]
  if not it then return nil end
  return ("|cffffffff|Hitem:%d:0:0:0:0:0:0:0:80|h[%s]|h|r"):format(it[1], names[it[1]])
end
function GetContainerItemInfo(bag, slot) local it = bags[bag][slot]; return "Interface\\Icons\\X", it[2] end

for i = 1, #arg do dofile(arg[i]) end

local driver
for _, f in ipairs(frames) do if f.events.CHAT_MSG_ADDON then driver = f end end
local function fire(e, ...) driver.scripts.OnEvent(driver, e, ...) end

-- Before the server has spoken: commands are held back.
fire("ADDON_LOADED", "PlayerHousing")
assert(PlayerHousingFrame, "window created")
PlayerHousing_Command("undo")
assert(#sent == 0 and #printed == 1, "no command before the server reports housing")

-- A state from someone else is ignored.
fire("CHAT_MSG_ADDON", "HOUSING", "state\t1\t0\t0\t\t0\t200\t0\t10\t\t\t\t0", "WHISPER", "Stranger")
assert(not PlayerHousingFrame:IsShown(), "spoofed state ignored")

-- Arriving home: the window opens by itself and lists the bag contents.
PlayerHousingFrame.shown = false
fire("CHAT_MSG_ADDON", "HOUSING", "state\t1\t1\t12\tBroken Cart\t5\t200\t1\t10\tplaced Barrel\tKrookowner\t\t1", "WHISPER", "Krookowner")
assert(PlayerHousingFrame:IsShown(), "window opened on arrival")
assert(PlayerHousingDB.known == true)
assert(PlayerHousingSlot1:IsShown() and PlayerHousingSlot3:IsShown() and not PlayerHousingSlot4:IsShown(), "three kinds of piece, key excluded")
assert(PlayerHousingSlot1.attrs.item == "1 4", "barrel first (by name)")
assert(PlayerHousingSlot2.attrs.item == "0 1" and PlayerHousingSlot2.count.text == 5, "chairs stacked: " .. tostring(PlayerHousingSlot2.count.text))
assert(PlayerHousingSlot3.attrs.item == "0 2", "buildings last")
assert(PlayerHousingSelected:IsShown(), "selected panel shown")

-- Hovering a piece previews its model and size; leaving hides it.
PlayerHousingSlot2.scripts.OnEnter(PlayerHousingSlot2)
assert(PlayerHousingPreview:IsShown(), "preview shown")
assert(PlayerHousingPreviewModel.modelPath == PlayerHousing_Models[901105][1], "chair model: " .. tostring(PlayerHousingPreviewModel.modelPath))
PlayerHousingSlot2.scripts.OnLeave(PlayerHousingSlot2)
assert(not PlayerHousingPreview:IsShown(), "preview hidden on leave")
-- Buildings made of world models get a floor plan, to scale, instead of a model.
PlayerHousing_Models[902200] = { false, 30, 10, 8 }
PlayerHousingSlot3.scripts.OnEnter(PlayerHousingSlot3)
assert(not PlayerHousingPreviewModel:IsShown(), "no model for a world model building")
assert(PlayerHousingPreviewPlan:IsShown(), "floor plan shown")
local rect = PlayerHousingPreviewPlanRect
assert(math.abs(rect.width / rect.height - 3) < 0.01, "30 by 10 yards drawn 3 to 1: " .. rect.width .. "x" .. rect.height)
assert(PlayerHousingPreviewPlanYou.width >= 10, "the person marker stays visible")
PlayerHousingSlot3.scripts.OnLeave(PlayerHousingSlot3)
-- A model piece hides the floor plan again.
PlayerHousingSlot2.scripts.OnEnter(PlayerHousingSlot2)
assert(not PlayerHousingPreviewPlan:IsShown() and PlayerHousingPreviewModel:IsShown(), "model shown, no floor plan")
PlayerHousingSlot2.scripts.OnLeave(PlayerHousingSlot2)
assert(PlayerHousingFrame.height == 500)

-- Filters and search.
PlayerHousingButton11.scripts.OnClick()  -- Buildings
assert(PlayerHousingSlot1.attrs.item == "0 2" and not PlayerHousingSlot2:IsShown(), "buildings filter")
PlayerHousingButton9.scripts.OnClick()   -- All
PlayerHousingSearchBox.text = "bar"
PlayerHousingSearchBox.scripts.OnTextChanged(PlayerHousingSearchBox)
assert(PlayerHousingSlot1.attrs.item == "1 4" and not PlayerHousingSlot2:IsShown(), "search")
PlayerHousingSearchBox.text = ""
PlayerHousingSearchBox.scripts.OnTextChanged(PlayerHousingSearchBox)

-- Toolbar buttons send .house commands.
local function last() return sent[#sent] end
PlayerHousingButton1.scripts.OnClick(); assert(last() == ".house leave", last())
PlayerHousingButton2.scripts.OnClick(); assert(last() == ".house decorate")
PlayerHousingButton3.scripts.OnClick(); assert(last() == ".house undo")
PlayerHousingButton8.scripts.OnClick(); assert(last() == ".house", last())
assert(PlayerHousingButton2.text == "Done", "decorate button shows Done while decorating")
assert(PlayerHousingButton4.enabled == false, "nothing to redo")

-- Undo tooltip reads the label.
PlayerHousingButton3.scripts.OnEnter(PlayerHousingButton3)

-- Turning: wheel notches add up into one command.
local OnUpdate = driver.scripts.OnUpdate
OnUpdate(driver, 1)
local before = #sent
PlayerHousingFrame.scripts.OnMouseWheel(PlayerHousingFrame, 1)
PlayerHousingFrame.scripts.OnMouseWheel(PlayerHousingFrame, 1)
PlayerHousingFrame.scripts.OnMouseWheel(PlayerHousingFrame, -1)
OnUpdate(driver, 0.02)
assert(#sent == before + 1 and last() == ".house rotate 15", "first turn goes at once: " .. last())
PlayerHousingFrame.scripts.OnMouseWheel(PlayerHousingFrame, 1)
PlayerHousingFrame.scripts.OnMouseWheel(PlayerHousingFrame, 1)
OnUpdate(driver, 0.1)
assert(#sent == before + 1, "throttled")
OnUpdate(driver, 0.3)
assert(last() == ".house rotate 30", last())
PlayerHousingButton15.scripts.OnClick()  -- Turn right
OnUpdate(driver, 0.5)
assert(last() == ".house rotate -15", last())
IsControlKeyDown = function() return true end
PlayerHousingFrame.scripts.OnMouseWheel(PlayerHousingFrame, 1)
OnUpdate(driver, 0.5)
assert(last() == ".house nudge up 0.10", last())
PlayerHousingButton14.scripts.OnClick()  -- Ctrl: Turn left 90
OnUpdate(driver, 0.5)
assert(last() == ".house rotate 90", last())
IsControlKeyDown = function() return false end
IsShiftKeyDown = function() return true end
PlayerHousingButton15.scripts.OnClick()  -- Shift: Turn right 5
OnUpdate(driver, 0.5)
assert(last() == ".house rotate -5", last())
PlayerHousingButton26.scripts.OnClick(); assert(last() == ".house size normal", last())
PlayerHousingButton30.scripts.OnClick(); assert(last() == ".house tilt straight", last())
IsShiftKeyDown = function() return false end
PlayerHousingButton26.scripts.OnClick(); assert(last() == ".house size bigger", last())
PlayerHousingButton27.scripts.OnClick(); assert(last() == ".house size smaller", last())
PlayerHousingButton28.scripts.OnClick(); assert(last() == ".house tilt forward", last())
PlayerHousingButton31.scripts.OnClick(); assert(last() == ".house tilt right", last())
PlayerHousingButton20.scripts.OnClick(); assert(last() == ".house nudge forward")

-- Buildings ask before being picked up.
PlayerHousingButton19.scripts.OnClick()
assert(popups[1][1] == "PLAYERHOUSING_PICKUP_BUILDING" and popups[1][2] == "Broken Cart")
StaticPopupDialogs.PLAYERHOUSING_PICKUP_BUILDING.OnAccept({}, 12); assert(last() == ".house pickup 12")
PlayerHousingButton33.scripts.OnClick()
StaticPopupDialogs.PLAYERHOUSING_PICKUP_BUILDING_ALL.OnAccept({}, 12); assert(last() == ".house pickup 12 inside")

-- A furnishing picks up straight away.
fire("CHAT_MSG_ADDON", "HOUSING", "state\t1\t1\t13\tBarrel\t5\t200\t1\t10\tpicked up Broken Cart\tKrookowner\tplaced Barrel\t0", "WHISPER", "Krookowner")
assert(PlayerHousingButton4.enabled == true, "redo available")
PlayerHousingButton19.scripts.OnClick(); assert(last() == ".house pickup 13")

-- Moving with the targeting circle: the server hands over a Move a Piece item and says
-- which; a button uses it. The item never shows in the grid.
PlayerHousingButton18.scripts.OnClick(); assert(last() == ".house move", last())
assert(not PlayerHousingSpotButton:IsShown(), "no spot button before the move starts")
bags[0][6] = {901193, 1}
fire("BAG_UPDATE")
fire("CHAT_MSG_ADDON", "HOUSING", "state\t1\t1\t13\tBarrel\t5\t200\t1\t10\tpicked up Broken Cart\tKrookowner\t\t0\t901193", "WHISPER", "Krookowner")
assert(PlayerHousingSpotButton:IsShown() and PlayerHousingSpotButton.attrs.item == "0 6", "spot button uses the mover")
assert(PlayerHousingSpotButton.attrs.type == "item")
assert(not PlayerHousingSlot4:IsShown(), "the mover isn't a furnishing")
-- Placed: the item is used up and the server says so.
bags[0][6] = nil
fire("BAG_UPDATE")
fire("CHAT_MSG_ADDON", "HOUSING", "state\t1\t1\t13\tBarrel\t5\t200\t1\t10\tmoved Barrel\tKrookowner\t\t0\t0", "WHISPER", "Krookowner")
assert(not PlayerHousingSpotButton:IsShown(), "spot button gone after the move")
-- The item can arrive after the state: the next bag scan brings the button.
fire("CHAT_MSG_ADDON", "HOUSING", "state\t1\t1\t13\tBarrel\t5\t200\t1\t10\tmoved Barrel\tKrookowner\t\t0\t901190", "WHISPER", "Krookowner")
assert(not PlayerHousingSpotButton:IsShown())
bags[1][9] = {901190, 1}
fire("BAG_UPDATE")
driver.scripts.OnUpdate(driver, 1)
assert(PlayerHousingSpotButton:IsShown() and PlayerHousingSpotButton.attrs.item == "1 9", "spot button after the bag update")
bags[1][9] = nil
fire("BAG_UPDATE")
fire("CHAT_MSG_ADDON", "HOUSING", "state\t1\t1\t13\tBarrel\t5\t200\t1\t10\tmoved Barrel\tKrookowner\t\t0\t0", "WHISPER", "Krookowner")

-- Another like this: the server puts one in the bags, and the same button places it.
PlayerHousingButton32.scripts.OnClick(); assert(last() == ".house another", last())
fire("CHAT_MSG_ADDON", "HOUSING", "state\t1\t1\t13\tBarrel\t5\t200\t1\t10\tmoved Barrel\tKrookowner\t\t0\t0\t902101", "WHISPER", "Krookowner")
assert(PlayerHousingSpotButton:IsShown() and PlayerHousingSpotButton.attrs.item == "1 4", "spot button uses the barrel")
assert(PlayerHousingSpotButton.text == "Now place the copy", PlayerHousingSpotButton.text)
fire("CHAT_MSG_ADDON", "HOUSING", "state\t1\t1\t14\tBarrel\t6\t200\t1\t10\tplaced Barrel\tKrookowner\t\t0\t0\t0", "WHISPER", "Krookowner")
assert(not PlayerHousingSpotButton:IsShown(), "spot button gone once the copy is placed")

-- A roommate on someone else's island gets the decorating controls too.
fire("CHAT_MSG_ADDON", "HOUSING", "state\t0\t1\t21\tTiny Table\t7\t200\t1\t10\tnudged Tiny Table\tKrookfriend\t\t0\t0\t0\t1", "WHISPER", "Krookowner")
assert(PlayerHousingSelected:IsShown(), "a roommate sees the selected piece")
assert(PlayerHousingButton3.enabled == true and PlayerHousingButton2.enabled == true, "a roommate can undo and decorate")
assert(PlayerHousingFrame and PlayerHousingButton1.text == "Leave")
-- A plain visitor doesn't.
fire("CHAT_MSG_ADDON", "HOUSING", "state\t0\t0\t0\t\t0\t200\t0\t10\t\tKrookfriend\t\t0\t0\t0\t0", "WHISPER", "Krookowner")
assert(not PlayerHousingSelected:IsShown() and PlayerHousingButton2.enabled == false, "a visitor can't decorate")
fire("CHAT_MSG_ADDON", "HOUSING", "state\t1\t1\t13\tBarrel\t5\t200\t1\t10\tmoved Barrel\tKrookowner\t\t0\t0", "WHISPER", "Krookowner")

-- Combat: the grid waits, the window can't toggle.
combat = true
fire("BAG_UPDATE")
OnUpdate(driver, 1)
PlayerHousing_Toggle()
assert(printed[#printed]:find("combat"))
fire("CHAT_MSG_ADDON", "HOUSING", "state\t0\t0\t0\t\t0\t200\t0\t10\t\t\t\t0", "WHISPER", "Krookowner")
assert(PlayerHousingFrame:IsShown(), "no hide in combat")
combat = false
fire("PLAYER_REGEN_ENABLED")

-- Leaving: the window that opened by itself closes again.
fire("CHAT_MSG_ADDON", "HOUSING", "state\t0\t0\t0\t\t0\t200\t0\t10\t\t\t\t0", "WHISPER", "Krookowner")
assert(not PlayerHousingFrame:IsShown(), "closed on leaving")
assert(PlayerHousingSelected:IsShown() == false)
PlayerHousingButton1.scripts.OnClick(); assert(last() == ".house home", last())

-- Zone change asks for state; slash command.
fire("ZONE_CHANGED_NEW_AREA"); assert(last() == ".house state")
SlashCmdList.PLAYERHOUSING("rotate 90"); assert(last() == ".house rotate 90")
SlashCmdList.PLAYERHOUSING("auto"); assert(PlayerHousingDB.autoShow == false)
SlashCmdList.PLAYERHOUSING("")
assert(PlayerHousingFrame:IsShown())

-- Empty bags.
bags = {}
fire("BAG_UPDATE")
OnUpdate(driver, 1)
print("sent:", table.concat(sent, " | "))
print("ALL ADDON CHECKS PASSED")
