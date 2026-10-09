-- Runs the addon outside the game against a few stubbed WoW 3.3.5 API calls and checks what
-- it does with state messages, the server's lists, clicks and the mouse wheel. The files go in
-- the order of PlayerHousing.toc:
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
    or k == "location" or k == "item" or k == "unit" or k == "alpha" then return nil end
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
function Widget:GetWidth() return self.width or 0 end
function Widget:GetPoint() return "CENTER", nil, "CENTER", 10, 20 end
function Widget:Enable() self.enabled = true end
function Widget:SetModel(path) self.modelPath = path end
function Widget:GetModel() return self.modelPath end
function Widget:SetAlpha(a) self.alpha = a end
function Widget:GetModelScale() return self.modelScale end
function Widget:ClearModel() self.modelPath = nil end
function Widget:SetUnit(unit) self.unit = unit end
function Widget:SetCreature(id) self.creature = id end
function Widget:Disable() self.enabled = false end
function Widget:GetName() return self.name end
function Widget:SetParent(p) self.parent = p end
function Widget:GetParent() return self.parent end
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
local mouseoverUnit
function UnitName(unit) if unit == "mouseover" then return mouseoverUnit and mouseoverUnit[1] end return "Krookowner" end
function IsControlKeyDown() return false end
function IsShiftKeyDown() return false end
function PickupContainerItem() end
local itemCounts = { [901105] = 5 }
function GetItemCount(id) return itemCounts[id] or 0 end
function GetItemInfo(id) return nil end
function GetItemIcon(id) return nil end
UISpecialFrames = {}
tinsert = table.insert
local INVENTORY = { HeadSlot = 1, ShoulderSlot = 3, BackSlot = 15, ChestSlot = 5, ShirtSlot = 4, TabardSlot = 19, WristSlot = 9,
  HandsSlot = 10, WaistSlot = 6, LegsSlot = 7, FeetSlot = 8, MainHandSlot = 16, SecondaryHandSlot = 17, RangedSlot = 18 }
function GetInventorySlotInfo(name) return INVENTORY[name], "Interface\\PaperDoll\\UI-PaperDoll-Slot-" .. name end
local cursorItem
function GetCursorInfo() if cursorItem then return "item", cursorItem end end
function ClearCursor() cursorItem = nil end
-- { name, guid } of what's under the mouse, nil for nothing
function UnitExists(unit) return unit == "mouseover" and mouseoverUnit ~= nil end
function UnitIsPlayer(unit) return false end
function UnitGUID(unit) return unit == "mouseover" and mouseoverUnit and mouseoverUnit[2] or nil end
function GetCoinTextureString(copper) return copper .. "c" end
lastMenu = nil
function EasyMenu(menu) lastMenu = menu end
function CloseDropDownMenus() end
DELETE = "Delete"
local now = 0
function GetTime() return now end
local cursorX, cursorY = 0, 0
function GetCursorPosition() return cursorX, cursorY end
-- The world's view, the mouse over it, and the addon messages sent (Mouse.lua).
WorldFrame = CreateFrame("Frame", "WorldFrame")
function WorldFrame:GetLeft() return 0 end
function WorldFrame:GetBottom() return 0 end
function WorldFrame:GetWidth() return 1000 end
function WorldFrame:GetHeight() return 750 end
function WorldFrame:GetEffectiveScale() return 1 end
local mouseFocus, mouseButtons, addonSent = nil, {}, {}
function GetMouseFocus() return mouseFocus end
function IsMouselooking() return false end
local facing = 0
function GetPlayerFacing() return facing end
function IsMouseButtonDown(button) return mouseButtons[button] or false end
function SendAddonMessage(prefix, message, channel, target)
  addonSent[#addonSent + 1] = { prefix, message, channel, target }
  -- Commands over AzerothCore's addon command channel count as sent, like chat ones (all but
  -- the mouse's points, which the mouse checks count on their own).
  local command = prefix == "AzerothCore" and channel == "WHISPER" and message:match("^i%d%d%d%d(house.*)$")
  if command and not command:find("^house ghost at ") then sent[#sent + 1] = "." .. command end
end
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
assert(PlayerHousingCollectionPanel:IsShown() and not PlayerHousingTabBags and not PlayerHousingTabStorage, "the Collection is the first tab; no Bags or Storage tab")
assert(sent[#sent] == ".house data collection", "the Collection asks for its list: " .. tostring(sent[#sent]))
local chairSlot, barrelPieceSlot, cartSlot = PlayerHousingCollectionSlot1, PlayerHousingCollectionSlot11, PlayerHousingCollectionSlot28
assert(chairSlot.info[1] == 901105 and barrelPieceSlot.info[1] == 902101 and cartSlot.info[1] == 902200, "the Collection's order")
assert(chairSlot.count.text == "", "no counts before the server's list (pieces never come from the bags)")
-- The server's counts: what the player owns to place. The bags don't count (the House Key's there).
local function list(kind, rows, arg)
  fire("CHAT_MSG_ADDON", "HOUSING", "begin\t" .. kind .. (arg and ("\t" .. arg) or ""), "WHISPER", "Krookowner")
  for _, row in ipairs(rows) do
    fire("CHAT_MSG_ADDON", "HOUSING", "row\t" .. kind .. "\t" .. row, "WHISPER", "Krookowner")
  end
  fire("CHAT_MSG_ADDON", "HOUSING", "end\t" .. kind .. (arg and ("\t" .. arg) or ""), "WHISPER", "Krookowner")
end
list("collection", { "settings\t0\t0\t200\t10\t0", "unlocked\t901105-901106,902101,902200", "storage\t901105:5,902101:1,902200:1", "placed\t901105:1" })
assert(chairSlot.count.text == 5 and barrelPieceSlot.count.text == 1 and cartSlot.count.text == 1, "the counts: " .. tostring(chairSlot.count.text))
assert(PlayerHousingCollectionSlot2.count.text == "" and not chairSlot.protected, "none of the table; plain buttons, fine in combat")
assert(not PlayerHousingSelected:IsShown(), "nothing held: no held panel (a piece being selected is no panel of its own)")
local function SEL(text)
  for _, f in ipairs(frames) do if f.parent == PlayerHousingSelected and f.text == text then return f end end
  error("no selected-panel button " .. text)
end

-- Hovering a piece previews its model and size; leaving hides it.
local measuredCamera = PlayerHousing_PreviewCamera
PlayerHousing_PreviewCamera = nil  -- first the framing without measurements
chairSlot.scripts.OnEnter(chairSlot)
assert(PlayerHousingPreview:IsShown(), "preview shown")
assert(PlayerHousingPreviewModel.modelPath == PlayerHousing_Models[901105][1] and PlayerHousingPreviewModel:IsShown() and not PlayerHousingPreviewUnit:IsShown(),
  "chair model, in the objects' frame: " .. tostring(PlayerHousingPreviewModel.modelPath))
-- At its own size (shrunk, it isn't drawn), moved back twice its longest side (2 yards at
-- least), about its middle, raised a quarter of its size; turned about that middle.
local function near(a, b) return math.abs(a - b) < 1e-6 end
local chair = PlayerHousing_Models[901105]
local midX, midY, midZ = chair[6], chair[7], chair[8]
local size = math.max(chair[5], 0.5)  -- the model's own units
local model, away = PlayerHousingPreviewModel, math.max(2, 2 * size)
assert(midZ > 0, "the chair has bounds")
assert(near(model.modelScale, 1), "its own size: " .. tostring(model.modelScale))
assert(near(model.position[1], -midX - away) and near(model.position[2], -midY) and near(model.position[3], -midZ + 0.25 * size),
  "back, about its middle: " .. table.concat(model.position, ", "))
PlayerHousingPreview.scripts.OnUpdate(PlayerHousingPreview, 1)
local c, s = math.cos(0.6), math.sin(0.6)
assert(near(model.facing, 0.6) and near(model.position[1], -(midX * c - midY * s) - away), "turns about its middle")
-- Drag to turn it (and it stops turning by itself), the wheel brings it nearer, right-drag lifts.
model.scripts.OnMouseDown(model, "LeftButton")
cursorX = 100
PlayerHousingPreview.scripts.OnUpdate(PlayerHousingPreview, 0.01)
model.scripts.OnMouseUp(model)
assert(near(model.facing, 2.1), "dragged: " .. model.facing)
PlayerHousingPreview.scripts.OnUpdate(PlayerHousingPreview, 1)
assert(near(model.facing, 2.1), "no more turning by itself")
model.scripts.OnMouseWheel(model, 1)
local c2, s2 = math.cos(2.1), math.sin(2.1)
assert(near(model.position[1], -(midX * c2 - midY * s2) - away / 1.2) and near(model.modelScale, 1), "nearer, not bigger")
model.scripts.OnMouseDown(model, "RightButton")
cursorY = 50
PlayerHousingPreview.scripts.OnUpdate(PlayerHousingPreview, 0.01)
model.scripts.OnMouseUp(model)
assert(near(model.position[3], -midZ + 0.25 * size + 0.5), "lifted: " .. model.position[3])
assert(SlashCmdList.PLAYERHOUSING and PlayerHousingDB.framing == nil, "no framing setting any more")
chairSlot.scripts.OnLeave(chairSlot)
-- Back again later: facing front again, but as near and as high as it was set by hand.
barrelPieceSlot.scripts.OnEnter(barrelPieceSlot)
barrelPieceSlot.scripts.OnLeave(barrelPieceSlot)
chairSlot.scripts.OnEnter(chairSlot)
assert(near(model.facing, 0) and near(model.position[1], -midX - away / 1.2) and near(model.position[3], -midZ + 0.25 * size + 0.5),
  "the chair's own view kept: " .. table.concat(model.position, ", "))
-- Reset puts the view back: facing front, its whole size, turning.
model.scripts.OnMouseWheel(model, 1)
PlayerHousingPreviewReset.scripts.OnClick(PlayerHousingPreviewReset)
assert(near(model.position[1], -midX - away) and near(model.position[3], -midZ + 0.25 * size) and near(model.facing, 0), "Reset: " .. model.position[1])
-- Nearer and higher by hand are kept for the piece until Reset.
model.scripts.OnMouseWheel(model, 1)
chairSlot.scripts.OnLeave(chairSlot)
barrelPieceSlot.scripts.OnEnter(barrelPieceSlot)
barrelPieceSlot.scripts.OnLeave(barrelPieceSlot)
chairSlot.scripts.OnEnter(chairSlot)
assert(near(model.position[1], -midX - away / 1.2), "kept for the chair: " .. model.position[1])
PlayerHousingPreviewReset.scripts.OnClick(PlayerHousingPreviewReset)
assert(PlayerHousingDB.previewViews[901105] == nil and near(model.position[1], -midX - away), "Reset forgets it")
chairSlot.scripts.OnLeave(chairSlot)
assert(not PlayerHousingPreview:IsShown(), "preview hidden on leave")
-- With the camera measured from screenshots (PreviewFix.lua): the chair at its measured distance
-- and middle height, its middle where the shots showed it.
PlayerHousing_PreviewCamera = measuredCamera
local cam, fix = PlayerHousing_PreviewCamera, PlayerHousing_PreviewFix[901105]
chairSlot.scripts.OnEnter(chairSlot)
PlayerHousingPreviewReset.scripts.OnClick(PlayerHousingPreviewReset)
assert(cam and fix and near(model.position[1], -(midX + fix[3]) - (fix[1] - cam.distance)) and near(model.position[3], -midZ + fix[2]),
  "measured framing: " .. table.concat(model.position, ", "))
model.scripts.OnMouseWheel(model, 1)   -- nearer: the middle follows the camera's aim
local seen = cam.distance + (fix[1] - cam.distance) / 1.2
assert(near(model.position[1], -(midX + fix[3]) - (seen - cam.distance)) and near(model.position[3], -midZ + fix[2] - cam.aim * (seen - fix[1]) / cam.focal),
  "measured, nearer: " .. table.concat(model.position, ", "))
PlayerHousingPreviewReset.scripts.OnClick(PlayerHousingPreviewReset)
chairSlot.scripts.OnLeave(chairSlot)
-- Buildings made of world models get a floor plan, to scale, instead of a model.
PlayerHousing_Models[902200] = { false, 30, 10, 8 }
cartSlot.scripts.OnEnter(cartSlot)
assert(not PlayerHousingPreviewModel:IsShown() and not PlayerHousingPreviewUnit:IsShown(), "no model for a world model building")
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
-- After the player (a mannequin's preview), an object shows in its own frame: the frame that
-- showed the player keeps drawing the player whatever it's given later.
PlayerHousing_Models[902200] = { "player", 1, 1, 2 }
cartSlot.scripts.OnEnter(cartSlot)
assert(PlayerHousingPreviewUnit.unit == "player" and PlayerHousingPreviewUnit:IsShown() and not PlayerHousingPreviewModel:IsShown(), "the player, in its own frame")
assert(PlayerHousingPreviewModel.unit == nil, "the objects' frame never shows a unit")
cartSlot.scripts.OnLeave(cartSlot)
chairSlot.scripts.OnEnter(chairSlot)
assert(PlayerHousingPreviewModel:IsShown() and not PlayerHousingPreviewUnit:IsShown() and PlayerHousingPreviewModel.unit == nil, "then a chair: the player's frame hidden")
-- A model the frame can't load: the floor plan instead of an empty view.
local realSetModel = Widget.SetModel
Widget.SetModel = function(self, path) self.modelPath = nil end
PlayerHousing_Models[902200] = { "World\\Nowhere\\Missing.m2", 4, 8, 5, 8, 0, 0, 2 }
chairSlot.scripts.OnLeave(chairSlot)
cartSlot.scripts.OnEnter(cartSlot)
assert(not PlayerHousingPreviewModel:IsShown() and PlayerHousingPreviewPlan:IsShown(), "unloadable model: floor plan")
SlashCmdList.PLAYERHOUSING("preview")
assert(printed[#printed]:find("Missing.m2"), printed[#printed])
cartSlot.scripts.OnLeave(cartSlot)
Widget.SetModel = realSetModel
chairSlot.scripts.OnEnter(chairSlot)
chairSlot.scripts.OnLeave(chairSlot)
assert(PlayerHousingFrame.height == 400, PlayerHousingFrame.height)

-- Owned: only what there's one of to place.
PlayerHousingOwnedOnly.checked = true
PlayerHousingOwnedOnly.scripts.OnClick(PlayerHousingOwnedOnly)
assert(PlayerHousingCollectionSlot1.info[1] == 901105 and PlayerHousingCollectionSlot2.info[1] == 902101 and PlayerHousingCollectionSlot3.info[1] == 902200
  and not PlayerHousingCollectionSlot4:IsShown(), "only the owned ones")
PlayerHousingOwnedOnly.checked = false
PlayerHousingOwnedOnly.scripts.OnClick(PlayerHousingOwnedOnly)
-- Sorting: by name puts the Barrel before the chair.
PlayerHousingCollectionSort.scripts.OnClick()
assert(PlayerHousingCollectionSort.text == "Sort: name" and PlayerHousingCollectionSlot1.info[2] < "Westfall", PlayerHousingCollectionSlot1.info[2])
PlayerHousingCollectionSort.scripts.OnClick()
PlayerHousingCollectionSort.scripts.OnClick()
PlayerHousingCollectionSort.scripts.OnClick()
-- By unlock: the same unlock together, levels first (by value), the ones everyone has last.
assert(PlayerHousingCollectionSort.text == "Sort: unlock" and PlayerHousingCollectionSlot1.info[7]:find("^Reach level"),
  "unlock sort: " .. tostring(PlayerHousingCollectionSlot1.info[7]))
local firstLevel = tonumber(PlayerHousingCollectionSlot1.info[7]:match("(%d+)"))
assert(firstLevel and firstLevel <= tonumber(PlayerHousingCollectionSlot2.info[7]:match("(%d+)") or 999), "levels by value")
PlayerHousingCollectionSort.scripts.OnClick()
assert(PlayerHousingCollectionSort.text == "Sort: collection" and PlayerHousingCollectionSlot1.info[1] == 901105)

-- Toolbar buttons send .house commands: Go home (Leave here), Undo, Redo, Help. No edit mode.
local function last() return sent[#sent] end
PlayerHousingButton1.scripts.OnClick(); assert(last() == ".house leave", last())
PlayerHousingButton2.scripts.OnClick(); assert(last() == ".house undo")
local printedBefore = #printed
PlayerHousingButton4.scripts.OnClick(); assert(#printed > printedBefore and last() == ".house undo", "Help prints how it works, sends nothing")
assert(PlayerHousingButton3.enabled == false, "nothing to redo")
assert(PlayerHousingButton2.text == "Undo" and PlayerHousingButton4.text == "Help", "no Edit button")
-- Undo tooltip reads the label.
PlayerHousingButton2.scripts.OnEnter(PlayerHousingButton2)
assert(not PlayerHousingFrame.scripts.OnMouseWheel, "the wheel over the window doesn't turn pieces")

-- Holding a piece (fields 21 to 23; 26 to 30: size, tilt, count and its front): the held panel
-- says what it is, and its keys come on: the wheel turns it, Shift+wheel raises it, Escape
-- puts it back. Nothing else is bound.
local OnUpdate = driver.scripts.OnUpdate
local function step() now = now + 1; OnUpdate(driver, 1) end
local GHOST = "state\t1\t1\t13\tBarrel\t5\t200\t1\t10\tmoved Barrel\tKrookowner\t\t0\t0\t0\t0\t0\t0\t0\t1\t902101\tnew\t1\t\t0\t100\t0\t0\t1"
fire("CHAT_MSG_ADDON", "HOUSING", GHOST, "WHISPER", "Krookowner")
assert(PlayerHousingSelected:IsShown() and PlayerHousingFrame.height == 550, "held panel: " .. tostring(PlayerHousingFrame.height))
assert(PlayerHousingHeldText.text:find("Placing:") and PlayerHousingHeldText.text:find("Barrel"), PlayerHousingHeldText.text)
assert(PlayerHousingHeldShape.text == "Size 100%   Front points away from you", PlayerHousingHeldShape.text)
fire("CHAT_MSG_ADDON", "HOUSING", GHOST .. "\t90", "WHISPER", "Krookowner")
assert(PlayerHousingHeldShape.text:find("Front points to your left"), PlayerHousingHeldShape.text)
fire("CHAT_MSG_ADDON", "HOUSING", GHOST, "WHISPER", "Krookowner")
assert(not overrides.MOUSEWHEELUP and not overrides.MOUSEWHEELDOWN, "the plain wheel zooms the camera as ever")
assert(overrides["SHIFT-MOUSEWHEELUP"] == "PlayerHousingEditWheelUp" and overrides["CTRL-MOUSEWHEELDOWN"] == "PlayerHousingEditRaiseWheelDown"
  and overrides.ESCAPE == "PlayerHousingEditCancel", "held keys bound")
assert(not overrides.TAB and not overrides.DELETE and not overrides.R and not overrides["CTRL-Z"], "no edit-mode keys")
-- No DLL: it follows you, so the arrows and G drive it too.
assert(overrides.G == "PlayerHousingEditSetDown" and overrides.UP == "PlayerHousingEditForward", "follow keys without the mouse")
-- Binding names keep the game's modifier order (ALT-CTRL-SHIFT-), or they never fire.
for key in pairs(overrides) do
  assert(not key:find("CTRL%-ALT") and not key:find("SHIFT%-ALT") and not key:find("SHIFT%-CTRL"), "modifier order: " .. key)
end
-- No buttons change it: the panel says which wheel does what.
for _, f in ipairs(frames) do
  assert(not (f.parent == PlayerHousingSelected and f:IsShown() and f.template == "UIPanelButtonTemplate"), "a button on the held panel: " .. tostring(f.text))
end
assert(PlayerHousingHeldKeys.text:find("Alt%+wheel|r  tilt forward") and PlayerHousingHeldKeys.text:find("Ctrl%+Alt%+wheel|r  size")
  and PlayerHousingHeldKeys.text:find("|cffffd100Wheel|r  zoom")
  and PlayerHousingHeldHelp.text:find("Middle%-click|r  stand it straight"), PlayerHousingHeldKeys.text)
assert(overrides["ALT-MOUSEWHEELUP"] == "PlayerHousingEditTiltForward" and overrides["ALT-SHIFT-MOUSEWHEELDOWN"] == "PlayerHousingEditTiltLeft"
  and overrides["ALT-CTRL-MOUSEWHEELUP"] == "PlayerHousingEditBigger" and overrides["CTRL-SHIFT-MOUSEWHEELDOWN"] == "PlayerHousingEditFineRight"
  and overrides.BUTTON3 == "PlayerHousingEditStraight" and overrides["SHIFT-BUTTON3"] == "PlayerHousingEditGrid"
  and overrides["CTRL-BUTTON3"] == "PlayerHousingEditNormalSize", "the wheel's combinations bound")
-- The wheel and steps add up into one command, a few times a second.
PlayerHousingEditWheelUp.scripts.OnClick(PlayerHousingEditWheelUp, "LeftButton", true)
PlayerHousingEditWheelUp.scripts.OnClick(PlayerHousingEditWheelUp, "LeftButton", true)
PlayerHousingEditLeft.scripts.OnClick(PlayerHousingEditLeft, "LeftButton", true)
PlayerHousingEditLeft.scripts.OnClick(PlayerHousingEditLeft, "LeftButton", false)
step()
assert(last() == ".house ghost adjust 0.00 0.25 0.00 30", "two wheel notches and a step, in one: " .. last())
PlayerHousingEditRaiseWheelDown.scripts.OnClick(PlayerHousingEditRaiseWheelDown, "LeftButton", true)
step()
assert(last() == ".house ghost adjust 0.00 0.00 -0.10 0", last())
PlayerHousingEditSetDown.scripts.OnClick(PlayerHousingEditSetDown, "LeftButton", true)
assert(last() == ".house ghost place", last())
IsShiftKeyDown = function() return true end
PlayerHousingEditSetDown.scripts.OnClick(PlayerHousingEditSetDown, "LeftButton", true)
assert(last() == ".house ghost place another", last())
IsShiftKeyDown = function() return false end
PlayerHousingEditCancel.scripts.OnClick(PlayerHousingEditCancel, "LeftButton", true)
assert(last() == ".house ghost cancel", last())
-- Size, tilt and fine turns are the wheel with modifiers; the middle button straightens, sets
-- the normal size, or steps the grid.
local function press(name) _G[name].scripts.OnClick(_G[name], "LeftButton", true) end
press("PlayerHousingEditTiltForward"); assert(last() == ".house tilt forward 15", last())
press("PlayerHousingEditTiltBack"); assert(last() == ".house tilt back 15", last())
press("PlayerHousingEditTiltRight"); assert(last() == ".house tilt right 15", last())
press("PlayerHousingEditTiltLeft"); assert(last() == ".house tilt left 15", last())
press("PlayerHousingEditBigger"); assert(last() == ".house size bigger", last())
press("PlayerHousingEditSmaller"); assert(last() == ".house size smaller", last())
press("PlayerHousingEditStraight"); assert(last() == ".house tilt straight", last())
press("PlayerHousingEditNormalSize"); assert(last() == ".house size normal", last())
press("PlayerHousingEditGrid"); assert(last() == ".house grid 0.25", last())
press("PlayerHousingEditFineLeft"); press("PlayerHousingEditFineLeft"); step()
assert(last() == ".house ghost adjust 0.00 0.00 0.00 2", "fine turns: " .. last())
-- The server says how it is sized and tilted; tilted furniture is shown as its real object.
fire("CHAT_MSG_ADDON", "HOUSING", GHOST:gsub("\t100\t0\t0\t1$", "\t120\t180\t0\t1"), "WHISPER", "Krookowner")
assert(PlayerHousingHeldShape.text:find("Size 120%%") and PlayerHousingHeldShape.text:find("180"), PlayerHousingHeldShape.text)
-- Moving a mannequin: Dress (its clothes aren't placing), for one piece only.
local MOVING = "state\t1\t1\t13\tMannequin\t5\t200\t1\t10\tmoved Barrel\tKrookowner\t\t0\t0\t0\t0\t0\t0\t0\t1\t902101\tmove\t1\t\t1\t100\t0\t0\t1"
fire("CHAT_MSG_ADDON", "HOUSING", MOVING, "WHISPER", "Krookowner")
assert(PlayerHousingHeldText.text:find("Moving:"), PlayerHousingHeldText.text)
assert(SEL("Dress..."):IsShown(), "a mannequin: Dress")
fire("CHAT_MSG_ADDON", "HOUSING", MOVING:gsub("\t1$", "\t3"), "WHISPER", "Krookowner")
assert(PlayerHousingHeldText.text:find("and 2 more") and not SEL("Dress..."):IsShown(), "several: no dress")
-- Set down: the keys and the panel go.
fire("CHAT_MSG_ADDON", "HOUSING", "state\t1\t1\t13\tBarrel\t5\t200\t1\t10\tmoved Barrel\tKrookowner\t\t0\t0\t0\t0\t0\t0\t0\t1\t0\t\t1", "WHISPER", "Krookowner")
assert(next(overrides) == nil and not PlayerHousingSelected:IsShown(), "keys and panel gone after the ghost")

-- With PlayerHousing.dll: the piece follows the mouse over the world (points go quietly over
-- AzerothCore's addon command channel), and a click on the world sets it down.
local pointed = { 16210.5, 16255.25, 12, 0, 0, 1 }
local asked
PlayerHousing_CursorWorld = function(fx, fy) asked = { fx, fy }; return unpack(pointed) end
-- The addon pinged AzerothCore's command channel when the server first reported housing; until
-- it answers, the mouse isn't used (a server with AddonChannel = 0 never does).
assert(addonSent[1] and addonSent[1][1] == "AzerothCore" and addonSent[1][2] == "p0000" and addonSent[1][4] == "Krookowner",
  "the channel is pinged: " .. tostring(addonSent[1] and addonSent[1][2]))
for _, command in ipairs(sent) do
  assert(not command:find("mouse"), "no answer from the channel yet: no mouse: " .. command)
end
fire("CHAT_MSG_ADDON", "AzerothCore", "a0000", "WHISPER", "Krookowner")
assert(sent[#sent - 1] == ".house addon 2 mouse" and last() == ".house state", "the channel answers: the server hears about the mouse: "
  .. tostring(sent[#sent - 1]) .. " " .. last())
assert(addonSent[#addonSent][2]:match("^i%d%d%d%dhouse state$"), "and every command goes over the channel from now on, not as chat")
wipe(addonSent)
fire("CHAT_MSG_ADDON", "HOUSING", GHOST .. "\t", "WHISPER", "Krookowner")
assert(PlayerHousingHeldHelp.text:find("Left%-click|r  set it down") and PlayerHousingHeldHelp.text:find("Shift%+right%-click|r  put it away"), PlayerHousingHeldHelp.text)
assert(not overrides.G and not overrides.UP, "with the mouse: no arrows or G")
local mouse = PlayerHousingMouse
local function tick(seconds) now = now + seconds; mouse.scripts.OnUpdate(mouse, seconds) end
local function lastAddon() return addonSent[#addonSent] end
-- Nothing is traced before the world is there (a loading screen).
mouseFocus = WorldFrame
cursorX, cursorY = 250, 600
asked = nil
tick(0.2)
assert(asked == nil and #addonSent == 0, "no tracing before entering the world")
mouse.scripts.OnEvent(mouse, "PLAYER_ENTERING_WORLD")
cursorX, cursorY = 250, 600
tick(0.1)
assert(#addonSent == 1 and lastAddon()[1] == "AzerothCore" and lastAddon()[3] == "WHISPER" and lastAddon()[4] == "Krookowner"
  and lastAddon()[2]:match("^i%d%d%d%dhouse ghost at 16210%.50 16255%.25 12%.00 0%.00 0%.00 1%.00$"), tostring(lastAddon() and lastAddon()[2]))
assert(asked[1] == 0.25 and asked[2] == 0.8, "the mouse as a fraction of the view: " .. asked[1] .. " " .. asked[2])
tick(0.1)
assert(#addonSent == 1, "the same spot isn't sent again")
pointed[1] = 16210.52
tick(0.1)
assert(#addonSent == 1, "a hair's move isn't sent")
pointed[1] = 16212
tick(0.05)
assert(#addonSent == 1, "not before a tenth of a second")
tick(0.05)
assert(#addonSent == 2 and lastAddon()[2]:find("ghost at 16212%.00"), lastAddon()[2])
-- Over the window, or with a button held (turning the camera): it waits.
mouseFocus = PlayerHousingFrame
pointed[1] = 16215
tick(0.2)
assert(#addonSent == 2, "the mouse over the window moves nothing")
mouseFocus = WorldFrame
mouseButtons.RightButton = true
tick(0.2)
assert(#addonSent == 2, "right button held: the camera turns, the piece waits")
facing = 0.5
tick(0.1)
mouseButtons.RightButton = nil
tick(0.01)
assert(last() ~= ".house ghost cancel", "a right-drag that turned the player isn't a click")
-- A right-click on the world: never mind.
mouseButtons.RightButton = true
tick(0.1)
mouseButtons.RightButton = nil
tick(0.01)
assert(last() == ".house ghost cancel", "right-click puts it back: " .. last())
-- Shift+right-click: a moved piece goes away to the Collection.
fire("CHAT_MSG_ADDON", "HOUSING", GHOST:gsub("\tnew\t", "\tmove\t"), "WHISPER", "Krookowner")
IsShiftKeyDown = function() return true end
mouseButtons.RightButton = true
tick(0.1)
mouseButtons.RightButton = nil
tick(0.01)
IsShiftKeyDown = function() return false end
assert(sent[#sent - 1] == ".house ghost cancel" and last() == ".house pickup 13", "put away: " .. sent[#sent - 1] .. " " .. last())
fire("CHAT_MSG_ADDON", "HOUSING", GHOST, "WHISPER", "Krookowner")
-- A long right press (looking around) isn't one.
mouseButtons.RightButton = true
tick(0.1); tick(0.5)
mouseButtons.RightButton = nil
local cancels = #sent
tick(0.01)
assert(#sent == cancels, "a long right press isn't a click")
-- A click on the world: sent there, and set down.
local before = #sent
mouseButtons.LeftButton = true
tick(0.1)
mouseButtons.LeftButton = nil
tick(0.1)
assert(#sent == before + 1 and last() == ".house ghost place at 16215.00 16255.25 12.00 0.00 0.00 1.00", "the click carries its point: " .. last())
IsShiftKeyDown = function() return true end
mouseButtons.LeftButton = true
tick(0.1)
mouseButtons.LeftButton = nil
tick(0.1)
assert(last():find("^%.house ghost place another at 16215%.00"), "Shift-click: " .. last())
IsShiftKeyDown = function() return false end
-- A long press, or one that turned the camera, is a drag: nothing set down.
before = #sent
mouseButtons.LeftButton = true
tick(0.1)
tick(0.5)
mouseButtons.LeftButton = nil
tick(0.1)
assert(#sent == before, "a long press isn't a click: " .. last())
mouseButtons.LeftButton = true
tick(0.1)
pointed[1] = 16230
mouseButtons.LeftButton = nil
tick(0.1)
assert(#sent == before, "the camera turned: not a click")
-- Nor one that started on the sky.
local sky = pointed
pointed = {}
mouseButtons.LeftButton = true
tick(0.1)
pointed = sky
mouseButtons.LeftButton = nil
tick(0.1)
assert(#sent == before, "a press that started on the sky isn't a click: " .. last())
-- A click on the window's buttons is theirs.
mouseFocus = PlayerHousingFrame
mouseButtons.LeftButton = true
tick(0.1)
mouseButtons.LeftButton = nil
tick(0.1)
assert(#sent == before, "a click on the window isn't one on the world")
-- The server says why it stopped following (off the island): the held panel shows it.
fire("CHAT_MSG_ADDON", "HOUSING", (GHOST:gsub("\tnew\t1\t\t", "\tnew\t1\tThat spot is off your island.\t")), "WHISPER", "Krookowner")
assert(PlayerHousingHeldShape.text:find("off your island"), tostring(PlayerHousingHeldShape.text))
-- Moving: Shift-click just sets it down.
fire("CHAT_MSG_ADDON", "HOUSING", GHOST:gsub("\tnew\t", "\tmove\t") .. "\t", "WHISPER", "Krookowner")
mouseFocus = WorldFrame
IsShiftKeyDown = function() return true end
mouseButtons.LeftButton = true
tick(0.1)
mouseButtons.LeftButton = nil
tick(0.1)
assert(last():find("^%.house ghost place at "), "Shift-click on a moved piece: " .. last())
IsShiftKeyDown = function() return false end
-- No ghost: the mouse sends nothing.
fire("CHAT_MSG_ADDON", "HOUSING", "state\t1\t1\t13\tBarrel\t5\t200\t1\t10\tmoved Barrel\tKrookowner\t\t0\t0\t0\t0\t0\t0\t0\t1\t0\t\t1\t", "WHISPER", "Krookowner")
local sentBefore = #addonSent
pointed[1] = 16240
tick(0.5)
assert(#addonSent == sentBefore, "no ghost, no points")

-- Version 2 of the DLL moves the ghost on the screen itself. /housing localghost turns that
-- off and on; the server hears either way.
local placedUnits = {}
PlayerHousing_PlaceUnit = function(hi, lo, x, y, z, o) placedUnits[#placedUnits + 1] = { hi, lo, x, y, z, o }; return 1 end
SlashCmdList.PLAYERHOUSING("localghost")
assert(last() == ".house addon 2 mouse" and PlayerHousingDB.localGhosts == false, "turned off: " .. last())
SlashCmdList.PLAYERHOUSING("localghost")
assert(last() == ".house addon 2 mouse local" and PlayerHousingDB.localGhosts == true, "and on: " .. last())
-- The server says which creatures make up the ghost and where its rules put it.
fire("CHAT_MSG_ADDON", "HOUSING", "gpiece\t1\t2\tF130DBC9\tB000012A\t0.000\t0.000\t0.000\t0.0000", "WHISPER", "Krookowner")
fire("CHAT_MSG_ADDON", "HOUSING", "gpiece\t2\t2\tF130DBC9\tB000012B\t1.000\t0.000\t0.500\t1.5708", "WHISPER", "Krookowner")
fire("CHAT_MSG_ADDON", "HOUSING", "gpose\t16240.000\t16300.000\t12.000\t0.5000\t0.250\t0.5000\t0.0000\t0.100\t0", "WHISPER", "Krookowner")
fire("CHAT_MSG_ADDON", "HOUSING", GHOST .. "\t", "WHISPER", "Krookowner")
-- The mouse moving: under it, every frame, by the server's rules (lift and the fix it made,
-- the turn on the floor), each piece where it sits from the lead.
mouseFocus = WorldFrame
pointed = { 16210.5, 16255.25, 12, 0, 0, 1 }
wipe(placedUnits)
tick(0.016)
pointed[1] = 16211.5
tick(0.016)
local function unitAt(index) return placedUnits[#placedUnits - 2 + index] end
local lead, second = unitAt(1), unitAt(2)
assert(lead and lead[1] == 0xF130DBC9 and lead[2] == 0xB000012A, "the lead's guid halves")
assert(near(lead[3], 16211.5) and near(lead[4], 16255.25) and math.abs(lead[5] - 12.35) < 1e-4 and near(lead[6], 0.5),
  ("the lead under the mouse, lifted: %s %s %s %s"):format(lead[3], lead[4], lead[5], lead[6]))
assert(second[2] == 0xB000012B and math.abs(second[3] - (16211.5 + math.cos(0.5))) < 1e-4 and math.abs(second[4] - (16255.25 + math.sin(0.5))) < 1e-4
  and math.abs(second[5] - 12.85) < 1e-4 and math.abs(second[6] - (0.5 + 1.5708)) < 1e-4, "the second piece turned with the lead")
-- On the grid (field 18: 1 yard).
fire("CHAT_MSG_ADDON", "HOUSING", "state\t1\t1\t13\tBarrel\t5\t200\t1\t10\tmoved Barrel\tKrookowner\t\t0\t0\t0\t0\t0\t1\t0\t1\t902101\tnew\t1\t",
  "WHISPER", "Krookowner")
pointed[1] = 16212.4
tick(0.016)
assert(near(unitAt(1)[3], 16212) and near(unitAt(1)[4], 16255), "snapped to the 1 yard grid: " .. unitAt(1)[3] .. " " .. unitAt(1)[4])
fire("CHAT_MSG_ADDON", "HOUSING", GHOST .. "\t", "WHISPER", "Krookowner")
-- On a wall: facing out from it, plus the turn the player gave it there; no grid.
pointed = { 16213.3, 16255.25, 13, 1, 0, 0 }
tick(0.016)
assert(near(unitAt(1)[6], 0) and near(unitAt(1)[3], 16213.3), "facing out from the wall: " .. unitAt(1)[6])
-- Resting: exactly where the server has it.
pointed = { 16213.3, 16255.25, 12, 0, 0, 1 }
tick(0.016)
tick(0.5)
assert(near(unitAt(1)[3], 16240) and near(unitAt(1)[4], 16300) and near(unitAt(1)[5], 12) and near(unitAt(1)[6], 0.5), "resting: the server's spot")
-- The mouse on the held piece itself (a tilted one is the real object): not that point, which
-- would pull it toward the camera, but where the mouse's ray meets the floor it stands on
-- (the pose's height less its lift and the server's fix: 12 - 0.25 - 0.1).
local floorZ = 11.65
local ox, oy, oz = 16240, 16290, 20
local tx, ty = 16241, 16301
local dx, dy, dz = tx - ox, ty - oy, floorZ - oz
local len = math.sqrt(dx * dx + dy * dy + dz * dz)
PlayerHousing_CursorRay = function() return ox, oy, oz, dx / len, dy / len, dz / len end
-- Upright, it's a figure the mouse goes through: a point there is the wall it's on.
pointed = { 16240.2, 16299.6, 12.6, 0, -1, 0 }   -- on the barrel's near side
local sentBefore = #addonSent
tick(0.2)
assert(#addonSent > sentBefore and addonSent[#addonSent][2]:find("ghost at 16240.20 16299.60 12.60", 1, true),
  "upright: the wall point stands: " .. tostring(addonSent[#addonSent] and addonSent[#addonSent][2]))
PlayerHousingAPI.state.ghostPitch = 15
pointed = { 16240.2, 16299.6, 12.6, 0, -1, 0 }
sentBefore = #addonSent
tick(0.2)
local sentPoint = addonSent[#addonSent] and addonSent[#addonSent][2] or ""
local gx, gy, gz = sentPoint:match("ghost at (%S+) (%S+) (%S+)")
assert(#addonSent > sentBefore and math.abs(tonumber(gx) - tx) < 0.02 and math.abs(tonumber(gy) - ty) < 0.02 and math.abs(tonumber(gz) - floorZ) < 0.02,
  "a point on the held piece goes to the floor under the mouse: " .. sentPoint)
-- Without the ray (an older DLL): such a point isn't used at all.
PlayerHousing_CursorRay = nil
pointed = { 16240.1, 16299.9, 12.7, 0, -1, 0 }
sentBefore = #addonSent
tick(0.2)
assert(#addonSent == sentBefore, "no ray: a point on the held piece is left out")
PlayerHousingAPI.state.ghostPitch = 0
-- A loading screen: nothing traced or moved, and the ghost is forgotten until the server says again.
mouse.scripts.OnEvent(mouse, "PLAYER_LEAVING_WORLD")
local placedBefore = #placedUnits
asked = nil
tick(0.2)
assert(#placedUnits == placedBefore and asked == nil, "nothing during a loading screen")
mouse.scripts.OnEvent(mouse, "PLAYER_ENTERING_WORLD")
tick(0.2)
assert(#placedUnits == placedBefore, "forgotten: the server's next word brings it back")
fire("CHAT_MSG_ADDON", "HOUSING", "state\t1\t1\t13\tBarrel\t5\t200\t1\t10\tmoved Barrel\tKrookowner\t\t0\t0\t0\t0\t0\t0\t0\t1\t0\t\t1\t", "WHISPER", "Krookowner")
PlayerHousing_PlaceUnit = nil
PlayerHousing_CursorWorld = nil
mouseFocus = nil

-- Without the DLL a held piece follows you: a held arrow repeats after a moment, and stops on
-- release; a key left down (the release never came) gives up after a few seconds.
local function click(name, down) _G[name].scripts.OnClick(_G[name], "LeftButton", down) end
local function flush() now = now + 1; OnUpdate(driver, 1) end
fire("CHAT_MSG_ADDON", "HOUSING", GHOST, "WHISPER", "Krookowner")
assert(overrides.UP == "PlayerHousingEditForward", "follow keys without the mouse")
flush()
click("PlayerHousingEditForward", true)
now = now + 0.2; PlayerHousingEditKeys.scripts.OnUpdate(PlayerHousingEditKeys, 0.2)
now = now + 0.2; PlayerHousingEditKeys.scripts.OnUpdate(PlayerHousingEditKeys, 0.2)
now = now + 0.1; PlayerHousingEditKeys.scripts.OnUpdate(PlayerHousingEditKeys, 0.1)
click("PlayerHousingEditForward", false)
now = now + 1; PlayerHousingEditKeys.scripts.OnUpdate(PlayerHousingEditKeys, 1)
OnUpdate(driver, 1)
assert(last() == ".house ghost adjust 0.75 0.00 0.00 0", "held: " .. last())
click("PlayerHousingEditBack", true)
for _ = 1, 200 do now = now + 0.1; PlayerHousingEditKeys.scripts.OnUpdate(PlayerHousingEditKeys, 0.1) end
OnUpdate(driver, 1)
local steps = -tonumber(last():match("adjust (%S+)")) / 0.25
assert(steps > 30 and steps < 70, "stuck key stops: " .. steps)
now = now + 1; PlayerHousingEditKeys.scripts.OnUpdate(PlayerHousingEditKeys, 1); OnUpdate(driver, 1)
local count = #sent; flush(); assert(#sent == count, "nothing more after it gave up")
-- The grid sets the step.
fire("CHAT_MSG_ADDON", "HOUSING", (GHOST:gsub("^(state" .. ("\t[^\t]*"):rep(16) .. ")\t0", "%1\t0.5")), "WHISPER", "Krookowner")
assert(PlayerHousingHeldShape.text:find("Grid 0.5 yd"), "grid shown: " .. PlayerHousingHeldShape.text)
click("PlayerHousingEditLeft", true); click("PlayerHousingEditLeft", false)
flush(); assert(last() == ".house ghost adjust 0.00 0.50 0.00 0", last())
-- Set down in combat: the keys stay until combat is over.
combat = true
fire("CHAT_MSG_ADDON", "HOUSING", "state\t1\t1\t13\tBarrel\t5\t200\t1\t10\tmoved Barrel\tKrookowner\t\t0\t0", "WHISPER", "Krookowner")
assert(overrides.UP, "keys stay in combat")
combat = false
PlayerHousingEditKeys.scripts.OnEvent(PlayerHousingEditKeys, "PLAYER_REGEN_ENABLED")
assert(next(overrides) == nil, "keys cleared after combat")

-- A roommate on someone else's island gets the decorating controls too.
fire("CHAT_MSG_ADDON", "HOUSING", "state\t0\t1\t21\tTiny Table\t7\t200\t1\t10\tnudged Tiny Table\tKrookfriend\t\t0\t0\t0\t1", "WHISPER", "Krookowner")
assert(PlayerHousingButton2.enabled == true, "a roommate can undo")
assert(PlayerHousingFrame and PlayerHousingButton1.text == "Leave")
-- A plain visitor doesn't.
fire("CHAT_MSG_ADDON", "HOUSING", "state\t0\t0\t0\t\t0\t200\t0\t10\t\tKrookfriend\t\t0\t0\t0\t0", "WHISPER", "Krookowner")
assert(not PlayerHousingSelected:IsShown() and PlayerHousingButton2.enabled == false, "a visitor can't undo")
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
for _, command in ipairs(sent) do if command == ".house addon 2" then registrations = registrations + 1 end end
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
assert(first.count.text == 2, "two owned: " .. tostring(first.count.text))
assert(PlayerHousingCollectionSlot2.new.text == "New", "the table is new")
local lockedSlot
for index = 1, 36 do
  local slot = _G["PlayerHousingCollectionSlot" .. index]
  if slot.info and slot.info[1] ~= 901104 and slot.info[1] ~= 901105 and slot.info[1] ~= 901106 then lockedSlot = slot break end
end
assert(lockedSlot and lockedSlot.icon.desaturated, "locked pieces are grey")
assert(PlayerHousingCollectionCount.text:find("^3 of "), PlayerHousingCollectionCount.text)
-- A click pins the piece next to the window, with its details and buttons, and a ghost of it
-- follows you on the island (the server takes one you own, or a new copy).
first:Click("LeftButton")
assert(last() == ".house ghost 901105", last())
assert(PlayerHousingPreview:IsShown() and PlayerHousingPreview.height == 360, "pinned: " .. tostring(PlayerHousingPreview.height))
assert(PlayerHousingDetailsText.text:find("Unlocked"), PlayerHousingDetailsText.text)
assert(PlayerHousingDetailsCounts.text == "Owned: 2   Placed: 1", PlayerHousingDetailsCounts.text)
assert(not PlayerHousingDetailsPlace, "no Place button: the click on the icon is the one way to place")
assert(not PlayerHousingDetailsTake and PlayerHousingDetailsGetOne.enabled and PlayerHousingDetailsGetOne.text == "Buy 1", "buy, no taking out")
PlayerHousingDetailsGetOne.scripts.OnClick()
assert(sent[#sent - 1] == ".house get 901105 1" and last() == ".house data collection", sent[#sent - 1])
PlayerHousingDetailsGetFive.scripts.OnClick()
assert(sent[#sent - 1] == ".house get 901105 5", sent[#sent - 1])
-- Hovering another piece shows it for a moment; moving off brings the pinned one back.
lockedSlot.scripts.OnEnter(lockedSlot)
assert(PlayerHousingPreviewName.text == lockedSlot.info[2] and not PlayerHousingDetailsGetOne:IsShown(), "hovered piece, no buttons")
assert(PlayerHousingPreviewHint.text ~= "", "says how to show it instead")
lockedSlot.scripts.OnLeave(lockedSlot)
assert(PlayerHousingPreview:IsShown() and PlayerHousingPreviewName.text == first.info[2] and PlayerHousingDetailsGetOne:IsShown(),
  "back to the pinned piece: " .. tostring(PlayerHousingPreviewName.text))
-- None to hand: a click starts a ghost all the same (the server buys a copy only when it's set
-- down). In combat too: nothing secure.
local barrelSlot
for index = 1, 36 do
  local slot = _G["PlayerHousingCollectionSlot" .. index]
  if slot.info and slot.info[1] == 901106 then barrelSlot = slot end
end
combat = true
count = #sent
barrelSlot:Click("LeftButton")
assert(#sent == count + 1 and last() == ".house ghost 901106", "a ghost at once: " .. last())
first:Click("LeftButton")
assert(last() == ".house ghost 901105", "in combat too: " .. last())
combat = false
-- Off the island, a click only shows it.
fire("CHAT_MSG_ADDON", "HOUSING", "state\t0\t0\t0\t\t0\t200\t0\t10\t\t\t\t0", "WHISPER", "Krookowner")
count = #sent
first:Click("LeftButton")
assert(#sent == count, "off the island: nothing sent")
fire("CHAT_MSG_ADDON", "HOUSING", "state\t1\t1\t13\tBarrel\t5\t200\t1\t10\tmoved Barrel\tKrookowner\t\t0\t0", "WHISPER", "Krookowner")
-- A locked piece says how to unlock it, and can't be had yet.
count = #sent
lockedSlot:Click("LeftButton")
assert(#sent == count, "locked: nothing sent")
assert(PlayerHousingDetailsText.text:find("Locked") and not PlayerHousingDetailsGetOne.enabled, PlayerHousingDetailsText.text)
-- Close unpins.
PlayerHousingPreviewClose.scripts.OnClick()
assert(not PlayerHousingPreview:IsShown(), "closed")
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

-- Placed: nearest first; Move puts it on the mouse (the way to move a building), Put away
-- straight away. Leaving the Collection marks the new pieces seen.
PlayerHousingTabPlaced.scripts.OnClick()
assert(sent[#sent - 1] == ".house seen" and last() == ".house data placed", sent[#sent - 1] .. " " .. last())
rows("placed", { "3\t901105\t2.5", "7\t902200\t9.1" })
assert(PlayerHousingPlacedPanelRow1Text.text:find("Westfall Chair") and PlayerHousingPlacedPanelRow2Text.text:find("Broken Cart"))
PlayerHousingPlacedPanelRow1Button1.scripts.OnClick()
assert(sent[#sent - 1] == ".house select 3" and last() == ".house ghost move 3", sent[#sent - 1] .. " " .. last())
PlayerHousingPlacedPanelRow1Button2.scripts.OnClick(); assert(last() == ".house goto 3", last())
PlayerHousingPlacedPanelRow1Button3.scripts.OnClick(); assert(last() == ".house pickup 3", last())
assert(not PlayerHousingPlacedPanelRow1Button4, "no Select, + or Here: one way to move")
local placedPopups = #popups
PlayerHousingPlacedPanelRow2Button3.scripts.OnClick()
assert(#popups == placedPopups and last():match("^%.house pickup %d+$"), "the building too, straight away: " .. last())
-- Find a piece by name.
PlayerHousingPlacedSearch.text = "cart"
PlayerHousingPlacedSearch.scripts.OnTextChanged(PlayerHousingPlacedSearch)
assert(PlayerHousingPlacedPanelRow1Text.text:find("Broken Cart") and not PlayerHousingPlacedPanelRow2:IsShown(), "search the placed pieces")
PlayerHousingPlacedSearch.text = ""
PlayerHousingPlacedSearch.scripts.OnTextChanged(PlayerHousingPlacedSearch)
-- Placing or picking up elsewhere refreshes the list.
fire("CHAT_MSG_ADDON", "HOUSING", "state\t1\t1\t13\tBarrel\t4\t200\t1\t10\tpicked up Barrel\tKrookowner\t\t0\t0", "WHISPER", "Krookowner")
assert(last() == ".house data placed", "placed list follows the counts: " .. last())

-- Layouts: each says what setting it out would still need; the rest is under More.
PlayerHousingTabLayouts.scripts.OnClick()
assert(last() == ".house data layouts", last())
rows("layouts", { "limit\t5\t1", "layout\t3\tSummer house\t12\t2026-09-28\t\t2\t300\t1" })
assert(PlayerHousingLayoutsPanelRow1Text.text:find("Summer house") and PlayerHousingLayoutsPanelRow1Text.text:find("12 pieces")
  and PlayerHousingLayoutsPanelRow1Text.text:find("needs 2"), PlayerHousingLayoutsPanelRow1Text.text)
assert(PlayerHousingLayoutCopyable.checked == true, "visitors may copy it, as the server says")
PlayerHousingLayoutCopyable.checked = false
PlayerHousingLayoutCopyable.scripts.OnClick(PlayerHousingLayoutCopyable)
assert(sent[#sent - 1] == ".house layout copyable off", sent[#sent - 1])
PlayerHousingLayoutsPanelRow1Button1.scripts.OnClick()
StaticPopupDialogs.PLAYERHOUSING_LAYOUT_LOAD.OnAccept({}, 3)
assert(sent[#sent - 1] == ".house layout load 3", sent[#sent - 1])
PlayerHousingLayoutsPanelRow1Button2.scripts.OnClick()
local function menuItem(prefix)
  for _, item in ipairs(lastMenu or {}) do if item.text and item.text:find(prefix, 1, true) == 1 then return item end end
  error("no menu item " .. prefix)
end
assert(menuItem("Buy the 2 pieces it needs (300c)"), "buying what's missing, with its price")
menuItem("Buy the 2"):func()
assert(popups[#popups][1] == "PLAYERHOUSING_LAYOUT_MISSING" and popups[#popups][2]:find("300c"), tostring(popups[#popups][2]))
StaticPopupDialogs.PLAYERHOUSING_LAYOUT_MISSING.OnAccept({}, 3)
assert(sent[#sent - 1] == ".house layout missing 3", sent[#sent - 1])
menuItem("Save the island over it"):func()
StaticPopupDialogs.PLAYERHOUSING_LAYOUT_OVERWRITE.OnAccept({}, 3)
assert(sent[#sent - 1] == ".house layout overwrite 3", sent[#sent - 1])
menuItem("Rename"):func()
StaticPopupDialogs.PLAYERHOUSING_LAYOUT_RENAME.OnAccept({ editBox = { GetText = function() return " Autumn house " end } }, 3)
assert(sent[#sent - 1] == ".house layout rename 3 Autumn house", sent[#sent - 1])
menuItem("Send to someone"):func()
StaticPopupDialogs.PLAYERHOUSING_LAYOUT_SEND.OnAccept({ editBox = { GetText = function() return " Krookfriend " end } }, 3)
assert(last() == ".house layout send 3 Krookfriend", last())
menuItem("Delete"):func()
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
-- Pack up (asked first), unstuck and a new key: the old House Key menu's other things.
PlayerHousingPackUp.scripts.OnClick()
assert(popups[#popups][1] == "PLAYERHOUSING_PACKUP", "pack up asks first")
StaticPopupDialogs.PLAYERHOUSING_PACKUP.OnAccept()
assert(last() == ".house packup", last())
PlayerHousingUnstuck.scripts.OnClick(); assert(last() == ".house unstuck", last())
PlayerHousingCallKrook.scripts.OnClick(); assert(last() == ".house krook", last())
assert(not PlayerHousingNewKey, "a new House Key comes from Krook, not the window")
PlayerHousingGuestbookButton.scripts.OnClick()
assert(PlayerHousingGuestsPanel:IsShown() and last() == ".house data guestbook", "the guestbook from the Island tab: " .. last())

-- The guestbook: the notes, a new one marked, the whole note on hover, thrown out.
-- A note too long for one message comes in pieces ("more" rows) and reads whole.
rows("guestbook", { "note\t7\tKrookfriend\t2026-09-29 10:15\t1\tWhat a lovely island!", "note\t5\tKrookother\t2026-09-20 18:02\t0\tNice, ",
  "more\t5\tand the lanterns ", "more\t5\tare perfect." })
assert(PlayerHousingGuestbookPageRow1Text.text:find("new") and PlayerHousingGuestbookPageRow1Text.text:find("lovely island")
  and not PlayerHousingGuestsPanelRow1:IsShown(), PlayerHousingGuestbookPageRow1Text.text)
assert(PlayerHousingGuestbookPageRow2Text.text:find("Nice, and the lanterns are perfect%."), "the long note joined up: " .. PlayerHousingGuestbookPageRow2Text.text)
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

-- Visiting: sign the island's guestbook from the Visit tab, or save a copy of its layout.
fire("CHAT_MSG_ADDON", "HOUSING", "state\t0\t0\t0\t\t0\t200\t0\t10\t\tKrookfriend\t\t0\t0\t0\t0", "WHISPER", "Krookowner")
PlayerHousingTabVisit.scripts.OnClick()
assert(PlayerHousingSign:IsShown() and PlayerHousingLike:IsShown() and PlayerHousingCopyLayout:IsShown(), "signing while visiting")
PlayerHousingCopyLayout.scripts.OnClick(); assert(last() == ".house layout copy", last())
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
PlayerHousingButton2.scripts.OnClick(PlayerHousingButton2, "RightButton")
assert(PlayerHousingHistory:IsShown() and last() == ".house data history", last())
rows("history", { "undo\tmoved Barrel", "undo\tplaced Westfall Chair", "undo\tturned Tiny Table 45° left", "redo\tpicked up Lantern" })
assert(PlayerHousingHistoryRow1Text.text == "1. moved Barrel" and PlayerHousingHistoryRow3:IsShown() and not PlayerHousingHistoryRow4:IsShown(),
  PlayerHousingHistoryRow1Text.text)
PlayerHousingHistoryRow2.scripts.OnEnter(PlayerHousingHistoryRow2)
assert(PlayerHousingHistoryRow1.highlighted and PlayerHousingHistoryRow2.highlighted and not PlayerHousingHistoryRow3.highlighted, "what goes")
PlayerHousingHistoryRow2.scripts.OnClick(PlayerHousingHistoryRow2)
assert(last() == ".house undo 2" and not PlayerHousingHistory:IsShown(), last())
PlayerHousingButton2.scripts.OnClick(PlayerHousingButton2, "LeftButton")
assert(last() == ".house undo", last())

-- Several pieces selected (Ctrl-right-click), none held: what can be done with them together.
local GROUP = "state\t1\t1\t13\tBarrel\t5\t200\t1\t10\tmoved Barrel\tKrookowner\t\t0\t0\t0\t0\t0\t0\t0\t3"
fire("CHAT_MSG_ADDON", "HOUSING", GROUP, "WHISPER", "Krookowner")
assert(PlayerHousingSelected:IsShown() and PlayerHousingHeldText.text:find("3 pieces"), tostring(PlayerHousingHeldText.text))
assert(PlayerHousingHeldHelp.text:find("Right%-click|r one of them"), PlayerHousingHeldHelp.text)
SEL("Save as a set").scripts.OnClick()
assert(popups[#popups][1] == "PLAYERHOUSING_SAVE_SET")
StaticPopupDialogs.PLAYERHOUSING_SAVE_SET.OnAccept({ editBox = { GetText = function() return " Dining " end } })
assert(last() == ".house set save Dining", last())
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

-- The minimap button: click for the window, drag round the edge.
assert(PlayerHousingMinimapButton:IsShown(), "minimap button")
local shownBefore = PlayerHousingFrame:IsShown()
PlayerHousingMinimapButton.scripts.OnClick(PlayerHousingMinimapButton, "LeftButton")
assert(PlayerHousingFrame:IsShown() ~= shownBefore, "toggles the window")
PlayerHousingMinimapButton.scripts.OnClick(PlayerHousingMinimapButton, "LeftButton")
cursorX, cursorY = 100, 180
PlayerHousingMinimapButton.scripts.OnDragStart(PlayerHousingMinimapButton)
PlayerHousingMinimapButton.scripts.OnUpdate(PlayerHousingMinimapButton, 0.1)
PlayerHousingMinimapButton.scripts.OnDragStop(PlayerHousingMinimapButton)
assert(math.abs(PlayerHousingDB.minimapAngle - 90) < 0.01, "dragged to the top: " .. PlayerHousingDB.minimapAngle)
SlashCmdList.PLAYERHOUSING("minimap")
assert(not PlayerHousingMinimapButton:IsShown() and PlayerHousingDB.minimapHidden, "hidden")
SlashCmdList.PLAYERHOUSING("minimap")
assert(PlayerHousingMinimapButton:IsShown(), "back")

-- /housing help: how it works, in the chat.
local printedCount = #printed
SlashCmdList.PLAYERHOUSING("help")
assert(#printed > printedCount and printed[#printed]:find("%.house help"), printed[#printed])

-- A mannequin picked up: Dress... opens its sheet, gear in slots like the character sheet's.
fire("CHAT_MSG_ADDON", "HOUSING", "state\t1\t1\t21\tMannequin\t5\t200\t1\t10\tplaced Mannequin\tKrookowner\t\t0\t0\t0\t0\t0\t0\t0\t1\t902300\tmove\t1\t\t1\t100\t0\t0\t1",
  "WHISPER", "Krookowner")
SEL("Dress...").scripts.OnClick()
assert(PlayerHousingDress:IsShown() and last() == ".house data stand 21", "the sheet asks for the stand: " .. last())
assert(UISpecialFrames[#UISpecialFrames] == "PlayerHousingDress", "Escape closes the sheet")
-- look: a Dwarf (3) woman (256).
rows("stand", { "figure\tDwarf woman", "look\t259", "worn\t15\tmain hand\t25\tWorn Shortsword", "worn\t6\tlegs\t39\tRecruit's Pants" }, 21)
assert(PlayerHousingDressFigure.text == "Dwarf woman", tostring(PlayerHousingDressFigure.text))
assert(PlayerHousingDressMainHandSlot.entry == 25 and PlayerHousingDressLegsSlot.entry == 39 and not PlayerHousingDressHeadSlot.entry, "worn in slots")
-- Dragged on from the bags: the server finds the slot.
cursorItem = 2092
PlayerHousingDressSecondaryHandSlot.scripts.OnReceiveDrag(PlayerHousingDressSecondaryHandSlot)
assert(sent[#sent - 1] == ".house stand dress 2092 21" and last() == ".house data stand 21" and not cursorItem, sent[#sent - 1])
-- Dragged off, or right-clicked: back to the bags. An empty slot does nothing.
PlayerHousingDressMainHandSlot.scripts.OnDragStart(PlayerHousingDressMainHandSlot)
assert(sent[#sent - 1] == ".house stand undress 15 21", sent[#sent - 1])
PlayerHousingDressLegsSlot.scripts.OnClick(PlayerHousingDressLegsSlot, "RightButton")
assert(sent[#sent - 1] == ".house stand undress 6 21", sent[#sent - 1])
count = #sent
PlayerHousingDressHeadSlot.scripts.OnClick(PlayerHousingDressHeadSlot, "RightButton")
assert(#sent == count, "an empty slot: nothing")
-- Race, man or woman, a new look, trading gear; no poses (version 2).
local function DressButton(text)
  for _, f in ipairs(frames) do if f.parent == PlayerHousingDress and f.text == text then return f end end
end
assert(DressButton("Dwarf"), "the race button names it")
DressButton("Dwarf").scripts.OnClick(DressButton("Dwarf"))
assert(#lastMenu == 10, "ten races")
lastMenu[5].func()
assert(sent[#sent - 1] == ".house stand look 11 female 21", sent[#sent - 1])
DressButton("Man").scripts.OnClick(DressButton("Man"))
assert(sent[#sent - 1] == ".house stand look 3 male 21", sent[#sent - 1])
assert(not DressButton("Pose"), "no poses for now")
DressButton("Trade gear").scripts.OnClick(DressButton("Trade gear"))
assert(sent[#sent - 1] == ".house stand trade 21", sent[#sent - 1])
-- A list for another stand is ignored; selecting something else closes it.
rows("stand", { "figure\tOrc man" }, 99)
assert(PlayerHousingDressFigure.text == "Dwarf woman", "another stand's list ignored")
fire("CHAT_MSG_ADDON", "HOUSING", "state\t1\t1\t13\tBarrel\t5\t200\t1\t10\tmoved Barrel\tKrookowner\t\t0\t0", "WHISPER", "Krookowner")
assert(not PlayerHousingDress:IsShown(), "closed when the mannequin isn't selected")

assert(not PlayerHousingPoser, "a left-click on a mannequin does nothing (no poses for now)")

-- Picking a piece up with the window open: the window steps aside, the held panel stays at the
-- bottom of the screen. Set down or put back (Escape), the window comes back.
assert(UISpecialFrames[1] == "PlayerHousingFrame", "Escape closes the window")
PlayerHousingFrame:Show()
fire("CHAT_MSG_ADDON", "HOUSING", "state\t1\t1\t13\tBarrel\t5\t200\t1\t10\t\tKrookowner\t\t0\t0\t0\t0\t0\t0\t0\t0\t902101\t\t1\t\t0\t100\t0\t0\t1",
  "WHISPER", "Krookowner")
assert(not PlayerHousingFrame:IsShown() and PlayerHousingSelected:GetParent() == UIParent and PlayerHousingSelected:IsShown(), "out of the way")
fire("CHAT_MSG_ADDON", "HOUSING", "state\t1\t1\t13\tBarrel\t5\t200\t1\t10\t\tKrookowner\t\t0\t0", "WHISPER", "Krookowner")
assert(PlayerHousingFrame:IsShown() and PlayerHousingSelected:GetParent() == PlayerHousingFrame, "back")
-- Picked up with the window closed: it stays closed afterwards.
PlayerHousingFrame:Hide()
fire("CHAT_MSG_ADDON", "HOUSING", "state\t1\t1\t13\tBarrel\t5\t200\t1\t10\t\tKrookowner\t\t0\t0\t0\t0\t0\t0\t0\t0\t902101\t\t1\t\t0\t100\t0\t0\t1",
  "WHISPER", "Krookowner")
assert(PlayerHousingSelected:GetParent() == UIParent, "held panel docked")
fire("CHAT_MSG_ADDON", "HOUSING", "state\t1\t1\t13\tBarrel\t5\t200\t1\t10\t\tKrookowner\t\t0\t0", "WHISPER", "Krookowner")
assert(not PlayerHousingFrame:IsShown(), "stays closed")
PlayerHousingFrame:Show()
-- The window open on your island is decorating (chairs and chests pick up); closed, they work
-- again, once nothing is held. Asked once each way.
PlayerHousingFrame:Hide()
fire("CHAT_MSG_ADDON", "HOUSING", "state\t1\t0\t0\t\t5\t200\t1\t10\t\tKrookowner\t\t0\t0", "WHISPER", "Krookowner")
count = #sent
PlayerHousingFrame:Show()
local asked = 0
for index = count + 1, #sent do if sent[index] == ".house decorate on" then asked = asked + 1 end end
assert(asked == 1, "window open: decorating: " .. last())
count = #sent
fire("CHAT_MSG_ADDON", "HOUSING", "state\t1\t0\t0\t\t5\t200\t1\t10\t\tKrookowner\t\t0\t0", "WHISPER", "Krookowner")
assert(#sent == count or last() ~= ".house decorate on", "asked once")
fire("CHAT_MSG_ADDON", "HOUSING", "state\t1\t1\t0\t\t5\t200\t1\t10\t\tKrookowner\t\t0\t0", "WHISPER", "Krookowner")
PlayerHousingFrame:Hide()
assert(last() == ".house decorate off", "window closed: not decorating: " .. last())
-- The preview tour: each shot shown as told, coded in the strip, then a screenshot.
PlayerHousing_PreviewTask = { run = "t1", shots = { { 901105, 0, 1, 0, 0.785 }, { 901105, 1, 0.6, 0, 0.785 } } }
local shotsBefore = screenshots
PlayerHousingAPI.PreviewTour()
local tourFrame = PlayerHousingPreviewTour
local function tourTick(seconds) now = now + seconds; tourFrame.scripts.OnUpdate(tourFrame, seconds) end
tourTick(1.1)   -- shows the first
assert(PlayerHousingPreviewModel.modelPath == PlayerHousing_Models[901105][1] and PlayerHousingPreviewTourBlack:IsShown()
  and not PlayerHousingPreviewReset:IsShown(), "first shot shown on black")
tourTick(0.7)   -- shoots
assert(screenshots == shotsBefore + 1, "one screenshot")
for _ = 1, 6 do tourTick(0.8) end
assert(screenshots == shotsBefore + 2 and PlayerHousingDB.previewTourDone == "t1" and not PlayerHousingPreviewTourBlack:IsShown()
  and PlayerHousingPreviewReset:IsShown(), "two shots, done, back to normal")
PlayerHousing_PreviewTask = nil
OnUpdate(driver, 1)
print("sent:", table.concat(sent, " | "))
print("ALL ADDON CHECKS PASSED")
