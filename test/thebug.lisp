(defun! foo (x)
  (lambda (y)
    (+ x y)))

(setq! bar (foo 10))
(setq! baz (foo 100))

(print! (funcall bar 4))

