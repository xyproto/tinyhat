-- LÖVE starts here, and the game itself is written in Fennel, a Lisp, in game.fnl.
-- Run it with "make run" or "love ." in this directory.
package.path = package.path .. ";/usr/share/lua/5.4/?.lua"
local fennel = require("fennel")
fennel.dofile(love.filesystem.getSource() .. "/game.fnl")
