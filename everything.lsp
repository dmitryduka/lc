(define atom? (lambda (x) (cond (null? x) 1 
                                    (func? x) 1 
                                    (str? x) 1 
                                    (int? x) 1 
									(1) 0)))
(define first (lambda (x) (cond (atom? x) x (1) (car x))))
(define rest  (lambda (x) (cond (atom? x) Nil (1) (cdr x))))
(define odd? (lambda (x) (eq (- x (* (/ x 2) 2)) 1)))
(define not (lambda (x) (cond (eq x 0) 1 (1) 0)))
(define even? (lambda (x) (not (odd? x))))
(define square (lambda (x) (* x x)))
(define add (lambda (x y) (+ x y)))
(define append (lambda (x y) (cond (null? x) y 
                                    (1) (cons (first x) (append (rest x) y)))))
(define apply (lambda (f l) (cond (atom? l) (f l) (1) (begin (f (first l)) (apply f (rest l))))))
(define accum (lambda (op start l) 
                                  (cond (null? l) start 
                                        (1) (op (car l) (accum op start (cdr l))))))
(define reverse (lambda (l) (begin 
											(define rev-aux (lambda (x y) 
																		(cond (null? x) y 
											      							  (1) (rev-aux 
																			   			(rest x) 
																			   			(cons 
																			   				(first x) 
																			   				y))))) 
											(rev-aux l Nil))))
(define filter (lambda (pred l) 
                                    (cond (null? l) Nil 
                                          (1) (append (cond (pred (first l)) (first l) 
                                                            (1) Nil) 
                                                      (filter pred (rest l))))))
(define qsort (lambda (l) (begin (define f (first l)) 
                                             (define r (rest l)) 
                                             (define <= (lambda (x) (lambda (y) (cond (eq x y) 1 
                                                                                      (less y x) 1 
                                                                                      (1) 0)))) 
                                             (define > (lambda (x) (lambda (y) (cond (not (eq x y)) (cond (not (less y x)) 1 
                                                                                           					   (1) 0) 
                                                                                     (1) 0)))) 
                                             (cond (null? l) Nil 
                                                   (null? f) Nil 
                                                   (null? r) f 
                                                   (1) (append (qsort (filter (<= f) r)) 
                                                               (append f (qsort (filter (> f) r))))))))
(define map (lambda (f l) (cond (null? l) Nil (1) (append (f (first l)) (map f (rest l))))))
(define faux (lambda (x a) (cond (eq x 1) a (1) (faux (- x 1) (* x a)))))
(define factl (lambda (x) (faux x 1)))
(define prnel (lambda (x) (begin (print x) (print))))
(define length (lambda (l) (cond (null? l) 0 (atom? l) 1 (1) (+ 1 (length (rest l))))))
// get nth element
(define nthaux (lambda (c n l) (cond (eq n c) (first l) (1) (cond (atom? l) Nil (1) (nthaux (+ c 1) n (rest l))))))
(define nth (lambda (n l) (nthaux 0 n l)))
// set nth element
(define fstnax (lambda (c n l) (cond (eq n c) (first l) (1) (cons (first l) (fstnax (+ c 1) n (rest l))))))
(define firstn (lambda (n l) (fstnax 1 n l)))
(define stnaux (lambda (c n l v) (cond (eq n c) (cond (atom? l) v (1) (cons v (rest l))) (1) (cond (atom? l) l (1) (stnaux (+ c 1) n (rest l) v)))))
(define setnth (lambda (n l x) (append (firstn n l) (stnaux 0 n l x))))
// generate a list of elements with given value
(define gen1 (lambda (x) (cond (eq x 1) 1 (1) (cons 1 (gen1 (- x 1))))))

