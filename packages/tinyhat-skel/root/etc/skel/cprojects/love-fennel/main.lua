package.path = package.path .. ";/usr/share/lua/5.4/?.lua"
local fennel = require("fennel")
fennel.eval(love.filesystem.read("game.fnl"), { filename = "game.fnl" })
