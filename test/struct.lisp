(defstruct! foo
  x
  y
  z)

(setq! x (foo/new :y 10 :x 5 :z 111))
(print! x)
(print! (foo/get-y x))
(foo/set-y! x 42)
(print! (foo/get-y x))
(print! (foo/get-z x))
