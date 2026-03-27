(setq! Y
  (lambda (f)
    (funcall
      (lambda (x) (funcall f (funcall x x)))
      (lambda (x) (funcall f (funcall x x))))))
