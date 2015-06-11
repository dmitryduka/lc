(define atom? (lambda (x) (cond (null? x) 1 
                                (func? x) 1 
                                (str? x) 1 
                                (int? x) 1 
								(1) 0)))
(define first (lambda (x) (cond (atom? x) x (1) (car x))))
(define rest  (lambda (x) (cond (atom? x) Nil (1) (cdr x))))
(define not (lambda (x) (cond (eq x 0) 1 (1) 0)))
(define append (lambda (x y) (cond (null? x) y 
                                   (1) (cons (first x) (append (rest x) y)))))
(define apply (lambda (f l) (cond (atom? l) (f l) (1) (begin (f (first l)) (apply f (rest l))))))
(define prnel (lambda (x) (begin (print x) (print))))
(define length (lambda (l) (cond (null? l) 0 (atom? l) 1 (1) (+ 1 (length (rest l))))))
(define nthaux (lambda (c n l) (cond (eq n c) (first l) (1) (cond (atom? l) Nil (1) (nthaux (+ c 1) n (rest l))))))
(define nth (lambda (n l) (nthaux 0 n l)))
(define fstnax (lambda (c n l) (cond (eq n c) (first l) (1) (cons (first l) (fstnax (+ c 1) n (rest l))))))
(define firstn (lambda (n l) (fstnax 1 n l)))
(define stnaux (lambda (c n l v) (cond (eq n c) (cond (atom? l) v (1) (cons v (rest l))) (1) (cond (atom? l) l (1) (stnaux (+ c 1) n (rest l) v)))))
(define setnth (lambda (n l x) (append (firstn n l) (stnaux 0 n l x))))
(define gen1 (lambda (x) (cond (eq x 1) 1 (1) (cons 1 (gen1 (- x 1))))))
(define inloop (lambda (l x n) (cons (setnth n l (% x n)) (+ (* 10 (nth (- n 1) l)) (/ x n)))))
(define mnloop (lambda (l x n) (cond (eq n 1)  (inloop l x n) (1) (begin (define r (inloop l x n)) (mnloop (car r) (cdr r) (- n 1))))))
(define otloop (lambda (l x n) (cond (eq n 10) (print) (1) (begin (define r (mnloop l x n)) (print (cdr r)) (otloop (car r) (cdr r) (- n 1))))))
(define l1 (cons 0 (cons 2 (gen1 107))))
(otloop l1 0 108)
(gc)
