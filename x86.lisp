(defun! x86/split16le (w)
  "Split the 16-bit W16 into a little-endian list of 8-bit integers."
  (list
    (bitwise/and 0xff w)
    (bitwise/and 0xff (bitwise/rshift w 8))))

(defun! x86/split32le (w)
  "Split the 32-bit W32 into a little-endian list of 8-bit integers."
  (list
   (bitwise/and 0xff w)
   (bitwise/and 0xff (bitwise/rshift w 8))
   (bitwise/and 0xff (bitwise/rshift w 16))
   (bitwise/and 0xff (bitwise/rshift w 24))))

(defun! x86/register-1byte? (r)
  "Return the register index for 1-byte register R."
  (case r
    (al 0) (cl 1) (dl 2) (bl 3)
    (ah 4) (ch 5) (dh 6) (bh 7)
    (r8b 8) (r9b 9) (r10b 10) (r11b 11)
    (r12b 12) (r13b 13) (r14b 14) (r15b 15)))

(defun! x86/register-2byte? (r)
  "Return the register index for 2-byte register R."
  (case r
    (ax 0) (cx 1) (dx 2) (bx 3)
    (sp 4) (bp 5) (si 6) (di 7)
    (r8w 8) (r9w 9) (r10w 10) (r11w 11)
    (r12w 12) (r13w 13) (r14w 14) (r15w 15)))

(defun! x86/register-4byte? (r)
  "Return the register index for 4-byte register R."
  (case r
    (eax 0) (ecx 1) (edx 2) (ebx 3)
    (esp 4) (ebp 5) (esi 6) (edi 7)
    (r8d 8) (r9d 9) (r10d 10) (r11d 11)
    (r12d 12) (r13d 13) (r14d 14) (r15d 15)))

(defun! x86/register-8byte? (r)
  "Return the register index for 8-byte register R."
  (case r
    (rax 0) (rcx 1) (rdx 2) (rbx 3)
    (rsp 4) (rbp 5) (rsi 6) (rdi 7)
    (r8 8) (r9 9) (r10 10) (r11 11)
    (r12 12) (r13 13) (r14 14) (r15 15)))

(defun! x86/register? (r)
  "Return the register index of R."
  (or
    (x86/register-1byte? r)
    (x86/register-2byte? r)
    (x86/register-4byte? r)
    (x86/register-8byte? r)))

(defun! x86/register-extended? (r)
  "Return non-nil if R is an extended register."
  (list/contains? r
    '( r8b r9b r10b r11b r12b r13b r14b r15b
       r8w r9w r10w r11w r12w r13w r14w r15w
       r8d r9d r10d r11d r12d r13d r14d r15d
       r8 r9 r10 r11 r12 r13 r14 r15)))

(defun! x86/integer-fits-in-bits? (bits x)
  "Determine if X fits in BITS."
  (if (integer? x)
    (let ((bound (bitwise/lshift 1 bits)))
      (or
        (and (>= x 0) (< x bound))
        (and (< x 0) (<= (abs x) (/ bound 2)))))))
(defun! x86/operand-immediate-fits? (sz x)
  "Determine if immediate operand X fits in SZ."
  (let
    ((bits
       (or
         (case sz
           ("b" 8) ("c" 16) ("d" 32) ("i" 16)
           ("j" 32) ("q" 64) ("v" 64) ("w" 16)
           ("y" 64) ("z" 32))
         (error! "unknown operand pattern size"))))
    (x86/integer-fits-in-bits? bits x)))

(defun! x86/operand-register-fits? (sz r)
  "Determine if register operand R fits in SZ."
  (case sz
    ("b" (x86/register-1byte? r))
    ("c" (or (x86/register-1byte? r) (x86/register-2byte? r)))
    ("d" (x86/register-4byte? r))
    ("i" (x86/register-2byte? r))
    ("j" (x86/register-4byte? r))
    ("q" (x86/register-8byte? r))
    ("v" (or (x86/register-2byte? r) (x86/register-4byte? r) (x86/register-8byte? r)))
    ("w" (x86/register-2byte? r))
    ("y" (or (x86/register-4byte? r) (x86/register-8byte? r)))
    ("z" (or (x86/register-2byte? r) (x86/register-4byte? r)))))

(defun! x86/memory-operand-base (m)
  (and
    (eq? (car m) 'mem)
    (car (cdr m))))
(defun! x86/memory-operand-off (m)
  (and
    (eq? (car m) 'mem)
    (or (car (cdr (cdr m))) 0)))

(defun! x86/operand-memory-location? (op)
  "Return non-nil if OP represents a memory location."
  (let ( (base (x86/memory-operand-base op))
         (off (x86/memory-operand-off op)))
    (and
      (or (x86/register-4byte? base) (x86/register-8byte? base))
      (integer? off))))

(defun! x86/operand-match? (pat op)
  "Determine if operand OP matches PAT."
  (cond
    ((symbol? pat) (eq? pat op))
    ((cons? pat) (list/contains? op pat))
    ((bytes? pat)
      (let ( (loc (bytes/range 0 1 pat))
             (sz (bytes/range 1 (bytes/len pat) pat)))
        (cond
          ((or (equal? loc "I") (equal? loc "J")) (x86/operand-immediate-fits? sz op))
          ((or (equal? loc "G") (equal? loc "R")) (x86/operand-register-fits? sz op))
          ((equal? loc "M") (x86/operand-memory-location? op))
          ((equal? loc "E")
            (or (x86/operand-register-fits? sz op) (x86/operand-memory-location? op)))
          (t (error! "unknown operand pattern location")))))))

(defun! x86/operand-size (op)
  "Return the minimum power-of-2 size in bytes that contains OP."
  (cond
    ((symbol? op)
      (cond
        ((x86/register-1byte? op) 1)
        ((x86/register-2byte? op) 2)
        ((x86/register-4byte? op) 4)
        ((x86/register-8byte? op) 8)
        (t (error! "attempted to take size of unknown register"))))
    ((integer? op)
      (cond
        ((x86/integer-fits-in-bits? 8 op) 1)
        ((x86/integer-fits-in-bits? 16 op) 2)
        ((x86/integer-fits-in-bits? 32 op) 4)
        ((x86/integer-fits-in-bits? 64 op) 8)
        (t (error! "attempted to take size of too-large immediate"))))
    ((x86/operand-memory-location? op) 1)
    (t (error! "attempted to take size of unknown operand"))))

(defstruct! x86/ins
  operand-size-prefix
  address-size-prefix
  rex-w
  rex-r
  rex-x
  rex-b
  opcode
  modrm-mod
  modrm-reg
  modrm-rm
  disp ;; pair of size and value
  imm ;; pair of size and value
  )

(defun! x86/ins-bytes (ins)
  "Return a list of bytes encoding INS."
  (let ( (opcode (x86/ins/get-opcode ins))
         (rex-w (x86/ins/get-rex-w ins))
         (rex-r (x86/ins/get-rex-r ins))
         (rex-x (x86/ins/get-rex-x ins))
         (rex-b (x86/ins/get-rex-b ins))
         (modrm-mod (x86/ins/get-modrm-mod ins))
         (modrm-reg (x86/ins/get-modrm-reg ins))
         (modrm-rm (x86/ins/get-modrm-rm ins))
         (disp (x86/ins/get-disp ins))
         (imm (x86/ins/get-imm ins)))
    (list/append
      (if (x86/ins/get-operand-size-prefix ins) '(0x66))
      (if (x86/ins/get-address-size-prefix ins) '(0x67))
      (if (or rex-w rex-r rex-x rex-b)
        (list
          (bitwise/or
            0x40
            (if rex-w 0b1000 0)
            (if rex-r 0b0100 0)
            (if rex-x 0b0010 0)
            (if rex-b 0b0001 0))))
      (cond
        ((not opcode) (error! "no opcode for instruction"))
        ((cons? opcode) opcode)
        ((integer? opcode) (list opcode))
        (t (error! "malformed opcode for instruction")))
      (if (or modrm-mod modrm-reg modrm-rm)
        (list
          (bitwise/or
            (bitwise/lshift (or modrm-mod 0) 6)
            (bitwise/lshift (or modrm-reg 0) 3)
            (or modrm-rm 0))))
      (if disp
        (cond
          ((= (car disp) 1) (list (cdr disp)))
          ((= (car disp) 4) (x86/split32le (cdr disp)))
          (t (error! "malformed displacement for instruction"))))
      (if imm
        (cond
          ((= (car imm) 1) (list (cdr imm)))
          ((= (car imm) 2) (x86/split16le (cdr imm)))
          ((= (car imm) 4) (x86/split32le (cdr imm)))
          (t (error! "malformed immediate for instruction")))))))

(defun! x86/instruction-update-sizes (ins ops default-size)
  "Update INS to account for the sizes of OPS.
DEFAULT-SIZE is the default operand size."
  (let ((defsz (or default-size 4)))
    (if (> (list/len ops) 0)
      (let ((regs (list/uniq (list/map 'x86/operand-size (list/filter 'x86/register? ops)))))
        (if (> (list/len regs) 1)
          (error! "invalid mix of register sizes in operands"))
        (let ((sz (if (eq? (list/len regs) 0) defsz (car regs))))
          (cond
            ((eq? sz 1) nil)
            ((eq? defsz sz) nil)
            ((and (not (eq? defsz 2)) (eq? sz 2)) (x86/ins/set-operand-size-prefix! ins t))
            ((and (not (eq? defsz 8)) (eq? sz 8)) (x86/ins/set-rex-w! ins t))
            (t (error! "unable to encode operands with default size")))
          sz)))))

(defun! x86/instruction-update-operand (esz ins pat op)
  "Update INS to account for an operand OP according to PAT.
The effective operand size is ESZ."
  (cond
    ((bytes? pat)
      (let ((loc (bytes/range 0 1 pat)))
        (cond
          ((equal? loc "I")
            (let ((immsz (min esz 4)))
              (if (not (x86/integer-fits-in-bits? (* 8 immsz) op))
                (error! "Immediate too large" op))
              (x86/ins/set-imm! ins (cons immsz op))))
          ((equal? loc "J")
            (let ((immsz (if (= esz 1) 1 4)))
              (if (not (x86/integer-fits-in-bits? (* 8 immsz) op))
                (error! "jump displacement too large"))
              (x86/ins/set-disp! ins (cons immsz op))))
          ((equal? loc "G")
            (x86/ins/set-modrm-reg! ins
              (or (x86/register? op) (error "Invalid register: %s" op))))
          ((or (equal? loc "R") (and (equal? loc "E") (x86/register? op)))
            (x86/ins/set-modrm-mod! ins 0b11)
            (x86/ins/set-modrm-rm! ins
              (or (x86/register? op) (error "Invalid register: %s" op))))
          ((or (equal? loc "M") (and (equal? loc "E") (x86/operand-memory-location? op)))
            (let ( (base (x86/memory-location-base op))
                   (off (x86/memory-location-off op)))
              (cond
                ((eq? base 'eip)
                  (x86/ins/set-modrm-rm! ins 0b101)
                  (x86/ins/set-modrm-mod! ins 0b00)
                  (x86/ins/set-disp! ins (cons 4 off))
                  (x86/ins/set-address-size-prefix! ins t))
                ((eq? base 'rip)
                  (x86/ins/set-modrm-rm! ins 0b101)
                  (x86/ins/set-modrm-mod! ins 0b00)
                  (x86/ins/set-disp! ins (cons 4 off)))
                (t
                  (x86/ins/set-modrm-rm! ins
                    (or
                      (x86/register-4byte? base)
                      (x86/register-8byte? base)
                      (error! "invalid base register")))
                  (if (x86/register-4byte? base)
                    (x86/ins/set-address-size-prefix! ins t))
                  (cond
                    ((x86/integer-fits-in-bits? 8 off)
                      (x86/ins/set-disp! ins (cons 1 off))
                      (x86/ins/set-modrm-mod! ins 0b01))
                    ((x86/integer-fits-in-bits? 32 off)
                      (x86/ins/set-disp! ins (cons 4 off))
                      (x86/ins/set-modrm-mod! ins 0b10))
                    (t (error! "invalid offset")))))))
          (t (error! "invalid operand location code")))))))

(defun! x86/default-instruction-handler (opcode & kwargs)
  "Return an instruction handler for OPCODE.
The instruction handler will run POSTHOOK on the instruction at the end.
DEFAULT-SIZE is the default operand size."
  (print! opcode)
  (let ( (posthook (plist/get :posthook kwargs))
         (default-size (plist/get :default-size kwargs)))
    (lambda (pats ops)
      (print! opcode)
      ;; (print! default-size)
      ;; (print! posthook)
      (let ((ret (x86/ins/new :opcode opcode)))
        (let ((esz
                (or (x86/instruction-update-sizes ret ops default-size)
                  (error! "malformed size for operands"))))
          (list/zip-with
            (lambda (it other)
              (x86/instruction-update-operand esz ret it other))
            pats
            ops))
        (if posthook
          (funcall posthook ret ops))
        ret))))

(defun! x86/asm (op)
  "Assemble OP to an instruction."
  (let ((mnem (car op)) (operands (cdr op)))
    (let ((variants (or (alist/get mnem x86/mnemonic-table) (error! "unknown mnemonic"))))
      (let ((v
              (list/find
                (lambda (it)
                  (and (eq? (list/len (car it)) (list/len operands))
                    (list/all? (lambda (x) x) (list/zip-with 'x86/operand-match? (car it) operands))))
                variants)))
        (if (and v (function? (cdr v)))
          (funcall (cdr v) (car v) operands)
          (error! "could not identify instruction"))))))

(setq!
  x86/mnemonic-table
  (list
    (cons 'add
      (list
        (cons (list "Eb" "Gb") (x86/default-instruction-handler 0))
        (cons (list "Ev" "Gv") (x86/default-instruction-handler 1))))))

(setq! test-ins (x86/asm '(add al bl)))
(print! test-ins)
(print! (x86/ins-bytes test-ins))
