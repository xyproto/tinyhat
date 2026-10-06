;; A hat that glides after the mouse under twinkling stars. Esc quits.

(local stars [])
(var hat-x 320)
(var hat-y 200)

(fn love.load []
  (love.mouse.setVisible false)
  (for [_ 1 80]
    (table.insert stars {:x (love.math.random 0 (love.graphics.getWidth))
                         :y (love.math.random 0 (love.graphics.getHeight))
                         :phase (* (love.math.random) 6.28)})))

(fn love.update [dt]
  (let [(mx my) (love.mouse.getPosition)]
    (set hat-x (+ hat-x (* (- mx hat-x) dt 4)))
    (set hat-y (+ hat-y (* (- my hat-y) dt 4)))))

(fn draw-hat [x y]
  (love.graphics.setColor 0.75 0.22 0.17)
  (love.graphics.rectangle :fill (- x 30) (- y 50) 60 60)
  (love.graphics.rectangle :fill (- x 55) (+ y 10) 110 14)
  (love.graphics.setColor 0.1 0.62 0.59)
  (love.graphics.rectangle :fill (- x 30) (- y 2) 60 8))

(fn love.draw []
  (love.graphics.clear 0.06 0.08 0.17)
  (let [t (love.timer.getTime)]
    (each [_ s (ipairs stars)]
      (let [v (+ 0.6 (* 0.4 (math.sin (+ (* t 3) s.phase))))]
        (love.graphics.setColor v v v)
        (love.graphics.points s.x s.y))))
  (draw-hat hat-x hat-y)
  (love.graphics.setColor 1 1 1)
  (love.graphics.print "Move the mouse to steer the hat. Esc quits." 10 10))

(fn love.keypressed [key]
  (when (= key :escape)
    (love.event.quit)))
