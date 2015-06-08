#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <memory>
#include <chrono>

using std::cout;
using std::endl;
using std::shared_ptr;

struct Cell
{
    enum CellType { Symbol, Int, List, Nil } type;
    int as_int;
    std::vector<Cell> list;
    std::string name;

    bool operator!=(const Cell& cell) { return type != cell.type; }

    Cell() : type(List) {}
    Cell(CellType x) : type(x) {}
    Cell(int x) : type(Int) { as_int = x; }
    Cell(const std::string& x)
    {
        if (!x.empty())
        {
            const char* str = x.c_str();
            if ((str[0] >= '0' && str[0] <= '9') ||
                ((str[0] == '-' || str[0] == '+') &&
                (str[1] >= '0' && str[1] <= '9')))
            {
                type = Int;
                as_int = atol(str);
            }
            else
            {
                type = Symbol;
                name = x;
            }
        }
    }

    void compile(std::vector<std::string>&, std::vector<std::vector<std::string>>&) const;
};
                                                                                                                                                                                
void compile_args(const std::vector<Cell>& list, 
                        std::vector<std::string>& program,
                        std::vector<std::vector<std::string>>& functions)
{
   for (int i = 1; i < list.size(); ++i)
        list[i].compile(program, functions);
}

void Cell::compile(std::vector<std::string>& program,
                   std::vector<std::vector<std::string>>& functions) const
{
    if (type == Int) program.push_back("PUSHCI " + std::to_string(as_int));
    else if (type == Symbol)
    {
    	if (name == "Nil") program.push_back("PUSHNIL");    		
	    else
    	{
	        program.push_back("LOADENV");
	        program.push_back("PUSHCAR");
	        program.push_back("PUSHCAR");
	        program.push_back("EQSI " + name);
	        program.push_back("RJNZ +6");
	        program.push_back("POP");
	        program.push_back("POP");
	        program.push_back("POP");
	        program.push_back("CDR");
	        program.push_back("RJMP -8");
	        program.push_back("POP");
	        program.push_back("POP");
	        program.push_back("CDR");
	        program.push_back("SWAP 0");
	        program.push_back("POP");
	    }
    }
    else if (type == List)
    {
        if (list.empty()) return;
        else if (list[0].type == Cell::Int) list[0].compile(program, functions);
        else if (list[0].type == Cell::Nil) program.push_back("PUSHNIL");
        else if (list[0].type == Cell::Symbol)
        {
            if (list[0].name == "+") { compile_args(list, program, functions); program.push_back("ADD"); }
            else if (list[0].name == "-") { compile_args(list, program, functions); program.push_back("SUB"); }
            else if (list[0].name == "*") { compile_args(list, program, functions); program.push_back("MUL"); }
            else if (list[0].name == "/") { compile_args(list, program, functions); program.push_back("DIV"); }
            else if (list[0].name == "%") { compile_args(list, program, functions); program.push_back("MOD"); }
            else if (list[0].name == "less")
            {
                 compile_args(list, program, functions); 
                 program.push_back("LT");
            }
            else if (list[0].name == "eq")
            {
                 compile_args(list, program, functions); 
                 program.push_back("EQ");
            }
            else if (list[0].name == "cons")
            {
		         list[2].compile(program, functions);
		         list[1].compile(program, functions);
                 program.push_back("CONS");
            }
            else if (list[0].name == "car")
            {
                 compile_args(list, program, functions); 
                 program.push_back("CAR");
            }
            else if (list[0].name == "cdr")
            {
                 compile_args(list, program, functions); 
                 program.push_back("CDR");
            }
            else if (list[0].name == "define")
            {
                list[2].compile(program, functions);
                program.push_back("PUSHS " + list[1].name);
                program.push_back("CONS");
                program.push_back("DEF");
            }
            else if (list[0].name == "func?")
            {
                compile_args(list, program, functions); 
                program.push_back("PUSHL -1"); 
                program.push_back("EQT");      
                program.push_back("SWAP 1");   
                program.push_back("POP");      
                program.push_back("POP");      
            }
            else if (list[0].name == "gc")
            {
                program.push_back("GC");      
                program.push_back("PUSHNIL");
            }
            else if (list[0].name == "print")
            {
                if (list.size() == 1)
                    program.push_back("PRNL"); 
                else
                {
                    list[1].compile(program, functions);
                    program.push_back("PRN"); 
                }
                program.push_back("PUSHNIL");
            }
            else if (list[0].name == "null?")
            {
                compile_args(list, program, functions); 
                program.push_back("PUSHNIL");
                program.push_back("EQT");    
                program.push_back("SWAP 1");   
                program.push_back("POP");      
                program.push_back("POP");      
            }
            else if (list[0].name == "int?")
            {
                compile_args(list, program, functions); 
                program.push_back("PUSHCI 0");
                program.push_back("EQT");    
                program.push_back("SWAP 1");   
                program.push_back("POP");      
                program.push_back("POP");      
            }
            else if (list[0].name == "str?")
            {
                compile_args(list, program, functions); 
                program.push_back("PUSHS s");
                program.push_back("EQT");    
                program.push_back("SWAP 1");   
                program.push_back("POP");      
                program.push_back("POP");      
            }
            else if (list[0].name == "begin")
            {
                for (int i = 1; i < list.size() - 1; ++i)
                {
                    list[i].compile(program, functions);
	                program.push_back("POP");      
                }
                list.back().compile(program, functions);
	        }
            else if (list[0].name == "cond")
            {
                std::vector<std::vector<std::string>> conditions;
                std::vector<std::vector<std::string>> results;
                for (int i = 1; i < list.size(); ++i)
                {
                    if (i % 2)
                    {
                        std::vector<std::string> cond;
                        list[i].compile(cond, functions);
                        conditions.push_back(cond);
                    }
                    else
                    {
                        std::vector<std::string> result;
                        list[i].compile(result, functions);
                        results.push_back(result);
                    }
                }
                for (int i = 0; i < conditions.size(); ++i)
                {
                    if (i != 0)
                        program.push_back("POP");
                    for (auto& line : conditions[i])
                        program.push_back(line);
                    if (i != conditions.size() - 1)
                        program.push_back("RJZ " + std::to_string(results[i].size() + 3));
                    else
                        program.push_back("RJZ " + std::to_string(results[i].size() + 2));
                    program.push_back("POP");
                    for (auto& line : results[i])
                        program.push_back(line);
                    if (i != conditions.size() - 1)
                    {
                        size_t jump = 0;
                        for (int j = i + 1; j < conditions.size(); ++j)
                            jump += conditions[j].size() + results[j].size() + 4;
                        program.push_back("RJMP " + std::to_string(jump));
                    }
                }                
            }
            else if (list[0].name == "lambda")
            {
                // create new environment and bind arguments
                const size_t args_count = list[1].list.size();
                std::vector<std::string> func;
                for (int i = 0; i < args_count; ++i)
                {
                    func.push_back("LOADENV");
                    func.push_back("PUSHFS " + std::to_string(2 + args_count - i)); // 2 - PC and env
                    func.push_back("PUSHS " + list[1].list[i].name);
                    func.push_back("CONS");
                    func.push_back("CONS");
                    func.push_back("STOREENV");
                }
                // pop arguments from the stack
                for (int i = 0; i < args_count; ++i)
                {
                    func.push_back("SWAP 1");
                    func.push_back("POP");
                }
                if (args_count % 2)
                    func.push_back("SWAP 0");                
                // compile body
                list[2].compile(func, functions);
                // SWAP pc and result
                func.push_back("SWAP 1"); 
                func.push_back("SWAP 0"); 
                func.push_back("RET");
                functions.push_back(func);
                program.push_back("PUSHL " + std::to_string(functions.size() - 1));
            }
            else // function call
            {
                compile_args(list, program, functions); 
                Cell f(Symbol);
                f.name = list[0].name;
                f.compile(program, functions);
                program.push_back("CALL");
            }
        }
    }
}

Cell parse_list(const char* input, const char** jumped_to = NULL)
{
    if (input)
    {
        const char* cur = input;
        bool symbol_ready = false;
        std::string symbol;
        Cell cell;
        while (*cur)
        {
            const char c = *cur;
            if (c == '(')
                cell.list.push_back(parse_list(++cur, &cur));
            else if (c == ')' || c == ' ' || c == '\t' || c == '\n')
            {
                if (symbol_ready)
                {   
                    // issue error in case symbol size is more than 7 characters:
                    // it wont fit into the Cell in the VM
                    // TODO: mangle names to shorter strings
                    // if (symbol.size() > 7) { std::cout << "Long names are not supported: " << symbol << endl; exit(1); }
                    cell.list.push_back(Cell(symbol));
                    symbol_ready = false;
                    symbol.clear();
                }
                if (c == ')')
                {
                    *jumped_to = cur;
                    return cell;
                }
            }
            else
            {
                symbol_ready = true;
                symbol.push_back(c);
            }
            cur++;
        }
        return cell.list[0];
    }
}

std::vector<std::string> tokenize(const std::string x)
{
    std::vector<std::string> strings;
    std::istringstream f(x);
    std::string s;    
    while (getline(f, s, ' ')) strings.push_back(s);
    return strings;
}

void link(std::vector<std::string>& program,
          std::vector<std::vector<std::string>>& functions)
{
    std::vector<size_t> relocs;
    relocs.reserve(functions.size());
    size_t program_size = program.size();
    for (auto& func : functions)
    {
        for (auto& line : func)
            program.push_back(line);
        relocs.push_back(program_size);
        program_size += func.size();
    }
    for (int i = 0; i < relocs.size(); ++i)
    {
        for (auto& line : program)
        {
            auto tokens = tokenize(line);
            if (!tokens.empty())
                if (tokens[0] == "PUSHL")
                    if (std::stoi(tokens[1]) == i)
                        line = std::string("PUSHL ") + std::to_string(relocs[i]);
        }        
    }
}

int main()
{
    std::vector<std::string> program;
    std::vector<std::vector<std::string>> functions;
    parse_list("(define atom? (lambda (x) (cond (null? x) 1 \
    											(func? x) 1 \
    											(str? x) 1 \
    											(int? x) 1 \
    											(1) 0)))").compile(program, functions);
    parse_list("(define first (lambda (x) (cond (atom? x) x (1) (car x))))").compile(program, functions);
    parse_list("(define rest  (lambda (x) (cond (atom? x) Nil (1) (cdr x))))").compile(program, functions);
    // parse_list("(define odd? (lambda (x) (eq (- x (* (/ x 2) 2)) 1)))").compile(program, functions);
    parse_list("(define not (lambda (x) (cond (eq x 0) 1 (1) 0)))").compile(program, functions);
    // parse_list("(define even? (lambda (x) (not (odd? x))))").compile(program, functions);
    // parse_list("(define square (lambda (x) (* x x)))").compile(program, functions);
    // parse_list("(define add (lambda (x y) (+ x y)))").compile(program, functions);
    parse_list("(define append (lambda (x y) (cond (null? x) y \
                                                (1) (cons (first x) (append (rest x) y)))))").compile(program, functions);
    parse_list("(define apply (lambda (f l) (cond (atom? l) (f l) (1) (begin (f (first l)) (apply f (rest l))))))").compile(program, functions);
    // parse_list("(define accum (lambda (op start l) \
    //                                   (cond (null? l) start \
    //                                         (1) (op (car l) (accum op start (cdr l))))))").compile(program, functions);
    // parse_list("(define reverse (lambda (l) (begin \
    // 											(define rev-aux (lambda (x y) \
    // 																		(cond (null? x) y \
    // 											      							  (1) (rev-aux \
    // 																			   			(rest x) \
    // 																			   			(cons \
    // 																			   				(first x) \
    // 																			   				y))))) \
    // 											(rev-aux l Nil))))").compile(program, functions);
    // parse_list("(define filter (lambda (pred l) \
    //                                     (cond (null? l) Nil \
    //                                           (1) (append (cond (pred (first l)) (first l) \
    //                                                             (1) Nil) \
    //                                                       (filter pred (rest l))))))").compile(program, functions);
    // parse_list("(define qsort (lambda (l) (begin (define f (first l)) \
    //                                              (define r (rest l)) \
    //                                              (define <= (lambda (x) (lambda (y) (cond (eq x y) 1 \
    //                                                                                       (less y x) 1 \
    //                                                                                       (1) 0)))) \
    //                                              (define > (lambda (x) (lambda (y) (cond (not (eq x y)) (cond (not (less y x)) 1 \
    //                                                                                            					   (1) 0) \
    //                                                                                      (1) 0)))) \
    //                                              (cond (null? l) Nil \
    //                                                    (null? f) Nil \
    //                                                    (null? r) f \
    //                                                    (1) (append (qsort (filter (<= f) r)) \
    //                                                                (append f (qsort (filter (> f) r))))))))").compile(program, functions);
    // parse_list("(define map (lambda (f l) (cond (null? l) Nil (1) (append (f (first l)) (map f (rest l))))))").compile(program, functions);
    // parse_list("(define faux (lambda (x a) (cond (eq x 1) a (1) (faux (- x 1) (* x a)))))").compile(program, functions);
    // parse_list("(define factl (lambda (x) (faux x 1)))").compile(program, functions);
    parse_list("(define prnel (lambda (x) (begin (print x) (print))))").compile(program, functions);
    parse_list("(define length (lambda (l) (cond (null? l) 0 (atom? l) 1 (1) (+ 1 (length (rest l))))))").compile(program, functions);
    // get nth element
    parse_list("(define nthaux (lambda (c n l) (cond (eq n c) (first l) (1) (cond (atom? l) Nil (1) (nthaux (+ c 1) n (rest l))))))").compile(program, functions);
    parse_list("(define nth (lambda (n l) (nthaux 0 n l)))").compile(program, functions);
    // set nth element
    parse_list("(define fstnax (lambda (c n l) (cond (eq n c) (first l) (1) (cons (first l) (fstnax (+ c 1) n (rest l))))))").compile(program, functions);
    parse_list("(define firstn (lambda (n l) (fstnax 1 n l)))").compile(program, functions);
    parse_list("(define stnaux (lambda (c n l v) (cond (eq n c) (cond (atom? l) v (1) (cons v (rest l))) (1) (cond (atom? l) l (1) (stnaux (+ c 1) n (rest l) v)))))").compile(program, functions);
    parse_list("(define setnth (lambda (n l x) (append (firstn n l) (stnaux 0 n l x))))").compile(program, functions);
    // generate a list of elements with given value
    parse_list("(define gen1 (lambda (x) (cond (eq x 1) 1 (1) (cons 1 (gen1 (- x 1))))))").compile(program, functions);
    // inner loop, return pair <list, x>
    parse_list("(define inloop (lambda (l x n) (cons (setnth n l (% x n)) (+ (* 10 (nth (- n 1) l)) (/ x n)))))").compile(program, functions);
    // main loop
    parse_list("(define mnloop (lambda (l x n) (cond (eq n 1)  (inloop l x n) (1) (begin (define r (inloop l x n)) (mnloop (car r) (cdr r) (- n 1))))))").compile(program, functions);
    // outer loop
    parse_list("(define otloop (lambda (l x n) (cond (eq n 10) (print) (1) (begin (define r (mnloop l x n)) (print (cdr r)) (otloop (car r) (cdr r) (- n 1))))))").compile(program, functions);

/*

int main() {
      int N = 109, a[109], x;
      for (int n = N - 1; n > 0; --n) a[n] = 1;
      a[1] = 2;

      while (N > 9) {
          int n = N--;
          while (--n) {
              a[n] = x % n;
              x = 10 * a[n-1] + x/n;
          }
          printf("%d", x);
      }
      return 0;
  }

*/

    parse_list("(define l1 (cons 0 (cons 2 (gen1 37))))").compile(program, functions);
    parse_list("(otloop l1 0 38)").compile(program, functions);
    parse_list("(print)").compile(program, functions);
    parse_list("(gc)").compile(program, functions);
    program.push_back("FIN");
    link(program, functions);
    for (auto x : program)
        cout << x << endl;
    return 0;
}

