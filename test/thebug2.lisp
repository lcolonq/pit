
(setq! z 40)
(setq! f
  (lambda (x)
    (lambda (y)
      (lambda (w)
        (+
          (funcall (lambda (z) (+ x y z)) 3)
          w
          z)))))
(let ((z 20))
  (print! (funcall (funcall (funcall f 1) 2) 3)))

