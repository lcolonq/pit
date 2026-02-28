;;; pit --- support for pit -*- lexical-binding: t; -*-
;;; Commentary:
;;; Code:

(require 'dash)
(require 's)
(require 'cl-lib)
(require 'rx)
(require 'hydra)
(require 'comint)

(defcustom pit/repl-buffer-name "*pit-repl*"
  "Name of the pit REPL buffer."
  :type '(string)
  :group 'pit)

(defcustom pit/interpreter-path "~/src/libcolonq/pit/pit"
  "Path to the pit interpreter."
  :type '(string)
  :group 'pit)

(define-derived-mode pit/mode lisp-mode "pit"
  "Major mode for pit source code."
  )
(add-to-list 'auto-mode-alist `(,(rx ".pit" eos) . pit/mode))

(defun pit/repl-buffer ()
  "Ensure the REPL is running and return its buffer."
  (make-comint-in-buffer "pit" pit/repl-buffer-name pit/interpreter-path nil)
  (get-buffer pit/repl-buffer-name))

(defun pit/repl-process ()
  "Return the Comint process for the REPL."
  (get-buffer-process (pit/repl-buffer)))

(defun pit/send-string (s)
  "Send string S to the REPL."
  (comint-send-string (pit/repl-process) (s-concat s "\n")))

(defun pit/eval-region (start end)
  "Send the region from START to END to the REPL."
  (interactive "r")
  (comint-send-region (pit/repl-process) start end)
  (comint-send-string (pit/repl-process) "\n"))

(defun pit/eval-defun ()
  "Send the defun under point to the REPL."
  (interactive)
  (save-excursion
    (end-of-defun)
    (beginning-of-defun)
    (let ((start (point)))
      (forward-sexp)
      (pit/eval-region start (point)))))

(defun pit/eval-buffer ()
  "Send the current buffer to the REPL."
  (interactive)
  (pit/send-string (format "(progn %s 'done)" (buffer-string))))

(defun pit/restart ()
  "Restart the pit REPL."
  (interactive)
  (kill-buffer pit/repl-buffer-name)
  (pit/repl))

(defun pit/repl ()
  "Launch the pit REPL."
  (interactive)
  (switch-to-buffer (pit/repl-buffer)))

;;;; configuration
(defhydra pit/ide (:color teal :hint nil)
  "Dispatcher > pit IDE."
  ("<f12>" keyboard-escape-quit)
  ("S" pit/restart "start")
  ("e" pit/eval-defun "eval")
  ("i" pit/eval-buffer "buffer")
  ("r" pit/repl "repl"))
(defun pit/setup ()
  "Configuration for `pit/mode'."
  (setq-local c/contextual-ide 'pit/ide/body))
(add-hook 'pit/mode-hook #'pit/setup)

(provide 'pit)
;;; pit.el ends here
