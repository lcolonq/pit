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

;;;; nrepl client
;; (require 'nrepl-client)
;; (defun pit/repl/connect (&optional host port)
;;   "Connect to pit nREPL server at HOST:PORT."
;;   (let ((ret (get-buffer-create (generate-new-buffer-name " *pit-nrepl*") t)))
;;     (nrepl-start-client-process (or host "localhost") (or port 7888) nil (lambda (_pl) ret))
;;     ret))

(require 'cider)
(defun pit/repl/connect (&optional host port)
  "Connect to pit nREPL server at HOST:PORT."
  (let ( (dir (if-let* ((p (project-current))) (project-root p) default-directory))
         (cider-repl-init-code nil))
    (cider-nrepl-connect
      (print
      (thread-first `(:host ,host :port ,port :project-dir ,dir :repl-init-function nil :session-name nil :repl-type pit)
        (cider--update-project-dir)
        (cider--update-host-port)
        (cider--check-existing-session))))))

(defun cider-repl-handler (buffer)
  "Make an nREPL evaluation handler for the REPL BUFFER."
  (message "running cider-repl-handler")
  (let ((show-prompt t))
    (nrepl-make-response-handler
     buffer
     (lambda (buffer value)
       (message "value-handler")
       (cider-repl-emit-result buffer value t))
     (lambda (buffer out)
       (dolist (f cider--repl-stdout-functions)
         (funcall f buffer out))
       (cider-repl-emit-stdout buffer out))
     (lambda (buffer err)
       (dolist (f cider--repl-stderr-functions)
         (funcall f buffer err))
       (cider-repl-emit-stderr buffer err))
     (lambda (buffer)
       (message "done-handler: %s" show-prompt)
       (when show-prompt
         (cider-repl-emit-prompt buffer))
       (when cider-repl-buffer-size-limit
         (cider-repl-maybe-trim-buffer buffer))
       (dolist (f cider--repl-done-functions)
         (funcall f buffer)))
     nrepl-err-handler
     (lambda (buffer value content-type)
       (if-let* ((content-attrs (cadr content-type))
                 (content-type* (car content-type))
                 (handler (cdr (assoc content-type*
                                      cider-repl-content-type-handler-alist))))
           (setq show-prompt (funcall handler content-type buffer value nil t))
         (cider-repl-emit-result buffer value t t)))
     (lambda (buffer warning)
       (cider-repl-emit-stderr buffer warning)))))

(provide 'pit)
;;; pit.el ends here
