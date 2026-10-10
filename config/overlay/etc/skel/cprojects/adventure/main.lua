-- The Tiny Hat: a point-and-click adventure in LÖVE. Press Ctrl+Space twice to run it.
-- Rooms, hotspots and items are plain Lua tables, and adventure.lua is the engine.
-- In the game: left click walks and uses, right click looks, F2 shows the hotspots and copies coordinates.
local adv = require("adventure")
local flags = adv.flags

local function rect(r, g, b, x, y, w, h)
    love.graphics.setColor(r, g, b)
    love.graphics.rectangle("fill", x, y, w, h)
end

local function circle(r, g, b, x, y, radius)
    love.graphics.setColor(r, g, b)
    love.graphics.circle("fill", x, y, radius)
end

local function drawHat(x, y, s)
    s = s or 1
    rect(0.75, 0.2, 0.17, x - 14 * s, y - 4 * s, 28 * s, 5 * s)
    rect(0.75, 0.2, 0.17, x - 8 * s, y - 20 * s, 16 * s, 17 * s)
    rect(0.15, 0.6, 0.6, x - 8 * s, y - 8 * s, 16 * s, 4 * s)
end

function adv.player.draw(x, y, s, facing, walking)
    local step = walking and math.sin(love.timer.getTime() * 12) * 4 or 0
    love.graphics.push()
    love.graphics.translate(x, y)
    love.graphics.scale(s * facing, s)
    rect(0.2, 0.22, 0.35, -9 + step, -30, 7, 30)
    rect(0.2, 0.22, 0.35, 2 - step, -30, 7, 30)
    rect(0.25, 0.55, 0.3, -12, -66, 24, 38)
    rect(0.95, 0.75, 0.6, 9, -60, 6, 20)
    circle(0.95, 0.75, 0.6, 0, -78, 13)
    circle(0.1, 0.1, 0.1, 6, -80, 2)
    rect(0.4, 0.25, 0.1, -13, -92, 22, 8)
    if flags.hatOn then
        drawHat(0, -88, 1.1)
    end
    love.graphics.pop()
end

local crow = { x = 320, y = 130, color = { 0.7, 0.85, 1 } }

local function drawCrow(x, y)
    circle(0.1, 0.1, 0.12, x, y, 11)
    circle(0.1, 0.1, 0.12, x + 10, y - 9, 7)
    love.graphics.setColor(0.9, 0.7, 0.1)
    love.graphics.polygon("fill", x + 16, y - 10, x + 25, y - 7, x + 16, y - 6)
    circle(1, 1, 1, x + 12, y - 11, 2)
end

local function drawKey(x, y)
    love.graphics.setColor(0.85, 0.7, 0.2)
    love.graphics.setLineWidth(3)
    love.graphics.circle("line", x - 8, y, 6)
    love.graphics.line(x - 2, y, x + 14, y)
    love.graphics.line(x + 10, y, x + 10, y + 6)
    love.graphics.line(x + 14, y, x + 14, y + 6)
    love.graphics.setLineWidth(1)
end

local function drawCookie(x, y)
    circle(0.75, 0.5, 0.25, x, y, 10)
    circle(0.3, 0.15, 0.05, x - 4, y - 3, 2)
    circle(0.3, 0.15, 0.05, x + 3, y + 2, 2)
    circle(0.3, 0.15, 0.05, x + 4, y - 4, 1.5)
end

adv.item "cookie" {
    name = "Cookie",
    look = "A crunchy cookie. Crows love these.",
    draw = drawCookie,
}

adv.item "key" {
    name = "Key",
    look = "A small brass key.",
    draw = drawKey,
}

adv.room "bedroom" {
    walk = { { x = 20, y = 245, w = 600, h = 55 } },
    depth = { top = 245, bottom = 300, far = 0.85, near = 1 },

    draw = function()
        rect(0.86, 0.78, 0.62, 0, 0, 640, 240)
        rect(0.55, 0.36, 0.22, 0, 240, 640, 68)
        for x = 0, 640, 64 do
            rect(0.48, 0.3, 0.18, x, 240, 2, 68)
        end
        rect(0.45, 0.3, 0.2, 0, 232, 640, 8)
        rect(0.4, 0.7, 0.95, 60, 50, 100, 80)
        circle(1, 0.9, 0.3, 130, 75, 14)
        rect(1, 1, 1, 60, 88, 100, 4)
        rect(1, 1, 1, 108, 50, 4, 80)
        love.graphics.setColor(1, 1, 1)
        love.graphics.setLineWidth(4)
        love.graphics.rectangle("line", 60, 50, 100, 80)
        rect(0.45, 0.25, 0.6, 270, 60, 60, 80)
        love.graphics.setColor(0.95, 0.4, 0.6)
        love.graphics.print("LÖVE", 282, 88)
        rect(0.5, 0.3, 0.15, 30, 200, 180, 50)
        rect(0.95, 0.95, 0.95, 40, 190, 50, 18)
        rect(0.8, 0.25, 0.25, 90, 192, 120, 22)
        rect(0.5, 0.3, 0.15, 220, 195, 40, 50)
        rect(0.4, 0.24, 0.12, 225, 212, 30, 3)
        if not flags.cookieTaken then
            drawCookie(240, 186)
        end
        rect(0.45, 0.28, 0.14, 400, 80, 90, 165)
        if flags.wardrobeOpen then
            rect(0.15, 0.1, 0.08, 406, 86, 78, 150)
            if not flags.hatTaken then
                drawHat(445, 182, 1.3)
            end
        else
            rect(0.3, 0.18, 0.1, 444, 86, 2, 150)
            circle(0.9, 0.8, 0.3, 438, 165, 3)
            circle(0.9, 0.8, 0.3, 452, 165, 3)
        end
        rect(0.55, 0.35, 0.2, 560, 100, 60, 140)
        circle(0.9, 0.8, 0.3, 570, 175, 4)
    end,

    hotspots = {
        { name = "Window", x = 60, y = 50, w = 100, h = 80,
          look = "A sunny day. Perfect weather for wearing a hat.",
          use = "I'd rather use the door." },
        { name = "Poster", x = 270, y = 60, w = 60, h = 80,
          look = "A poster of my favourite game engine.",
          use = "It's stuck to the wall." },
        { name = "Bed", x = 30, y = 190, w = 180, h = 60,
          look = "My bed. No hat under the pillow, I already checked.",
          use = "No time to sleep. My hat is missing!" },
        { name = "Nightstand", x = 220, y = 195, w = 40, h = 50,
          look = "My trusty nightstand.",
          use = "Only socks in the drawer." },
        { name = "Cookie", x = 228, y = 175, w = 24, h = 22,
          when = function() return not flags.cookieTaken end,
          look = "A cookie. Breakfast!",
          use = function()
              flags.cookieTaken = true
              adv.take("cookie")
              adv.say("I'll save it for later.")
          end },
        { name = "Wardrobe", x = 400, y = 80, w = 90, h = 165,
          walkTo = { 380, 268 },
          look = function()
              if flags.wardrobeOpen then
                  adv.say("An open wardrobe.")
              else
                  adv.say("My wardrobe. I keep it locked, but I lost the key.")
              end
          end,
          use = function()
              if flags.wardrobeOpen then
                  adv.say("It's already open.")
              else
                  adv.say("It's locked.")
              end
          end,
          items = {
              key = function()
                  flags.wardrobeOpen = true
                  adv.drop("key")
                  adv.say("Click! It's open.")
              end,
          } },
        { name = "Tiny hat", x = 425, y = 158, w = 40, h = 30,
          walkTo = { 380, 268 },
          when = function() return flags.wardrobeOpen and not flags.hatTaken end,
          look = "There it is! My tiny hat!",
          use = function()
              flags.hatTaken = true
              flags.hatOn = true
              adv.say("Found it! I feel complete again.")
              adv.pause(0.5)
              adv.theEnd("The End\n\nThanks for playing!")
          end },
        { name = "Door to the garden", x = 560, y = 100, w = 60, h = 140,
          exit = "garden", entry = { 90, 270 } },
    },

    enter = function()
        if not flags.started then
            flags.started = true
            adv.pause(0.5)
            adv.say("Good morning!")
            adv.say("Wait... where is my tiny hat?")
        end
    end,
}

adv.room "garden" {
    walk = { { x = 40, y = 245, w = 580, h = 55 } },
    depth = { top = 245, bottom = 300, far = 0.85, near = 1 },

    draw = function()
        rect(0.55, 0.8, 1, 0, 0, 640, 230)
        circle(1, 0.95, 0.5, 560, 50, 26)
        circle(1, 1, 1, 200, 60, 20)
        circle(1, 1, 1, 225, 55, 26)
        circle(1, 1, 1, 252, 62, 18)
        rect(0.35, 0.65, 0.25, 0, 230, 640, 78)
        rect(0.86, 0.78, 0.62, 0, 40, 110, 200)
        rect(0.55, 0.35, 0.2, 30, 100, 60, 140)
        for x = 150, 640, 40 do
            rect(0.95, 0.95, 0.9, x, 175, 10, 62)
        end
        rect(0.95, 0.95, 0.9, 150, 188, 490, 7)
        rect(0.95, 0.95, 0.9, 150, 215, 490, 7)
        rect(0.45, 0.3, 0.15, 470, 100, 30, 140)
        circle(0.2, 0.5, 0.2, 485, 80, 50)
        circle(0.25, 0.55, 0.22, 450, 100, 35)
        circle(0.25, 0.55, 0.22, 520, 100, 35)
        rect(0.7, 0.35, 0.2, 200, 230, 30, 25)
        circle(0.9, 0.3, 0.4, 207, 222, 6)
        circle(0.95, 0.85, 0.3, 222, 220, 6)
        if flags.crowFed then
            drawCrow(480, 62)
        else
            drawCrow(320, 168)
            drawKey(338, 174)
        end
        if flags.crowFed and not flags.keyTaken then
            drawKey(325, 268)
        end
    end,

    hotspots = {
        { name = "Door to my bedroom", x = 30, y = 100, w = 60, h = 140,
          exit = "bedroom", entry = { 540, 270 } },
        { name = "Tree", x = 430, y = 30, w = 110, h = 210,
          look = "An old apple tree. No apples, and no hats." },
        { name = "Flowerpot", x = 195, y = 210, w = 40, h = 45,
          look = "Geraniums.",
          use = "I already looked under it. Only worms." },
        { name = "Crow", x = 305, y = 150, w = 40, h = 30,
          when = function() return not flags.crowFed end,
          look = "A crow, with a shiny key in its beak. That's my wardrobe key!",
          use = function()
              adv.say("Give me that key!")
              adv.say("Caw!", crow)
          end,
          items = {
              cookie = function()
                  adv.drop("cookie")
                  flags.crowFed = true
                  adv.say("Caw! Caw!", crow)
                  adv.say("It dropped the key and took the cookie up into the tree.")
              end,
          } },
        { name = "Crow", x = 465, y = 45, w = 40, h = 30,
          when = function() return flags.crowFed end,
          look = "A happy crow, munching on my cookie.",
          use = function() adv.say("Caw.", { x = 485, y = 20, color = crow.color }) end },
        { name = "Key", x = 310, y = 258, w = 32, h = 20,
          when = function() return flags.crowFed and not flags.keyTaken end,
          use = function()
              flags.keyTaken = true
              adv.take("key")
              adv.say("My wardrobe key!")
          end },
    },
}

function love.load()
    adv.init()
    adv.go("bedroom", 300, 275)
end

function love.update(dt)
    adv.update(dt)
end

function love.draw()
    adv.draw()
end

function love.mousepressed(x, y, button)
    adv.mousepressed(x, y, button)
end

function love.mousereleased(x, y, button)
    adv.mousereleased(x, y, button)
end

function love.keypressed(key)
    adv.keypressed(key)
end
