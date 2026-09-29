-- Runs the addon outside the game against a few stubbed WoW 3.3.5 API calls and checks what
-- it does with state messages, bag contents, clicks and the mouse wheel. The files go in the
-- order of PlayerHousing.toc:
--
--   lua5.1 client-addon/test/harness.lua $(sed -n 's|^\([A-Za-z]*\.lua\)$|client-addon/PlayerHousing/\1|p' client-addon/PlayerHousing/PlayerHousing.toc)
--
-- (luajit works too.) It can't show how the window looks; that needs the real client.
local sent, printed = {}, {}
local frames = {}
local combat = false

local Widget = {}
Widget.__index = function(t, k)
  if k == "protected" or k == "count" or k == "icon" or k == "piece" or k == "enabled" or k == "info" or k == "highlighted"
    or k == "location" or k == "item" then return nil end
  local v = rawget(Widget, k)
  if v then return v end
  return function(self, ...) return nil end   -- any unknown method: no-op
end
function Widget:SetScript(name, fn) self.scripts[name] = fn end
function Widget:HookScript(name, fn)
  local old = self.scripts[name]
  self.scripts[name] = old and function(...) old(...); fn(...) end or fn
end
function Widget:GetScript(name) return self.scripts[name] end
function Widget:SetAttribute(k, v) if combat and self.protected then error("protected attribute in combat") end self.attrs[k] = v end
function Widget:GetAttribute(k) return self.attrs[k] end
function Widget:Show() if combat and self.protected then error("protected show in combat") end self.shown = true; if self.scripts.OnShow then self.scripts.OnShow(self) end end
function Widget:Hide()
  if combat and self.protected then error("protected hide in combat") end
  local was = self.shown
  self.shown = false
  if was and self.scripts.OnHide then self.scripts.OnHide(self) end
end
function Widget:IsShown() return self.shown end
function Widget:SetText(t) self.text = t end
function Widget:GetText() return self.text end
function Widget:SetHeight(h) self.height = h end
function Widget:RegisterEvent(e) self.events[e] = true end
function Widget:CreateFontString(name)
  local t = setmetatable({scripts={}, attrs={}, events={}, shown=true}, Widget)
  if name then _G[name] = t end
  return t
end
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
function Widget:SetCreature(id) self.creature = id end
function Widget:Disable() self.enabled = false end
function Widget:GetName() return self.name end
function Widget:SetChecked(v) self.checked = v end
function Widget:GetChecked() return self.checked end
function Widget:LockHighlight() self.highlighted = true end
function Widget:UnlockHighlight() self.highlighted = false end
function Widget:SetDesaturated(v) self.desaturated = v end
function Widget:SetTexture(t) self.texture = t end
function Widget:SetTexCoord(...) self.texCoord = { ... } end
function Widget:SetPosition(x, y, z) self.position = { x, y, z } end
function Widget:GetFrameLevel() return rawget(self, "level") or 1 end
function Widget:SetModelScale(v) self.modelScale = v end
function Widget:SetFacing(v) self.facing = v end
function Widget:Click(button) if self.scripts.PreClick then self.scripts.PreClick(self, button) end
  if self.scripts.OnClick then self.scripts.OnClick(self, button, true) end
  if self.scripts.PostClick then self.scripts.PostClick(self, button) end end

function CreateFrame(kind, name, parent, template)
  local f = setmetatable({scripts={}, attrs={}, events={}, shown=true, name=name, parent=parent, template=template}, Widget)
  if template and template:find("Secure") then f.protected = true end
  if name then _G[name] = f end
  frames[#frames+1] = f
  return f
end
UIParent = CreateFrame("Frame", "UIParent")
local screenshots = 0
function Screenshot() screenshots = screenshots + 1 end
date = os.date
SAVE = "Save"
Minimap = CreateFrame("Frame", "Minimap")
function Minimap:GetCenter() return 100, 100 end
function Minimap:GetEffectiveScale() return 1 end
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
local itemCounts = { [901105] = 5 }
function GetItemCount(id) return itemCounts[id] or 0 end
function GetItemInfo(id) return nil end
function GetItemIcon(id) return nil end
function GetCoinTextureString(copper) return copper .. "c" end
DELETE = "Delete"
local now = 0
function GetTime() return now end
local cursorX, cursorY = 0, 0
function GetCursorPosition() return cursorX, cursorY end
function IsAltKeyDown() return false end
local zoomed = 0
function CameraZoomIn() zoomed = zoomed + 1 end
function CameraZoomOut() zoomed = zoomed - 1 end
local targeting, stoppedTargeting = false, 0
function SpellIsTargeting() return targeting end
function SpellStopTargeting() stoppedTargeting = stoppedTargeting + 1; targeting = false end
local overrides = {}
function SetOverrideBindingClick(owner, priority, key, button)
  if combat then error("binding in combat") end
  overrides[key] = button
end
function ClearOverrideBindings() if combat then error("binding in combat") end wipe(overrides) end
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

-- Arriving home: the window opens by itself on the Collection, where a click on a piece shows
-- it next to the window and a ghost of it follows you, to set down with G.
PlayerHousingFrame.shown = false
fire("CHAT_MSG_ADDON", "HOUSING", "state\t1\t1\t12\tBroken Cart\t5\t200\t1\t10\tplaced Barrel\tKrookowner\t\t1", "WHISPER", "Krookowner")
assert(PlayerHousingFrame:IsShown(), "window opened on arrival")
assert(PlayerHousingDB.known == true)
assert(PlayerHousingCollectionPanel:IsShown() and not PlayerHousingTabBags, "the Collection is the first tab; no Bags tab")
local chairSlot, barrelPieceSlot, cartSlot = PlayerHousingCollectionSlot1, PlayerHousingCollectionSlot11, PlayerHousingCollectionSlot28
assert(chairSlot.info[1] == 901105 and barrelPieceSlot.info[1] == 902101 and cartSlot.info[1] == 902200, "the Collection's order")
assert(chairSlot.location == "0 1" and chairSlot.count.text == 5, "the chairs in the bags, for dragging onto an action bar")
assert(barrelPieceSlot.location == "1 4" and cartSlot.location == "0 2", "the barrel and the cart too")
assert(PlayerHousingCollectionSlot2.location == nil and not chairSlot.protected, "none of the table; plain buttons, fine in combat")
assert(PlayerHousingSelected:IsShown(), "selected panel shown")
local function SEL(text)
  for _, f in ipairs(frames) do if f.parent == PlayerHousingSelected and f.text == text then return f end end
  error("no selected-panel button " .. text)
end

-- Hovering a piece previews its model and size; leaving hides it.
chairSlot.scripts.OnEnter(chairSlot)
assert(PlayerHousingPreview:IsShown(), "preview shown")
assert(PlayerHousingPreviewModel.modelPath == PlayerHousing_Models[901105][1], "chair model: " .. tostring(PlayerHousingPreviewModel.modelPath))
-- Fitted to the frame and turned about its middle, which the builder measured.
local function near(a, b) return math.abs(a - b) < 1e-6 end
local chair = PlayerHousing_Models[901105]
local fit, midX, midY, midZ = chair[5], chair[6], chair[7], chair[8]
assert(fit > 0 and midZ > 0, "the chair has bounds")
local model, fitted = PlayerHousingPreviewModel, math.min(3, 2.2 / chair[5])
assert(near(model.modelScale, fitted), "fitted: " .. tostring(model.modelScale))
assert(near(model.position[1], -midX * fitted) and near(model.position[2], -midY * fitted) and near(model.position[3], -midZ * fitted),
  "its middle at the frame's: " .. table.concat(model.position, ", "))
PlayerHousingPreview.scripts.OnUpdate(PlayerHousingPreview, 1)
local c, s = math.cos(0.6), math.sin(0.6)
assert(near(model.facing, 0.6) and near(model.position[1], -(midX * c - midY * s) * fitted), "turns about its middle")
-- Drag to turn it (and it stops turning by itself), the wheel zooms, right-drag lifts.
model.scripts.OnMouseDown(model, "LeftButton")
cursorX = 100
PlayerHousingPreview.scripts.OnUpdate(PlayerHousingPreview, 0.01)
model.scripts.OnMouseUp(model)
assert(near(model.facing, 2.1), "dragged: " .. model.facing)
PlayerHousingPreview.scripts.OnUpdate(PlayerHousingPreview, 1)
assert(near(model.facing, 2.1), "no more turning by itself")
model.scripts.OnMouseWheel(model, 1)
assert(near(model.modelScale, fitted * 1.2), "zoomed")
model.scripts.OnMouseDown(model, "RightButton")
cursorY = 50
PlayerHousingPreview.scripts.OnUpdate(PlayerHousingPreview, 0.01)
model.scripts.OnMouseUp(model)
assert(near(model.position[3], -midZ * fitted * 1.2 + 0.5), "lifted: " .. model.position[3])
-- The other reading of the offsets, for clients that scale them.
SlashCmdList.PLAYERHOUSING("framing")
assert(PlayerHousingDB.framing == "model" and near(model.position[3], -midZ + 0.5), "framing: " .. model.position[3])
SlashCmdList.PLAYERHOUSING("framing")
assert(PlayerHousingDB.framing == nil)
chairSlot.scripts.OnLeave(chairSlot)
-- Another piece starts afresh.
barrelPieceSlot.scripts.OnEnter(barrelPieceSlot)
barrelPieceSlot.scripts.OnLeave(barrelPieceSlot)
chairSlot.scripts.OnEnter(chairSlot)
assert(near(model.modelScale, fitted) and near(model.facing, 0), "view reset for a new piece")
chairSlot.scripts.OnLeave(chairSlot)
assert(not PlayerHousingPreview:IsShown(), "preview hidden on leave")
-- Buildings made of world models get a floor plan, to scale, instead of a model.
PlayerHousing_Models[902200] = { false, 30, 10, 8 }
cartSlot.scripts.OnEnter(cartSlot)
assert(not PlayerHousingPreviewModel:IsShown(), "no model for a world model building")
assert(PlayerHousingPreviewPlan:IsShown(), "floor plan shown")
local rect = PlayerHousingPreviewPlanRect
assert(math.abs(rect.width / rect.height - 3) < 0.01, "30 by 10 yards drawn 3 to 1: " .. rect.width .. "x" .. rect.height)
assert(PlayerHousingPreviewPlanYou.width >= 10, "the person marker stays visible")
cartSlot.scripts.OnLeave(cartSlot)
-- Figurines show their creature.
PlayerHousing_Models[902200] = { "creature:10184", 0.9, 0.5, 0.4 }
cartSlot.scripts.OnEnter(cartSlot)
assert(PlayerHousingPreviewModel.creature == 10184 and PlayerHousingPreviewModel:IsShown(), "figurine preview")
cartSlot.scripts.OnLeave(cartSlot)
-- A model piece hides the floor plan again.
chairSlot.scripts.OnEnter(chairSlot)
assert(not PlayerHousingPreviewPlan:IsShown() and PlayerHousingPreviewModel:IsShown(), "model shown, no floor plan")
chairSlot.scripts.OnLeave(chairSlot)
assert(PlayerHousingFrame.height == 568, PlayerHousingFrame.height)

-- In my bags: only what's there to place.
PlayerHousingInBags.checked = true
PlayerHousingInBags.scripts.OnClick(PlayerHousingInBags)
assert(PlayerHousingCollectionSlot1.info[1] == 901105 and not PlayerHousingCollectionSlot2:IsShown(), "only the chair is counted in the bags")
PlayerHousingInBags.checked = false
PlayerHousingInBags.scripts.OnClick(PlayerHousingInBags)
-- Sorting: by name puts the Barrel before the chair.
PlayerHousingCollectionSort.scripts.OnClick()
assert(PlayerHousingCollectionSort.text == "Sort: name" and PlayerHousingCollectionSlot1.info[2] < "Westfall", PlayerHousingCollectionSlot1.info[2])
PlayerHousingCollectionSort.scripts.OnClick()
PlayerHousingCollectionSort.scripts.OnClick()
PlayerHousingCollectionSort.scripts.OnClick()
assert(PlayerHousingCollectionSort.text == "Sort: collection" and PlayerHousingCollectionSlot1.info[1] == 901105)

-- Toolbar buttons send .house commands.
local function last() return sent[#sent] end
PlayerHousingButton1.scripts.OnClick(); assert(last() == ".house leave", last())
PlayerHousingButton2.scripts.OnClick(); assert(last() == ".house edit")
PlayerHousingButton3.scripts.OnClick(); assert(last() == ".house undo")
PlayerHousingButton5.scripts.OnClick(); assert(last() == ".house", last())
assert(PlayerHousingButton2.text == "Edit", "edit button while decorating from the menus")
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
assert(#sent == before + 1 and last() == ".house shift 0.00 0.00 0.00 15", "first turn goes at once: " .. last())
PlayerHousingFrame.scripts.OnMouseWheel(PlayerHousingFrame, 1)
PlayerHousingFrame.scripts.OnMouseWheel(PlayerHousingFrame, 1)
OnUpdate(driver, 0.1)
assert(#sent == before + 1, "throttled")
OnUpdate(driver, 0.3)
assert(last() == ".house shift 0.00 0.00 0.00 30", last())
SEL("Turn right").scripts.OnClick()  -- Turn right
OnUpdate(driver, 0.5)
assert(last() == ".house shift 0.00 0.00 0.00 -15", last())
IsControlKeyDown = function() return true end
PlayerHousingFrame.scripts.OnMouseWheel(PlayerHousingFrame, 1)
OnUpdate(driver, 0.5)
assert(last() == ".house shift 0.00 0.00 0.10 0", last())
SEL("Turn left").scripts.OnClick()  -- Ctrl: Turn left 90
OnUpdate(driver, 0.5)
assert(last() == ".house shift 0.00 0.00 0.00 90", last())
IsControlKeyDown = function() return false end
IsShiftKeyDown = function() return true end
SEL("Turn right").scripts.OnClick()  -- Shift: Turn right 5
OnUpdate(driver, 0.5)
assert(last() == ".house shift 0.00 0.00 0.00 -5", last())
SEL("Bigger").scripts.OnClick(); assert(last() == ".house size normal", last())
SEL("Tilt L").scripts.OnClick(); assert(last() == ".house tilt straight", last())
IsShiftKeyDown = function() return false end
SEL("Bigger").scripts.OnClick(); assert(last() == ".house size bigger", last())
SEL("Smaller").scripts.OnClick(); assert(last() == ".house size smaller", last())
SEL("Tilt fwd").scripts.OnClick(); assert(last() == ".house tilt forward", last())
SEL("Tilt R").scripts.OnClick(); assert(last() == ".house tilt right", last())
SEL("Fwd").scripts.OnClick(); assert(last() == ".house nudge forward")

-- Buildings ask before being picked up.
SEL("Pick up").scripts.OnClick()
assert(popups[1][1] == "PLAYERHOUSING_PICKUP_BUILDING" and popups[1][2] == "Broken Cart")
StaticPopupDialogs.PLAYERHOUSING_PICKUP_BUILDING.OnAccept({}, 12); assert(last() == ".house pickup 12")
SEL("Pick up all").scripts.OnClick()
StaticPopupDialogs.PLAYERHOUSING_PICKUP_BUILDING_ALL.OnAccept({}, 12); assert(last() == ".house pickup 12 inside")

-- A furnishing picks up straight away.
fire("CHAT_MSG_ADDON", "HOUSING", "state\t1\t1\t13\tBarrel\t5\t200\t1\t10\tpicked up Broken Cart\tKrookowner\tplaced Barrel\t0", "WHISPER", "Krookowner")
assert(PlayerHousingButton4.enabled == true, "redo available")
SEL("Pick up").scripts.OnClick(); assert(last() == ".house pickup 13")

-- Moving: the piece follows you as a ghost, set down with G (below).
SEL("Move").scripts.OnClick(); assert(last() == ".house ghost move", last())
assert(not PlayerHousingSpotButton:IsShown(), "no spot button: no circle")

-- Setting down a saved set uses the targeting circle: the server hands over a Move a Piece
-- item and says which; a button uses it. The item never shows in the grid.
bags[0][6] = {901193, 1}
fire("BAG_UPDATE")
fire("CHAT_MSG_ADDON", "HOUSING", "state\t1\t1\t13\tBarrel\t5\t200\t1\t10\tpicked up Broken Cart\tKrookowner\t\t0\t901193", "WHISPER", "Krookowner")
assert(PlayerHousingSpotButton:IsShown() and PlayerHousingSpotButton.attrs.item == "0 6", "spot button uses the mover")
assert(PlayerHousingSpotButton.attrs.type == "item")
-- Set down: the item is used up and the server says so.
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

-- Another like this: the server sends a ghost of a new one after you.
SEL("Another").scripts.OnClick(); assert(last() == ".house another", last())

-- A ghost following you (fields 21 to 23): the same keys as edit mode drive it, even outside
-- edit mode, and the banner says what they do.
local GHOST = "state\t1\t1\t13\tBarrel\t5\t200\t1\t10\tmoved Barrel\tKrookowner\t\t0\t0\t0\t0\t0\t0\t0\t1\t902101\tnew\t1"
fire("CHAT_MSG_ADDON", "HOUSING", GHOST, "WHISPER", "Krookowner")
assert(overrides.G == "PlayerHousingEditFollow" and overrides["SHIFT-G"] == "PlayerHousingEditFollow" and overrides.ESCAPE == "PlayerHousingEditDone",
  "ghost keys bound outside edit mode")
assert(PlayerHousingEditHud:IsShown() and PlayerHousingEditHudTitle.text == "Placing" and PlayerHousingEditHudName.text == "Barrel",
  tostring(PlayerHousingEditHudTitle.text) .. " " .. tostring(PlayerHousingEditHudName.text))
assert(PlayerHousingEditHudSetDown:IsShown() and PlayerHousingEditHudAnother:IsShown() and PlayerHousingEditHudCancel:IsShown()
  and not PlayerHousingEditHudRow:IsShown() and not PlayerHousingEditHudUndo:IsShown(), "the ghost's buttons, not edit mode's")
-- Steps add up and go out together, a few times a second, like edit mode's.
local function step() now = now + 1; OnUpdate(driver, 1) end
PlayerHousingEditForward.scripts.OnClick(PlayerHousingEditForward, "LeftButton", true)
PlayerHousingEditForward.scripts.OnClick(PlayerHousingEditForward, "LeftButton", false)
step()
assert(last() == ".house ghost adjust 0.25 0.00 0.00 0", last())
PlayerHousingEditLeft.scripts.OnClick(PlayerHousingEditLeft, "LeftButton", true)
PlayerHousingEditLeft.scripts.OnClick(PlayerHousingEditLeft, "LeftButton", false)
PlayerHousingEditWheelUp.scripts.OnClick(PlayerHousingEditWheelUp, "LeftButton", true)
PlayerHousingEditWheelUp.scripts.OnClick(PlayerHousingEditWheelUp, "LeftButton", true)
step()
assert(last() == ".house ghost adjust 0.00 0.25 0.00 30", "two wheel notches and a step, in one: " .. last())
PlayerHousingEditRaiseWheelDown.scripts.OnClick(PlayerHousingEditRaiseWheelDown, "LeftButton", true)
step()
assert(last() == ".house ghost adjust 0.00 0.00 -0.10 0", last())
PlayerHousingEditPickUp.scripts.OnClick(PlayerHousingEditPickUp, "LeftButton", true)
step()
assert(last() == ".house ghost adjust 0.00 0.00 -0.10 0", "Delete waits while a piece follows you: " .. last())
PlayerHousingEditFollow.scripts.OnClick(PlayerHousingEditFollow, "LeftButton", true)
assert(last() == ".house ghost place", last())
IsShiftKeyDown = function() return true end
PlayerHousingEditFollow.scripts.OnClick(PlayerHousingEditFollow, "LeftButton", true)
assert(last() == ".house ghost place another", last())
IsShiftKeyDown = function() return false end
PlayerHousingEditDone.scripts.OnClick(PlayerHousingEditDone, "LeftButton", true)
assert(last() == ".house ghost cancel", last())
PlayerHousingEditHudSetDown.scripts.OnClick(PlayerHousingEditHudSetDown)
assert(last() == ".house ghost place", last())
-- Moving pieces: no "and another".
fire("CHAT_MSG_ADDON", "HOUSING", GHOST:gsub("\tnew\t", "\tmove\t"), "WHISPER", "Krookowner")
assert(PlayerHousingEditHudTitle.text == "Moving" and not PlayerHousingEditHudAnother:IsShown(), "moving: no another")
IsShiftKeyDown = function() return true end
PlayerHousingEditFollow.scripts.OnClick(PlayerHousingEditFollow, "LeftButton", true)
assert(last() == ".house ghost place", "Shift+G sets a moved piece down: " .. last())
IsShiftKeyDown = function() return false end
-- Set down: the keys and the banner go.
fire("CHAT_MSG_ADDON", "HOUSING", "state\t1\t1\t13\tBarrel\t5\t200\t1\t10\tmoved Barrel\tKrookowner\t\t0\t0\t0\t0\t0\t0\t0\t1\t0\t\t1", "WHISPER", "Krookowner")
assert(next(overrides) == nil and not PlayerHousingEditHud:IsShown(), "keys and banner gone after the ghost")

-- Edit mode: the server says so, and the keys come on.
local function click(name, down) _G[name].scripts.OnClick(_G[name], "LeftButton", down) end
local function flush() now = now + 1; OnUpdate(driver, 1) end
local EDIT = "state\t1\t1\t13\tBarrel\t5\t200\t1\t10\tmoved Barrel\tKrookowner\t\t0\t0\t0\t0\t1\t0"
fire("CHAT_MSG_ADDON", "HOUSING", EDIT, "WHISPER", "Krookowner")
assert(overrides.UP == "PlayerHousingEditForward" and overrides["SHIFT-TAB"] == "PlayerHousingEditNext", "keys bound")
assert(overrides.G == "PlayerHousingEditFollow" and overrides.ESCAPE == "PlayerHousingEditDone")
assert(PlayerHousingEditHud:IsShown() and PlayerHousingEditHudName.text == "Barrel", "edit mode banner")
assert(PlayerHousingButton2.text == "Done", "edit button shows Done in edit mode")
flush()
local count = #sent
click("PlayerHousingEditForward", true); click("PlayerHousingEditForward", false)
click("PlayerHousingEditLeft", true); click("PlayerHousingEditLeft", false)
flush()
assert(#sent == count + 1 and last() == ".house shift 0.25 0.25 0.00 0", "presses add up: " .. last())
-- Holding a key repeats it after a moment, and stops on release.
click("PlayerHousingEditRaise", true)
now = now + 0.2; PlayerHousingEditKeys.scripts.OnUpdate(PlayerHousingEditKeys, 0.2)
now = now + 0.2; PlayerHousingEditKeys.scripts.OnUpdate(PlayerHousingEditKeys, 0.2)
now = now + 0.1; PlayerHousingEditKeys.scripts.OnUpdate(PlayerHousingEditKeys, 0.1)
click("PlayerHousingEditRaise", false)
now = now + 1; PlayerHousingEditKeys.scripts.OnUpdate(PlayerHousingEditKeys, 1)
OnUpdate(driver, 1)
assert(last() == ".house shift 0.00 0.00 0.30 0", "held: " .. last())
-- A key left down (the release never came) gives up after a few seconds.
click("PlayerHousingEditBack", true)
for _ = 1, 200 do now = now + 0.1; PlayerHousingEditKeys.scripts.OnUpdate(PlayerHousingEditKeys, 0.1) end
OnUpdate(driver, 1)
local steps = -tonumber(last():match("shift (%S+)")) / 0.25
assert(steps > 30 and steps < 70, "stuck key stops: " .. steps)
now = now + 1; PlayerHousingEditKeys.scripts.OnUpdate(PlayerHousingEditKeys, 1); OnUpdate(driver, 1)
count = #sent; flush(); assert(#sent == count, "nothing more after it gave up")
-- Shift is finer; the grid sets the step.
IsShiftKeyDown = function() return true end
click("PlayerHousingEditRight", true); click("PlayerHousingEditRight", false)
click("PlayerHousingEditWheelUp", true)
flush(); assert(last() == ".house shift 0.00 -0.05 0.00 5", last())
click("PlayerHousingEditNext", true); assert(last() == ".house select previous", last())
IsShiftKeyDown = function() return false end
fire("CHAT_MSG_ADDON", "HOUSING", EDIT:gsub("\t0$", "\t0.5"), "WHISPER", "Krookowner")
assert(PlayerHousingEditHudGrid.text == "Grid: 0.5 yd", "grid shown: " .. tostring(PlayerHousingEditHudGrid.text))
click("PlayerHousingEditForward", true); click("PlayerHousingEditForward", false)
flush(); assert(last() == ".house shift 0.50 0.00 0.00 0", last())
fire("CHAT_MSG_ADDON", "HOUSING", EDIT, "WHISPER", "Krookowner")
-- The wheel: turn, Ctrl raises, Alt zooms.
click("PlayerHousingEditWheelDown", true); flush(); assert(last() == ".house shift 0.00 0.00 0.00 -15", last())
IsControlKeyDown = function() return true end
click("PlayerHousingEditWheelUp", true); flush(); assert(last() == ".house shift 0.00 0.00 0.10 0", last())
IsControlKeyDown = function() return false end
IsAltKeyDown = function() return true end
count = #sent
click("PlayerHousingEditWheelUp", true); flush(); assert(#sent == count and zoomed == 1, "alt+wheel zooms")
IsAltKeyDown = function() return false end
-- Each wheel binding does its own thing, whether or not Ctrl reads as held, and the banner
-- says what happened.
assert(overrides["CTRL-MOUSEWHEELUP"] == "PlayerHousingEditRaiseWheelUp" and overrides["ALT-MOUSEWHEELDOWN"] == "PlayerHousingEditZoomWheelDown")
assert(overrides["SHIFT-MOUSEWHEELUP"] == "PlayerHousingEditWheelUp" and overrides.R == "PlayerHousingEditWheelMode")
click("PlayerHousingEditRaiseWheelUp", true); flush(); assert(last() == ".house shift 0.00 0.00 0.10 0", last())
assert(PlayerHousingEditHudLast.text:find("Ctrl%+Wheel up") and PlayerHousingEditHudLast.text:find("raised 0.1 yd"), PlayerHousingEditHudLast.text)
click("PlayerHousingEditWheelDown", true); flush()
assert(PlayerHousingEditHudLast.text:find("turned right 15 degrees"), PlayerHousingEditHudLast.text)
IsControlKeyDown = function() return true end
click("PlayerHousingEditWheelDown", true); flush(); assert(last() == ".house shift 0.00 0.00 -0.10 0", last())
assert(PlayerHousingEditHudLast.text:find("Ctrl held"), PlayerHousingEditHudLast.text)
IsControlKeyDown = function() return false end
click("PlayerHousingEditForward", true); click("PlayerHousingEditForward", false); flush()
assert(PlayerHousingEditHudLast.text:find("Up arrow") and PlayerHousingEditHudLast.text:find("moved forward 0.25 yd"), PlayerHousingEditHudLast.text)
-- R: the plain wheel raises instead, and back.
click("PlayerHousingEditWheelMode", true)
assert(PlayerHousingEditHudWheel.text == "Wheel: raise" and PlayerHousingEditHudHelp.text:find("R: wheel turns"))
click("PlayerHousingEditWheelDown", true); flush(); assert(last() == ".house shift 0.00 0.00 -0.10 0", last())
PlayerHousingEditHudWheel.scripts.OnClick()
assert(PlayerHousingEditHudWheel.text == "Wheel: turn")
click("PlayerHousingEditWheelUp", true); flush(); assert(last() == ".house shift 0.00 0.00 0.00 15", last())
-- The line goes after a few seconds.
now = now + 7; PlayerHousingEditKeys.scripts.OnUpdate(PlayerHousingEditKeys, 7)
assert(PlayerHousingEditHudLast.text == "", "feedback fades")
-- The banner's buttons.
assert(PlayerHousingEditHudUndo.enabled == true and PlayerHousingEditHudRedo.enabled == false, "undo, no redo")
PlayerHousingEditHudGrid.scripts.OnClick(); assert(last() == ".house grid 0.25", last())
fire("CHAT_MSG_ADDON", "HOUSING", EDIT:gsub("\t0$", "\t0.5"), "WHISPER", "Krookowner")
PlayerHousingEditHudGrid.scripts.OnClick(); assert(last() == ".house grid 1", last())
fire("CHAT_MSG_ADDON", "HOUSING", EDIT:gsub("\t0$", "\t2"), "WHISPER", "Krookowner")
PlayerHousingEditHudGrid.scripts.OnClick(); assert(last() == ".house grid off", last())
fire("CHAT_MSG_ADDON", "HOUSING", EDIT, "WHISPER", "Krookowner")
PlayerHousingEditHudUndo.scripts.OnClick(); assert(last() == ".house undo", last())
PlayerHousingEditHudDone.scripts.OnClick(); assert(last() == ".house edit off", last())
-- The other keys.
click("PlayerHousingEditNext", true); assert(last() == ".house select next", last())
click("PlayerHousingEditPickUp", true); assert(last() == ".house pickup 13", last())
click("PlayerHousingEditUndo", true); assert(last() == ".house undo", last())
click("PlayerHousingEditRedo", true); assert(last() == ".house redo", last())
-- G picks the selected piece up: it follows you until G again sets it down.
click("PlayerHousingEditFollow", true)
assert(last() == ".house ghost move", last())
assert(PlayerHousingEditHudLast.text:find("G again"), PlayerHousingEditHudLast.text)
-- Nothing selected: G says so.
fire("CHAT_MSG_ADDON", "HOUSING", "state\t1\t1\t0\t\t5\t200\t1\t10\tmoved Barrel\tKrookowner\t\t0\t0\t0\t0\t1\t0", "WHISPER", "Krookowner")
count = #sent
click("PlayerHousingEditFollow", true)
assert(#sent == count and PlayerHousingEditHudLast.text:find("select a piece first"), PlayerHousingEditHudLast.text)
fire("CHAT_MSG_ADDON", "HOUSING", EDIT, "WHISPER", "Krookowner")
flush()
-- Escape cancels the targeting circle first, then leaves edit mode.
targeting = true
count = #sent
click("PlayerHousingEditDone", true); assert(stoppedTargeting == 1 and #sent == count, "escape cancels the circle")
click("PlayerHousingEditDone", true); assert(last() == ".house edit off", last())
-- Nothing selected: the wheel zooms.
fire("CHAT_MSG_ADDON", "HOUSING", "state\t1\t1\t0\t\t5\t200\t1\t10\t\tKrookowner\t\t0\t0\t0\t0\t1\t0", "WHISPER", "Krookowner")
assert(PlayerHousingEditHudName.text:find("Tab"), "nothing selected yet")
click("PlayerHousingEditWheelDown", true); assert(zoomed == 0, "wheel zooms with nothing selected")
-- Edit mode ending in combat keeps the keys until combat is over.
combat = true
fire("CHAT_MSG_ADDON", "HOUSING", EDIT:gsub("\t1\t0$", "\t0\t0"), "WHISPER", "Krookowner")
assert(overrides.UP and not PlayerHousingEditHud:IsShown(), "keys stay in combat, banner goes")
combat = false
PlayerHousingEditKeys.scripts.OnEvent(PlayerHousingEditKeys, "PLAYER_REGEN_ENABLED")
assert(next(overrides) == nil, "keys cleared after combat")
fire("CHAT_MSG_ADDON", "HOUSING", "state\t1\t1\t13\tBarrel\t5\t200\t1\t10\tmoved Barrel\tKrookowner\t\t0\t0", "WHISPER", "Krookowner")

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

-- The window's tabs. The addon told the server it's here, once.
local registrations = 0
for _, command in ipairs(sent) do if command == ".house addon 1 1" then registrations = registrations + 1 end end
assert(registrations == 1, "registered once: " .. registrations)
local function msg(text) fire("CHAT_MSG_ADDON", "HOUSING", text, "WHISPER", "Krookowner") end
local function rows(kind, list, arg)
  msg("begin\t" .. kind .. (arg and ("\t" .. arg) or ""))
  for _, row in ipairs(list) do msg("row\t" .. kind .. "\t" .. row) end
  msg("end\t" .. kind .. (arg and ("\t" .. arg) or ""))
end
fire("CHAT_MSG_ADDON", "HOUSING", "state\t1\t1\t13\tBarrel\t5\t200\t1\t10\tmoved Barrel\tKrookowner\t\t0\t0", "WHISPER", "Krookowner")
PlayerHousingFrame.shown = false
msg("open")
assert(PlayerHousingFrame:IsShown(), "the House Key opens the window")
assert(PlayerHousingCollectionPanel:IsShown(), "the Collection first")

-- Collection: unlocked pieces in color, locked ones grey; a click shows one (or places it).
PlayerHousingTabCollection.scripts.OnClick()
assert(last() == ".house data collection", last())
assert(PlayerHousingCollectionPanel:IsShown() and PlayerHousingTabCollection.highlighted, "Collection tab shown")
rows("collection", { "settings\t0\t0\t200\t10\t0", "unlocked\t901104-901106", "new\t901106", "storage\t901105:2,902200:1",
                     "placed\t901105:1", "recent\t901106,901105" })
local first = PlayerHousingCollectionSlot1
assert(first.info[1] == 901105 and first:IsShown() and not first.icon.desaturated, "the chair: unlocked")
assert(first.count.text == 5, "five in the bags: " .. tostring(first.count.text))
assert(PlayerHousingCollectionSlot2.new.text == "New", "the table is new")
local lockedSlot
for index = 1, 36 do
  local slot = _G["PlayerHousingCollectionSlot" .. index]
  if slot.info and slot.info[1] ~= 901104 and slot.info[1] ~= 901105 and slot.info[1] ~= 901106 then lockedSlot = slot break end
end
assert(lockedSlot and lockedSlot.icon.desaturated, "locked pieces are grey")
assert(PlayerHousingCollectionCount.text:find("^3 of "), PlayerHousingCollectionCount.text)
-- A click pins the piece next to the window, with its details and buttons, and a ghost of it
-- follows you on the island (the server takes one from the bags or storage, or a new copy).
first:Click("LeftButton")
assert(last() == ".house ghost 901105", last())
assert(PlayerHousingPreview:IsShown() and PlayerHousingPreview.height == 360, "pinned: " .. tostring(PlayerHousingPreview.height))
assert(PlayerHousingDetailsText.text:find("Unlocked"), PlayerHousingDetailsText.text)
assert(PlayerHousingDetailsCounts.text == "Bags: 5   Storage: 2   Placed: 1", PlayerHousingDetailsCounts.text)
assert(PlayerHousingDetailsPlace:IsShown() and PlayerHousingDetailsPlace.enabled and not PlayerHousingDetailsPlace.protected, "Place: a plain button")
assert(PlayerHousingDetailsTake.enabled and PlayerHousingDetailsGetOne.enabled)
PlayerHousingDetailsGetOne.scripts.OnClick()
assert(sent[#sent - 1] == ".house get 901105 1" and last() == ".house data collection", sent[#sent - 1])
PlayerHousingDetailsGetFive.scripts.OnClick()
assert(sent[#sent - 1] == ".house get 901105 5", sent[#sent - 1])
PlayerHousingDetailsTake.scripts.OnClick()
assert(sent[#sent - 1] == ".house take 901105", sent[#sent - 1])
PlayerHousingDetailsPlace:Click()
assert(last() == ".house ghost 901105", "Place sends a ghost too: " .. last())
-- Hovering another piece shows it for a moment; moving off brings the pinned one back.
lockedSlot.scripts.OnEnter(lockedSlot)
assert(PlayerHousingPreviewName.text == lockedSlot.info[2] and not PlayerHousingDetailsPlace:IsShown(), "hovered piece, no buttons")
assert(PlayerHousingPreviewHint.text ~= "", "says how to show it instead")
lockedSlot.scripts.OnLeave(lockedSlot)
assert(PlayerHousingPreview:IsShown() and PlayerHousingPreviewName.text == first.info[2] and PlayerHousingDetailsPlace:IsShown(),
  "back to the pinned piece: " .. tostring(PlayerHousingPreviewName.text))
-- None to hand: a click only shows it (no copy bought by looking); Place asks for one. In
-- combat too: nothing secure.
local barrelSlot
for index = 1, 36 do
  local slot = _G["PlayerHousingCollectionSlot" .. index]
  if slot.info and slot.info[1] == 901106 then barrelSlot = slot end
end
combat = true
count = #sent
barrelSlot:Click("LeftButton")
assert(#sent == count and PlayerHousingDetailsPlace.enabled and PlayerHousingDetailsPlace:IsShown(), "only shown: " .. last())
PlayerHousingDetailsPlace:Click()
assert(last() == ".house ghost 901106", last())
first:Click("LeftButton")
assert(last() == ".house ghost 901105", "in combat too: " .. last())
combat = false
-- Off the island, a click only shows it.
fire("CHAT_MSG_ADDON", "HOUSING", "state\t0\t0\t0\t\t0\t200\t0\t10\t\t\t\t0", "WHISPER", "Krookowner")
count = #sent
first:Click("LeftButton")
assert(#sent == count and not PlayerHousingDetailsPlace.enabled, "off the island: nothing sent, Place off")
fire("CHAT_MSG_ADDON", "HOUSING", "state\t1\t1\t13\tBarrel\t5\t200\t1\t10\tmoved Barrel\tKrookowner\t\t0\t0", "WHISPER", "Krookowner")
-- A locked piece says how to unlock it, and can't be had yet.
count = #sent
lockedSlot:Click("LeftButton")
assert(#sent == count, "locked: nothing sent")
assert(PlayerHousingDetailsText.text:find("Locked") and not PlayerHousingDetailsGetOne.enabled, PlayerHousingDetailsText.text)
assert(not PlayerHousingDetailsPlace.enabled, "locked, none in the bags: nothing to place")
-- Close unpins.
PlayerHousingPreviewClose.scripts.OnClick()
assert(not PlayerHousingPreview:IsShown() and not PlayerHousingDetailsPlace:IsShown(), "closed")
first.scripts.OnEnter(first)
assert(PlayerHousingPreview:IsShown() and PlayerHousingPreview.height == 268, "preview while hovering")
first.scripts.OnLeave(first)
assert(not PlayerHousingPreview:IsShown(), "nothing pinned: hidden again")
-- So does the window closing.
first:Click("LeftButton")
PlayerHousingFrame:Hide()
assert(not PlayerHousingPreview:IsShown(), "unpinned with the window")
PlayerHousingFrame:Show()
first.scripts.OnEnter(first)
first.scripts.OnLeave(first)
assert(not PlayerHousingPreview:IsShown(), "stays unpinned")
-- Filters: unlocked only, a category, search.
PlayerHousingUnlockedOnly.checked = true
PlayerHousingUnlockedOnly.scripts.OnClick(PlayerHousingUnlockedOnly)
assert(PlayerHousingCollectionSlot3:IsShown() and not PlayerHousingCollectionSlot4:IsShown(), "three unlocked")
PlayerHousingUnlockedOnly.checked = false
PlayerHousingUnlockedOnly.scripts.OnClick(PlayerHousingUnlockedOnly)
-- Favorites: right-click stars a piece, and they have their own entry after All.
PlayerHousingCategoryNext.scripts.OnClick()
assert(PlayerHousingCategoryText.text == "Favorites" and not PlayerHousingCollectionSlot1:IsShown(), PlayerHousingCategoryText.text)
assert(PlayerHousingCollectionStatus.text:find("right%-click a piece"), PlayerHousingCollectionStatus.text)
PlayerHousingCategoryPrev.scripts.OnClick()
first:Click("RightButton")
assert(first.star:IsShown() and PlayerHousingDB.favorites[901105], "starred")
assert(not PlayerHousingPreview:IsShown() or PlayerHousingPreviewName.text ~= "", "right-click doesn't pin")
PlayerHousingCategoryNext.scripts.OnClick()
assert(PlayerHousingCollectionSlot1.info[1] == 901105 and not PlayerHousingCollectionSlot2:IsShown(), "only the favorite")
PlayerHousingCollectionSlot1:Click("RightButton")
assert(not PlayerHousingCollectionSlot1:IsShown() and PlayerHousingDB.favorites[901105] == nil, "unstarred")
PlayerHousingCategoryPrev.scripts.OnClick()
PlayerHousingCategoryNext.scripts.OnClick()
-- Recently placed, newest first.
PlayerHousingCategoryNext.scripts.OnClick()
assert(PlayerHousingCategoryText.text == "Recently placed" and PlayerHousingCollectionSlot1.info[1] == 901106
  and PlayerHousingCollectionSlot2.info[1] == 901105 and not PlayerHousingCollectionSlot3:IsShown(), PlayerHousingCategoryText.text)
PlayerHousingCategoryNext.scripts.OnClick()
assert(PlayerHousingCategoryText.text == "Starter", PlayerHousingCategoryText.text)
for _ = 1, 4 do PlayerHousingCategoryPrev.scripts.OnClick() end
assert(PlayerHousingCategoryText.text == "Figurines", "the Catalog is skipped when the server doesn't offer it: " .. PlayerHousingCategoryText.text)
PlayerHousingCategoryNext.scripts.OnClick()
PlayerHousingCollectionSearch.text = "westfall chair"
PlayerHousingCollectionSearch.scripts.OnTextChanged(PlayerHousingCollectionSearch)
assert(PlayerHousingCollectionSlot1.info[1] == 901105 and not PlayerHousingCollectionSlot2:IsShown(), "search")
PlayerHousingCollectionSearch.text = ""
PlayerHousingCollectionSearch.scripts.OnTextChanged(PlayerHousingCollectionSearch)

-- Storage: leaving the Collection marks the new pieces seen.
PlayerHousingTabStorage.scripts.OnClick()
assert(sent[#sent - 1] == ".house seen" and last() == ".house data collection", sent[#sent - 1])
assert(PlayerHousingStoragePanelRow1:IsShown() and PlayerHousingStoragePanelRow1Text.text == "Broken Cart", PlayerHousingStoragePanelRow1Text.text)
assert(PlayerHousingStoragePanelRow2Text.text == "Westfall Chair x2", PlayerHousingStoragePanelRow2Text.text)
PlayerHousingStoragePanelRow2Button2.scripts.OnClick()
assert(sent[#sent - 1] == ".house take 901105", sent[#sent - 1])
-- Place: a ghost of one follows you (the server takes it out of storage when it's set down).
PlayerHousingStoragePanelRow2Button1.scripts.OnClick()
assert(last() == ".house ghost 901105" and PlayerHousingPreview:IsShown() and PlayerHousingPreviewName.text == "Westfall Chair", last())
PlayerHousingPreviewClose.scripts.OnClick()
PlayerHousingTakeAll.scripts.OnClick()
assert(sent[#sent - 1] == ".house take all", sent[#sent - 1])
rows("collection", { "settings\t0\t0\t200\t10\t0" })
assert(not PlayerHousingStoragePanelRow1:IsShown() and PlayerHousingStoragePanelEmpty.text ~= "", "empty storage")

-- Placed: nearest first; buildings ask before being picked up.
PlayerHousingTabPlaced.scripts.OnClick()
assert(last() == ".house data placed", last())
rows("placed", { "3\t901105\t2.5", "7\t902200\t9.1" })
assert(PlayerHousingPlacedPanelRow1Text.text:find("Westfall Chair") and PlayerHousingPlacedPanelRow2Text.text:find("Broken Cart"))
PlayerHousingPlacedPanelRow1Button1.scripts.OnClick(); assert(last() == ".house goto 3", last())
PlayerHousingPlacedPanelRow1Button2.scripts.OnClick(); assert(last() == ".house select 3", last())
PlayerHousingPlacedPanelRow1Button3.scripts.OnClick(); assert(last() == ".house group add 3", last())
PlayerHousingPlacedPanelRow1Button4.scripts.OnClick(); assert(sent[#sent - 1] == ".house here 3", sent[#sent - 1])
PlayerHousingPlacedPanelRow1Button5.scripts.OnClick(); assert(last() == ".house pickup 3", last())
local popupCount = #popups
PlayerHousingPlacedPanelRow2Button5.scripts.OnClick()
assert(#popups == popupCount + 1 and popups[#popups][2] == "Broken Cart", "the building asks first")
-- Find a piece by name.
PlayerHousingPlacedSearch.text = "cart"
PlayerHousingPlacedSearch.scripts.OnTextChanged(PlayerHousingPlacedSearch)
assert(PlayerHousingPlacedPanelRow1Text.text:find("Broken Cart") and not PlayerHousingPlacedPanelRow2:IsShown(), "search the placed pieces")
PlayerHousingPlacedSearch.text = ""
PlayerHousingPlacedSearch.scripts.OnTextChanged(PlayerHousingPlacedSearch)
-- Placing or picking up elsewhere refreshes the list.
fire("CHAT_MSG_ADDON", "HOUSING", "state\t1\t1\t13\tBarrel\t4\t200\t1\t10\tpicked up Barrel\tKrookowner\t\t0\t0", "WHISPER", "Krookowner")
assert(last() == ".house data placed", "placed list follows the counts: " .. last())

-- Layouts.
PlayerHousingTabLayouts.scripts.OnClick()
assert(last() == ".house data layouts", last())
rows("layouts", { "limit\t5", "layout\t3\tSummer house\t12\t2026-09-28\t" })
assert(PlayerHousingLayoutsPanelRow1Text.text:find("Summer house") and PlayerHousingLayoutsPanelRow1Text.text:find("12 pieces"))
PlayerHousingLayoutsPanelRow1Button1.scripts.OnClick()
StaticPopupDialogs.PLAYERHOUSING_LAYOUT_LOAD.OnAccept({}, 3)
assert(sent[#sent - 1] == ".house layout load 3", sent[#sent - 1])
StaticPopupDialogs.PLAYERHOUSING_LAYOUT_SEND.OnAccept({ editBox = { GetText = function() return " Krookfriend " end } }, 3)
assert(last() == ".house layout send 3 Krookfriend", last())
StaticPopupDialogs.PLAYERHOUSING_LAYOUT_DELETE.OnAccept({}, 3)
assert(sent[#sent - 1] == ".house layout delete 3", sent[#sent - 1])
PlayerHousingLayoutName.text = "Winter house"
PlayerHousingLayoutSave.scripts.OnClick()
assert(sent[#sent - 1] == ".house layout save Winter house", sent[#sent - 1])

-- Guests.
PlayerHousingTabGuests.scripts.OnClick()
rows("guests", { "guest\tKrookfriend\t0", "guest\tKrookroomie\t1" })
assert(PlayerHousingGuestsPanelRow2Text.text:find("roommate") and PlayerHousingGuestsPanelRow2Button1.text == "Guest")
PlayerHousingGuestsPanelRow1Button1.scripts.OnClick(); assert(sent[#sent - 1] == ".house roommate Krookfriend", sent[#sent - 1])
PlayerHousingGuestsPanelRow2Button1.scripts.OnClick(); assert(sent[#sent - 1] == ".house unroommate Krookroomie", sent[#sent - 1])
PlayerHousingGuestsPanelRow1Button2.scripts.OnClick(); assert(sent[#sent - 1] == ".house uninvite Krookfriend", sent[#sent - 1])
PlayerHousingGuestName.text = "Newguest"
PlayerHousingInvite.scripts.OnClick(); assert(sent[#sent - 1] == ".house invite Newguest", sent[#sent - 1])
PlayerHousingInviteParty.scripts.OnClick(); assert(sent[#sent - 1] == ".house invite party", sent[#sent - 1])

-- Visit.
PlayerHousingTabVisit.scripts.OnClick()
assert(last() == ".house data visits 0", last())
PlayerHousingVisitList6.scripts.OnClick()
assert(last() == ".house data visits 5", last())
rows("visits", { "island\tKrookfriend\t0" }, "0")
assert(not PlayerHousingVisitPanelRow1:IsShown(), "an old list's answer is ignored")
rows("visits", { "island\tKrookfriend\t4", "island\tKrookother\t1" }, "5")
assert(PlayerHousingVisitPanelRow1Text.text:find("4 likes") and PlayerHousingVisitPanelRow2Text.text:find("1 like%)"))
PlayerHousingVisitPanelRow1Button1.scripts.OnClick(); assert(last() == ".house visit Krookfriend", last())
PlayerHousingVisitName.text = "Somebody"
PlayerHousingVisitGo.scripts.OnClick(); assert(last() == ".house visit Somebody", last())

-- Island.
PlayerHousingTabIsland.scripts.OnClick()
assert(last() == ".house data island", last())
rows("island", { "settings\t2\t0\t0\t0\t1\t7\t3", "greeting\tWelcome!", "weather\t0\tclear", "weather\t1\tfog",
                 "weather\t2\train", "time\t0\tserver clock", "time\t1\tnight", "music\t12816\tGrizzly Hills" })
assert(PlayerHousingPrivacy3.highlighted and not PlayerHousingPrivacy1.highlighted, "public")
assert(PlayerHousingWeatherText.text == "clear" and PlayerHousingTimeText.text == "server clock" and PlayerHousingMusicText.text == "none")
assert(PlayerHousingGreeting.text == "Welcome!")
PlayerHousingWeatherNext.scripts.OnClick(); assert(sent[#sent - 1] == ".house weather 1", sent[#sent - 1])
PlayerHousingWeatherPrev.scripts.OnClick(); assert(sent[#sent - 1] == ".house weather 2", "wraps around: " .. sent[#sent - 1])
PlayerHousingTimeNext.scripts.OnClick(); assert(sent[#sent - 1] == ".house time 1", sent[#sent - 1])
PlayerHousingMusicNext.scripts.OnClick(); assert(sent[#sent - 1] == ".house music 12816", sent[#sent - 1])
PlayerHousingPrivacy2.scripts.OnClick(); assert(sent[#sent - 1] == ".house privacy friends", sent[#sent - 1])
PlayerHousingGreeting.text = "Hello there"
PlayerHousingGreetingSet.scripts.OnClick(); assert(sent[#sent - 1] == ".house greeting Hello there", sent[#sent - 1])
rows("island", { "settings\t2\t0\t0\t12816\t1\t7\t3", "music\t12816\tGrizzly Hills" })
assert(PlayerHousingMusicText.text == "Grizzly Hills")
PlayerHousingMusicNext.scripts.OnClick(); assert(sent[#sent - 1] == ".house music off", sent[#sent - 1])

-- The door, and the guestbook's new notes.
rows("island", { "settings\t2\t0\t0\t0\t1\t7\t3\t1\t2" })
assert(PlayerHousingIslandPanel:GetName() and PlayerHousingGuestbookButton.text == "Guestbook (2 new)", tostring(PlayerHousingGuestbookButton.text))
PlayerHousingDoorHere.scripts.OnClick(); assert(sent[#sent - 1] == ".house door here", sent[#sent - 1])
PlayerHousingDoorReset.scripts.OnClick(); assert(sent[#sent - 1] == ".house door reset", sent[#sent - 1])
PlayerHousingGuestbookButton.scripts.OnClick()
assert(PlayerHousingGuestsPanel:IsShown() and last() == ".house data guestbook", "the guestbook from the Island tab: " .. last())

-- The guestbook: the notes, a new one marked, the whole note on hover, thrown out.
rows("guestbook", { "note\t7\tKrookfriend\t2026-09-29 10:15\t1\tWhat a lovely island!", "note\t5\tKrookother\t2026-09-20 18:02\t0\tNice." })
assert(PlayerHousingGuestbookPageRow1Text.text:find("new") and PlayerHousingGuestbookPageRow1Text.text:find("lovely island")
  and not PlayerHousingGuestsPanelRow1:IsShown(), PlayerHousingGuestbookPageRow1Text.text)
PlayerHousingGuestbookPageRow1Button1.scripts.OnClick()
StaticPopupDialogs.PLAYERHOUSING_NOTE_DELETE.OnAccept({}, 7)
assert(sent[#sent - 1] == ".house guestbook delete 7" and last() == ".house data guestbook", sent[#sent - 1])
PlayerHousingGuestsPage1.scripts.OnClick()
assert(last() == ".house data guests" and not PlayerHousingGuestbookPageRow1:IsShown(), "back to the guests")

-- Sets: the Layouts tab's second page.
PlayerHousingTabLayouts.scripts.OnClick()
PlayerHousingLayoutsPage2.scripts.OnClick()
assert(last() == ".house data sets" and PlayerHousingLayoutSave.text == "Save selection", last())
rows("sets", { "limit\t20", "set\t2\tDining\t3\t2026-09-29" })
assert(PlayerHousingSetsPageRow1Text.text:find("Dining") and PlayerHousingSetsPageRow1Text.text:find("3 pieces")
  and not PlayerHousingLayoutsPanelRow1:IsShown(), PlayerHousingSetsPageRow1Text.text)
PlayerHousingSetsPageRow1Button1.scripts.OnClick(); assert(last() == ".house set place 2", last())
StaticPopupDialogs.PLAYERHOUSING_SET_DELETE.OnAccept({}, 2)
assert(sent[#sent - 1] == ".house set delete 2", sent[#sent - 1])
PlayerHousingLayoutName.text = "Reading nook"
PlayerHousingLayoutSave.scripts.OnClick()
assert(sent[#sent - 1] == ".house set save Reading nook", sent[#sent - 1])
PlayerHousingLayoutsPage1.scripts.OnClick()
assert(last() == ".house data layouts" and PlayerHousingLayoutSave.text == "Save as new", last())

-- Visiting: sign the island's guestbook from the Visit tab.
fire("CHAT_MSG_ADDON", "HOUSING", "state\t0\t0\t0\t\t0\t200\t0\t10\t\tKrookfriend\t\t0\t0\t0\t0", "WHISPER", "Krookowner")
PlayerHousingTabVisit.scripts.OnClick()
assert(PlayerHousingSign:IsShown() and PlayerHousingLike:IsShown(), "signing while visiting")
PlayerHousingSignNote.text = "Lovely!"
PlayerHousingSign.scripts.OnClick(); assert(last() == ".house sign Lovely!", last())
fire("CHAT_MSG_ADDON", "HOUSING", "state\t1\t1\t13\tBarrel\t5\t200\t1\t10\tmoved Barrel\tKrookowner\t\t0\t0", "WHISPER", "Krookowner")
assert(not PlayerHousingSign:IsShown(), "not on your own island")

-- Tabs don't change in combat.
PlayerHousingTabIsland.scripts.OnClick()
combat = true
PlayerHousingTabCollection.scripts.OnClick()
assert(PlayerHousingIslandPanel:IsShown() and printed[#printed]:find("combat"))
combat = false
PlayerHousingTabCollection.scripts.OnClick()
assert(PlayerHousingCollectionPanel:IsShown() and not PlayerHousingIslandPanel:IsShown())
-- The Collection's secure slots wait for combat to end.
combat = true
PlayerHousingCollectionNext.scripts.OnClick()
combat = false
fire("PLAYER_REGEN_ENABLED")
assert(PlayerHousingCollectionPageText.text:find("^Page 2"), "caught up after combat: " .. PlayerHousingCollectionPageText.text)
PlayerHousingCollectionPrev.scripts.OnClick()

-- The undo history: right-click Undo, then undo back to any change.
PlayerHousingButton3.scripts.OnClick(PlayerHousingButton3, "RightButton")
assert(PlayerHousingHistory:IsShown() and last() == ".house data history", last())
rows("history", { "undo\tmoved Barrel", "undo\tplaced Westfall Chair", "undo\tturned Tiny Table 45° left", "redo\tpicked up Lantern" })
assert(PlayerHousingHistoryRow1Text.text == "1. moved Barrel" and PlayerHousingHistoryRow3:IsShown() and not PlayerHousingHistoryRow4:IsShown(),
  PlayerHousingHistoryRow1Text.text)
PlayerHousingHistoryRow2.scripts.OnEnter(PlayerHousingHistoryRow2)
assert(PlayerHousingHistoryRow1.highlighted and PlayerHousingHistoryRow2.highlighted and not PlayerHousingHistoryRow3.highlighted, "what goes")
PlayerHousingHistoryRow2.scripts.OnClick(PlayerHousingHistoryRow2)
assert(last() == ".house undo 2" and not PlayerHousingHistory:IsShown(), last())
PlayerHousingButton3.scripts.OnClick(PlayerHousingButton3, "LeftButton")
assert(last() == ".house undo", last())

-- A row of copies.
SEL("Row...").scripts.OnClick()
assert(PlayerHousingRowDialog:IsShown() and PlayerHousingRowCount.text == "3")
PlayerHousingRowCount.text = "5"
PlayerHousingRowSpacing.text = "1.5"
local directionButton
for _, f in ipairs(frames) do if f.parent == PlayerHousingRowDialog and f.text == "right" then directionButton = f end end
directionButton.scripts.OnClick(directionButton)
assert(directionButton.text == "left")
for _, f in ipairs(frames) do if f.parent == PlayerHousingRowDialog and f.text == "Place row" then f.scripts.OnClick(f) end end
assert(last() == ".house row 5 1.50 left" and not PlayerHousingRowDialog:IsShown(), last())

-- Several pieces selected: the group row, the banner and picking them all up.
local GROUP = "state\t1\t1\t13\tBarrel\t5\t200\t1\t10\tmoved Barrel\tKrookowner\t\t0\t0\t0\t0\t1\t0\t0\t3"
fire("CHAT_MSG_ADDON", "HOUSING", GROUP, "WHISPER", "Krookowner")
assert(SEL("Height").enabled and SEL("Space").enabled and PlayerHousingEditHudName.text == "Barrel and 2 more", PlayerHousingEditHudName.text)
SEL("Height").scripts.OnClick(); assert(last() == ".house match height", last())
PlayerHousingEditHudLine.scripts.OnClick(PlayerHousingEditHudLine); assert(last() == ".house match line", last())
PlayerHousingEditHudSpace.scripts.OnClick(PlayerHousingEditHudSpace); assert(last() == ".house match space", last())
SEL("Save set").scripts.OnClick()
assert(popups[#popups][1] == "PLAYERHOUSING_SAVE_SET")
StaticPopupDialogs.PLAYERHOUSING_SAVE_SET.OnAccept({ editBox = { GetText = function() return " Dining " end } })
assert(last() == ".house set save Dining", last())
SEL("Pick up").scripts.OnClick()
assert(popups[#popups][1] == "PLAYERHOUSING_PICKUP_GROUP" and popups[#popups][2] == 3, "several: ask first")
StaticPopupDialogs.PLAYERHOUSING_PICKUP_GROUP.OnAccept({})
assert(last() == ".house pickup", last())
fire("CHAT_MSG_ADDON", "HOUSING", GROUP:gsub("\t3$", "\t2"), "WHISPER", "Krookowner")
assert(SEL("Height").enabled and not SEL("Space").enabled, "spacing out takes three")
-- Ctrl held while decorating: the server hears it, once each way.
count = #sent
IsControlKeyDown = function() return true end
PlayerHousingEditKeys.scripts.OnUpdate(PlayerHousingEditKeys, 0.1)
PlayerHousingEditKeys.scripts.OnUpdate(PlayerHousingEditKeys, 0.1)
assert(#sent == count + 1 and last() == ".house group hold on", last())
IsControlKeyDown = function() return false end
PlayerHousingEditKeys.scripts.OnUpdate(PlayerHousingEditKeys, 0.1)
assert(#sent == count + 2 and last() == ".house group hold off", last())
fire("CHAT_MSG_ADDON", "HOUSING", "state\t1\t1\t13\tBarrel\t5\t200\t1\t10\tmoved Barrel\tKrookowner\t\t0\t0", "WHISPER", "Krookowner")

-- Buildings with a picture from the photo tour show it instead of the floor plan.
PlayerHousing_Models[902200] = { false, 30, 10, 8 }
PlayerHousing_Pictures[902200] = true
cartSlot.scripts.OnEnter(cartSlot)
assert(PlayerHousingPreviewPicture:IsShown() and not PlayerHousingPreviewPlan:IsShown()
  and PlayerHousingPreviewPicture.texture == "Interface\\AddOns\\PlayerHousing\\Pictures\\902200", tostring(PlayerHousingPreviewPicture.texture))
cartSlot.scripts.OnLeave(cartSlot)

-- The photo tour: clear skies, then midday, then each building: the interface hides, a
-- screenshot, and the next.
local tour = PlayerHousingPhotoTour
SlashCmdList.PLAYERHOUSING("phototour")
assert(last() == ".house weather clear", last())
now = now + 2; tour.scripts.OnUpdate(tour, 2)
assert(last() == ".house time midday", last())
now = now + 2; tour.scripts.OnUpdate(tour, 2)
assert(last() == ".house phototour start", last())
msg("photo\t902200\t1\t2")
now = now + 5; tour.scripts.OnUpdate(tour, 5)
assert(not UIParent:IsShown(), "the interface hides for the picture")
now = now + 1; tour.scripts.OnUpdate(tour, 1)
assert(screenshots == 1 and PlayerHousingDB.photos[902200], "a screenshot, remembered for the building")
now = now + 2; tour.scripts.OnUpdate(tour, 2)
assert(UIParent:IsShown() and last() == ".house phototour next", last())
msg("photo\tdone")
assert(printed[#printed]:find("1 pictures"), printed[#printed])

-- The minimap button: click for the window, right-click for edit mode, drag round the edge.
assert(PlayerHousingMinimapButton:IsShown(), "minimap button")
local shownBefore = PlayerHousingFrame:IsShown()
PlayerHousingMinimapButton.scripts.OnClick(PlayerHousingMinimapButton, "LeftButton")
assert(PlayerHousingFrame:IsShown() ~= shownBefore, "toggles the window")
PlayerHousingMinimapButton.scripts.OnClick(PlayerHousingMinimapButton, "LeftButton")
PlayerHousingMinimapButton.scripts.OnClick(PlayerHousingMinimapButton, "RightButton")
assert(last() == ".house edit", last())
cursorX, cursorY = 100, 180
PlayerHousingMinimapButton.scripts.OnDragStart(PlayerHousingMinimapButton)
PlayerHousingMinimapButton.scripts.OnUpdate(PlayerHousingMinimapButton, 0.1)
PlayerHousingMinimapButton.scripts.OnDragStop(PlayerHousingMinimapButton)
assert(math.abs(PlayerHousingDB.minimapAngle - 90) < 0.01, "dragged to the top: " .. PlayerHousingDB.minimapAngle)
SlashCmdList.PLAYERHOUSING("minimap")
assert(not PlayerHousingMinimapButton:IsShown() and PlayerHousingDB.minimapHidden, "hidden")
SlashCmdList.PLAYERHOUSING("minimap")
assert(PlayerHousingMinimapButton:IsShown(), "back")

-- /housing key: the House Key opens the menu instead.
SlashCmdList.PLAYERHOUSING("key")
assert(last() == ".house addon 1 0" and PlayerHousingDB.keyWindow == false, last())

-- Empty bags.
bags = {}
fire("BAG_UPDATE")
OnUpdate(driver, 1)
print("sent:", table.concat(sent, " | "))
print("ALL ADDON CHECKS PASSED")
