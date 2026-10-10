-- A small point-and-click adventure engine for LÖVE 11.
-- Left click walks and uses, right click looks. F2 toggles the editor, F5 saves, F9 loads.
local adv = {
    width = 640,
    height = 360,
    barHeight = 52,
    rooms = {},
    items = {},
    flags = {},
    inventory = {},
    player = { x = 320, y = 280, speed = 140, facing = 1 },
}

local unpack = table.unpack or unpack
local canvas, font, bigFont, chime
local scale, offsetX, offsetY = 1, 0, 0
local mouseX, mouseY = 0, 0
local room, held, label, speech, notice, ending
local editing, dragStart = false, nil
local scripts, running = {}, nil

function adv.room(id)
    return function(def)
        def.id = id
        def.hotspots = def.hotspots or {}
        def.walk = def.walk or { { x = 0, y = 0, w = adv.width, h = adv.height - adv.barHeight } }
        adv.rooms[id] = def
        return def
    end
end

function adv.item(id)
    return function(def)
        def.id = id
        adv.items[id] = def
        return def
    end
end

local function wait(done)
    if running then
        coroutine.yield(done)
    end
end

function adv.run(fn, ...)
    local args = { ... }
    table.insert(scripts, { co = coroutine.create(function() fn(unpack(args)) end) })
end

local function stepScripts()
    local i = 1
    while i <= #scripts do
        local s = scripts[i]
        if not s.done or s.done() then
            running = s
            local ok, result = coroutine.resume(s.co)
            running = nil
            if not ok then
                error(result, 0)
            end
            s.done = result
        end
        if coroutine.status(s.co) == "dead" then
            table.remove(scripts, i)
        else
            i = i + 1
        end
    end
end

function adv.busy()
    return #scripts > 0
end

function adv.say(text, who)
    local line = { text = text, time = 1.2 + #text * 0.055, who = who }
    speech = line
    wait(function() return speech ~= line end)
end

function adv.pause(seconds)
    local untilTime = love.timer.getTime() + seconds
    wait(function() return love.timer.getTime() >= untilTime end)
end

function adv.notice(text)
    notice = { text = text, time = 3 }
end

function adv.depthScale(y)
    local d = room and room.depth
    if not d then
        return 1
    end
    local t = math.max(0, math.min(1, (y - d.top) / (d.bottom - d.top)))
    return d.far + (d.near - d.far) * t
end

local function clampToFloor(x, y)
    local best, bestX, bestY = math.huge, x, y
    for _, r in ipairs(room.walk) do
        local cx = math.max(r.x, math.min(x, r.x + r.w))
        local cy = math.max(r.y, math.min(y, r.y + r.h))
        local d = (cx - x) ^ 2 + (cy - y) ^ 2
        if d < best then
            best, bestX, bestY = d, cx, cy
        end
    end
    return bestX, bestY
end

function adv.walk(x, y)
    local p = adv.player
    p.tx, p.ty = clampToFloor(x, y)
    wait(function() return p.tx == nil end)
end

function adv.face(x)
    adv.player.facing = x < adv.player.x and -1 or 1
end

function adv.go(id, x, y)
    room = assert(adv.rooms[id], "there is no room called " .. tostring(id))
    local p = adv.player
    p.x, p.y = clampToFloor(x or p.x, y or p.y)
    p.tx, p.ty = nil, nil
    held = nil
    if room.enter then
        adv.run(room.enter)
    end
end

function adv.currentRoom()
    return room
end

function adv.has(id)
    for _, v in ipairs(adv.inventory) do
        if v == id then
            return true
        end
    end
    return false
end

function adv.take(id)
    assert(adv.items[id], "there is no item called " .. tostring(id))
    if not adv.has(id) then
        table.insert(adv.inventory, id)
        if chime then
            chime:stop()
            chime:play()
        end
    end
end

function adv.drop(id)
    for i, v in ipairs(adv.inventory) do
        if v == id then
            table.remove(adv.inventory, i)
            break
        end
    end
    if held and held.id == id then
        held = nil
    end
end

function adv.theEnd(text)
    ending = text
end

local function respond(reaction, ...)
    if type(reaction) == "function" then
        reaction(...)
    elseif reaction then
        adv.say(reaction)
    end
end

local function present(spot)
    return spot.when == nil or spot.when()
end

local function hotspotAt(x, y)
    for i = #room.hotspots, 1, -1 do
        local s = room.hotspots[i]
        if present(s) and x >= s.x and x < s.x + s.w and y >= s.y and y < s.y + s.h then
            return s
        end
    end
end

local function slotPosition(i)
    return 8 + (i - 1) * 52, adv.height - adv.barHeight + 4
end

local function slotAt(x, y)
    for i = 1, #adv.inventory do
        local sx, sy = slotPosition(i)
        if x >= sx and x < sx + 44 and y >= sy and y < sy + 44 then
            return i
        end
    end
end

local function interact(spot, item)
    adv.run(function()
        local startRoom = room
        local wx = spot.walkTo and spot.walkTo[1] or spot.x + spot.w / 2
        local wy = spot.walkTo and spot.walkTo[2] or spot.y + spot.h
        adv.walk(wx, wy)
        if room ~= startRoom then
            return
        end
        adv.face(spot.x + spot.w / 2)
        if item then
            respond(spot.items and spot.items[item.id] or "That doesn't work.", spot, item)
        elseif spot.exit then
            adv.go(spot.exit, unpack(spot.entry or {}))
        else
            respond(spot.use or "I can't do anything with that.", spot)
        end
    end)
end

local function look(thing)
    adv.run(function()
        if thing.x then
            adv.face(thing.x + thing.w / 2)
        end
        respond(thing.look or ("Nothing special about the " .. thing.name:lower() .. "."), thing)
    end)
end

local function serialize(v)
    if type(v) == "table" then
        local parts = {}
        for k, x in pairs(v) do
            parts[#parts + 1] = "[" .. serialize(k) .. "]=" .. serialize(x)
        end
        return "{" .. table.concat(parts, ",") .. "}"
    elseif type(v) == "string" then
        return string.format("%q", v)
    end
    return tostring(v)
end

function adv.save()
    local p = adv.player
    local data = { room = room.id, x = p.x, y = p.y, facing = p.facing, flags = adv.flags, inventory = adv.inventory }
    love.filesystem.write("save.lua", "return " .. serialize(data))
    adv.notice("Saved")
end

function adv.restore()
    if not love.filesystem.getInfo("save.lua") then
        adv.notice("There is no saved game yet")
        return
    end
    local data = love.filesystem.load("save.lua")()
    scripts, speech, ending, held = {}, nil, nil, nil
    for k in pairs(adv.flags) do
        adv.flags[k] = nil
    end
    for k, v in pairs(data.flags) do
        adv.flags[k] = v
    end
    for i = #adv.inventory, 1, -1 do
        adv.inventory[i] = nil
    end
    for i, id in ipairs(data.inventory) do
        adv.inventory[i] = id
    end
    room = adv.rooms[data.room]
    local p = adv.player
    p.x, p.y, p.facing, p.tx, p.ty = data.x, data.y, data.facing, nil, nil
    adv.notice("Loaded")
end

local function makeChime()
    local rate = 22050
    local data = love.sound.newSoundData(math.floor(rate * 0.3), rate, 16, 1)
    for i = 0, data:getSampleCount() - 1 do
        local t = i / rate
        local f = t < 0.1 and 880 or 1320
        data:setSample(i, math.sin(2 * math.pi * f * t) * 0.25 * (1 - t / 0.3))
    end
    return love.audio.newSource(data, "static")
end

function adv.init()
    love.graphics.setDefaultFilter("nearest", "nearest")
    canvas = love.graphics.newCanvas(adv.width, adv.height)
    font = love.graphics.newFont(14)
    bigFont = love.graphics.newFont(30)
    love.graphics.setFont(font)
    local ok, source = pcall(makeChime)
    if ok then
        chime = source
    end
end

local function updateLabel()
    label = nil
    if editing then
        label = string.format("%d, %d", mouseX, mouseY)
        return
    end
    if adv.busy() or ending then
        return
    end
    local slot = slotAt(mouseX, mouseY)
    local spot = mouseY < adv.height - adv.barHeight and hotspotAt(mouseX, mouseY)
    local target = slot and adv.items[adv.inventory[slot]] or spot
    if held and target and target ~= held then
        label = "Use " .. held.name .. " with " .. target.name
    elseif held then
        label = "Use " .. held.name .. " with"
    elseif target then
        label = target.name
    end
end

function adv.update(dt)
    local w, h = love.graphics.getDimensions()
    scale = math.min(w / adv.width, h / adv.height)
    offsetX, offsetY = (w - adv.width * scale) / 2, (h - adv.height * scale) / 2
    local mx, my = love.mouse.getPosition()
    mouseX, mouseY = (mx - offsetX) / scale, (my - offsetY) / scale

    local p = adv.player
    p.walking = p.tx ~= nil
    if p.tx then
        local dx, dy = p.tx - p.x, p.ty - p.y
        local dist = math.sqrt(dx * dx + dy * dy)
        local step = p.speed * dt * adv.depthScale(p.y)
        if math.abs(dx) > 1 then
            p.facing = dx > 0 and 1 or -1
        end
        if dist <= step then
            p.x, p.y, p.tx, p.ty = p.tx, p.ty, nil, nil
        else
            p.x, p.y = p.x + dx / dist * step, p.y + dy / dist * step
        end
    end
    if speech then
        speech.time = speech.time - dt
        if speech.time <= 0 then
            speech = nil
        end
    end
    if notice then
        notice.time = notice.time - dt
        if notice.time <= 0 then
            notice = nil
        end
    end
    if room.update then
        room.update(dt)
    end
    stepScripts()
    updateLabel()
end

function adv.mousepressed(x, y, button)
    x, y = (x - offsetX) / scale, (y - offsetY) / scale
    if ending then
        return
    end
    if editing then
        if button == 1 then
            dragStart = { math.floor(x), math.floor(y) }
        end
        return
    end
    if speech then
        speech = nil
        return
    end
    if adv.busy() then
        return
    end
    if y >= adv.height - adv.barHeight then
        local slot = slotAt(x, y)
        local item = slot and adv.items[adv.inventory[slot]]
        if not item then
            held = nil
        elseif button == 2 then
            held = nil
            look(item)
        elseif held and held ~= item then
            local a = held
            held = nil
            adv.run(function()
                respond(a.items and a.items[item.id] or item.items and item.items[a.id] or "That doesn't work.", a, item)
            end)
        else
            held = held ~= item and item or nil
        end
        return
    end
    local spot = hotspotAt(x, y)
    local item = held
    held = nil
    if button == 2 then
        if spot then
            look(spot)
        end
    elseif spot then
        interact(spot, item)
    else
        local p = adv.player
        p.tx, p.ty = clampToFloor(x, y)
    end
end

function adv.mousereleased(x, y, button)
    if not (editing and dragStart and button == 1) then
        return
    end
    x, y = math.floor((x - offsetX) / scale), math.floor((y - offsetY) / scale)
    local x0, y0 = math.min(dragStart[1], x), math.min(dragStart[2], y)
    local w, h = math.abs(x - dragStart[1]), math.abs(y - dragStart[2])
    dragStart = nil
    local text
    if w > 2 and h > 2 then
        text = string.format("x = %d, y = %d, w = %d, h = %d", x0, y0, w, h)
    else
        text = string.format("%d, %d", x, y)
    end
    love.system.setClipboardText(text)
    print(text)
    adv.notice("Copied " .. text)
end

function adv.keypressed(key)
    if key == "escape" then
        love.event.quit()
    elseif key == "f2" then
        editing = not editing
        dragStart = nil
    elseif key == "f5" then
        adv.save()
    elseif key == "f9" then
        adv.restore()
    elseif key == "f11" or (key == "return" and love.keyboard.isDown("lalt", "ralt")) then
        love.window.setFullscreen(not love.window.getFullscreen())
    end
end

local function outlined(text, x, y, width, align, color)
    love.graphics.setColor(0, 0, 0)
    for dx = -1, 1 do
        for dy = -1, 1 do
            if dx ~= 0 or dy ~= 0 then
                love.graphics.printf(text, x + dx, y + dy, width, align)
            end
        end
    end
    love.graphics.setColor(color or { 1, 1, 1 })
    love.graphics.printf(text, x, y, width, align)
end

local function drawSpeech()
    local p = adv.player
    local who = speech.who or {}
    local x = who.x or p.x
    local top = who.y or (p.y - 100 * adv.depthScale(p.y))
    local width = 280
    local _, lines = font:getWrap(speech.text, width)
    local y = math.max(4, top - #lines * font:getHeight())
    x = math.max(4, math.min(adv.width - width - 4, x - width / 2))
    outlined(speech.text, x, y, width, "center", who.color or { 1, 1, 0.6 })
end

local function drawBar()
    local top = adv.height - adv.barHeight
    love.graphics.setColor(0.12, 0.1, 0.14)
    love.graphics.rectangle("fill", 0, top, adv.width, adv.barHeight)
    for i, id in ipairs(adv.inventory) do
        local x, y = slotPosition(i)
        local item = adv.items[id]
        love.graphics.setColor(item == held and { 0.45, 0.4, 0.25 } or { 0.22, 0.2, 0.25 })
        love.graphics.rectangle("fill", x, y, 44, 44, 4)
        if item ~= held then
            love.graphics.setColor(1, 1, 1)
            item.draw(x + 22, y + 22)
        end
    end
end

local function drawEditor()
    love.graphics.setLineWidth(1)
    for _, r in ipairs(room.walk) do
        love.graphics.setColor(0.2, 1, 0.3, 0.8)
        love.graphics.rectangle("line", r.x + 0.5, r.y + 0.5, r.w, r.h)
    end
    for _, s in ipairs(room.hotspots) do
        love.graphics.setColor(1, 0.9, 0.2, present(s) and 0.9 or 0.35)
        love.graphics.rectangle("line", s.x + 0.5, s.y + 0.5, s.w, s.h)
        love.graphics.print(s.name, s.x + 2, s.y + 1)
    end
    if dragStart then
        love.graphics.setColor(0.3, 0.9, 1)
        love.graphics.rectangle("line", dragStart[1] + 0.5, dragStart[2] + 0.5, mouseX - dragStart[1], mouseY - dragStart[2])
    end
    outlined("Editor: drag to copy a rectangle, click to copy a point, F2 to play", 0, adv.height - adv.barHeight - 20, adv.width, "center", { 0.6, 1, 1 })
end

function adv.draw()
    love.graphics.setCanvas(canvas)
    love.graphics.clear(0, 0, 0)
    love.graphics.setColor(1, 1, 1)
    room.draw()
    local p = adv.player
    love.graphics.setColor(1, 1, 1)
    p.draw(p.x, p.y, adv.depthScale(p.y), p.facing, p.walking)
    if room.drawFront then
        love.graphics.setColor(1, 1, 1)
        room.drawFront()
    end
    drawBar()
    if editing then
        drawEditor()
    end
    if speech then
        drawSpeech()
    end
    if label then
        outlined(label, 0, 6, adv.width, "center")
    end
    if notice then
        outlined(notice.text, 0, adv.height - adv.barHeight - 40, adv.width - 8, "right", { 0.6, 1, 0.6 })
    end
    if held then
        love.graphics.setColor(1, 1, 1)
        held.draw(mouseX + 14, mouseY + 14)
    end
    if ending then
        love.graphics.setColor(0, 0, 0, 0.75)
        love.graphics.rectangle("fill", 0, 0, adv.width, adv.height)
        love.graphics.setFont(bigFont)
        local _, lines = bigFont:getWrap(ending, adv.width - 40)
        outlined(ending, 20, (adv.height - #lines * bigFont:getHeight()) / 2, adv.width - 40, "center", { 1, 0.85, 0.4 })
        love.graphics.setFont(font)
    end
    love.graphics.setCanvas()
    love.graphics.clear(0, 0, 0)
    love.graphics.setColor(1, 1, 1)
    love.graphics.draw(canvas, offsetX, offsetY, 0, scale, scale)
end

return adv
