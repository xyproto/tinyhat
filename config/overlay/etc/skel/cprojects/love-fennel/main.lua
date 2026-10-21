-- LÖVE starts here, and the game itself is written in Fennel, a Lisp, in game.fnl.
-- Press Ctrl+Space twice in rapid succession to build and run it.
package.path = package.path .. ";/usr/share/lua/5.4/?.lua"
local fennel = require("fennel")
fennel.eval(love.filesystem.read("game.fnl"), { filename = "game.fnl" })
