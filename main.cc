#include <iostream>
#include <vector>
#include <memory>
#include <unordered_map>

struct Env;

using std::cout;
using std::endl;
using std::shared_ptr;

struct Cell
{
    enum CellType { Symbol, Func, Int, List, Nil, Unknown } type;
    typedef Cell (*FuncType)(const Cell&, shared_ptr<Env>);
    union
    {
        int as_int;
        FuncType func;
    };
    shared_ptr<Env> bound_env;
    std::vector<Cell> list;
    std::string name;

    bool operator!=(const Cell& cell) { return type != cell.type; }

    Cell() : type(List), func(NULL) {}
    Cell(CellType x) : type(x), func(NULL) {}
    Cell(FuncType x) : type(Func), func(x) {}
    Cell(int x) : type(Int), func(NULL) { as_int = x; }
    Cell(const std::string& x) : func(NULL)
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

    void pretty_print(size_t tabs_no = 1) const;
    Cell eval(shared_ptr<Env> env) const;

    static std::string type_to_string(const CellType type)
    {
        if (type == List) return "List";
        if (type == Symbol) return "Symbol";
        if (type == Func) return "Func";
        if (type == Nil) return "Nil";
        if (type == Int) return "Int";
        else return "Unknown";
    }
};

static Cell Nil(Cell::Nil);

struct AddOp {  int operator()(int x, int y) { return x + y; } };
struct MinusOp { int operator()(int x, int y) { return x - y; } };
struct MulOp { int operator()(int x, int y) { return x * y; } };
struct DivOp { int operator()(int x, int y) { return x / y; } };
struct EqOp { int operator()(int x, int y) { return x == y ? 1 : 0; } };
struct NotEqOp { int operator()(int x, int y) { return x != y ? 1 : 0; } };
struct LessOp { int operator()(int x, int y) { return x < y ? 1 : 0; } };
struct MoreOp { int operator()(int x, int y) { return x > y ? 1 : 0; } };

template<typename Op>
Cell binary_func(const Cell& cell, shared_ptr<Env> env)
{
    // should have at least 3 cells inside (op arg1 arg2)
    if (cell.list.size() < 3) return Cell(Cell::Nil);
    Cell result(cell.list[1].eval(env).as_int);
    for (int i = 2; i < cell.list.size(); ++i)
    {
        result.as_int = Op()(result.as_int, cell.list[i].eval(env).as_int);
    }
    return result;
}

Cell null_func(const Cell& cell, shared_ptr<Env> env)
{
    // checks at least one argument is Nil
    if (cell.list.size() > 1)
    {
        for (int i = 1; i < cell.list.size(); ++i)
            if (cell.list[i].eval(env).type == Cell::Nil) return Cell(1); // return true
        return Cell(0);          // return false otherwise
    }
    return Nil;
}

Cell cond_func(const Cell& cell, shared_ptr<Env> env)
{
    // doesn't handle no true predicate case
    if (cell.list.size() < 3) return Nil;
    for (int i = 1; i < cell.list.size(); i+=2)
    {
        if (cell.list[i].eval(env).as_int)
            if (cell.list.size() != i)
                return cell.list[i + 1].eval(env);
    }
}

Cell lambda_func(const Cell& cell, shared_ptr<Env> env)
{
    Cell result(Cell::Func);
    result.list.reserve(2);
    result.list.push_back(cell.list[1]); // push formal argument names (that'll be a list of symbols)
    result.list.push_back(cell.list[2]); // push lambda body
    return result;
}

Cell car_func(const Cell& cell, shared_ptr<Env> env)
{
    if (cell.list.size() != 2) return Nil;
    const Cell& result = cell.list[1].eval(env);
    if (result.list.empty()) return Nil;
    return result.list[0];
}

Cell cdr_func(const Cell& cell, shared_ptr<Env> env)
{
    if (cell.list.size() != 2) return Nil;
    const Cell& source = cell.list[1].eval(env);
    if (source.type == Cell::List && source.list.size() > 1)
    {
        Cell result(Cell::List);
        result.list.reserve(source.list.size() - 1);
        for (int i = 1; i < source.list.size(); ++i)
            result.list.push_back(source.list[i]);
        return result;
    }
    return Nil;
}

Cell list_func(const Cell& cell, shared_ptr<Env> env)
{
    Cell result(Cell::List);
    result.list.reserve(cell.list.size() - 1);
    for (int i = 1; i < cell.list.size(); ++i)
        result.list.push_back(cell.list[i].eval(env));
    return result;
}

Cell append_func(const Cell& cell, shared_ptr<Env> env)
{
    Cell result(Cell::List);
    for (int i = 1; i < cell.list.size(); ++i)
    {
        const Cell& e = cell.list[i].eval(env);
        if (e.type == Cell::List)
            for (int j = 0; j < e.list.size(); ++j)
                result.list.push_back(e.list[j]);
    }
    return result;
}

Cell let_func(const Cell& cell, shared_ptr<Env> env);

Cell begin_func(const Cell& cell, shared_ptr<Env> env)
{
    // TODO: add checks
    for (int i = 0; i < cell.list.size() - 1; ++i)
        cell.list[i].eval(env);
    return cell.list[cell.list.size() - 1].eval(env);
}

Cell set_func(const Cell& cell, shared_ptr<Env> env);
Cell define_func(const Cell& cell, shared_ptr<Env> env);
Cell undef_func(const Cell& cell, shared_ptr<Env> env);

struct Env
{
    std::unordered_map<std::string, Cell> data;
    shared_ptr<Env> outer;

    Env() : outer(NULL) {}
    Env(shared_ptr<Env> outer_env) : outer(outer_env) {}

    Cell& lookup(const std::string& name)
    {
        if (data.count(name)) return data[name];
        else if (outer) return outer->lookup(name);
        else return Nil;         // unbound variable, should cause error here
    }

    void set(const std::string& name, const Cell& cell)  { data[name] = cell; }
    void unset(const std::string& name) 
    {
        if (data.count(name)) data.erase(name);
        else outer->unset(name); 
    }

    static shared_ptr<Env> global()
    {
        shared_ptr<Env> env(new Env);
        // initialize new global env with special forms and symbols
        env->data["Nil"] = Nil;
        env->data["+"] = Cell(&binary_func<AddOp>);
        env->data["-"] = Cell(&binary_func<MinusOp>);
        env->data["*"] = Cell(&binary_func<MulOp>);
        env->data["/"] = Cell(&binary_func<DivOp>);
        env->data["eq"] = Cell(&binary_func<EqOp>);
        env->data["less"] = Cell(&binary_func<LessOp>);
        env->data["neq"] = Cell(&binary_func<NotEqOp>);
        env->data["more"] = Cell(&binary_func<MoreOp>);
        env->data["null?"] = Cell(&null_func);
        env->data["define"] = Cell(&define_func);
        env->data["cond"] = Cell(&cond_func);
        env->data["set"] = Cell(&set_func);
        env->data["lambda"] = Cell(&lambda_func);
        env->data["list"] = Cell(&list_func);
        env->data["car"] = Cell(&car_func);
        env->data["cdr"] = Cell(&cdr_func);
        env->data["append"] = Cell(&append_func);
        env->data["let"] = Cell(&let_func);
        env->data["begin"] = Cell(&begin_func);
        env->data["undef"] = Cell(&undef_func);
        return env;
    }
};

Cell set_func(const Cell& cell, shared_ptr<Env> env)
{
    // TODO: doesn't handle quotes
    if ((cell.list.size() == 3) &&
        (env->lookup(cell.list[1].name) != Nil))
    {
        env->lookup(cell.list[1].name) = cell.list[2].eval(env);
        return Cell(cell.list[1].name);
    }
    return Nil;
}

Cell define_func(const Cell& cell, shared_ptr<Env> env)
{
    if (cell.list.size() < 3) return Nil;
    env->set(cell.list[1].name, cell.list[2].eval(env));
    return Cell(cell.list[1].name);
}

Cell undef_func(const Cell& cell, shared_ptr<Env> env)
{
    // TODO: check that argument is really a symbol
    if (cell.list.size() == 2)
        env->unset(cell.list[1].name);
    return Nil;
}

Cell let_func(const Cell& cell, shared_ptr<Env> env)
{
    shared_ptr<Env> local_env(new Env(env));
    for (int i = 0; i < cell.list[1].list.size(); ++i)
    {
        local_env->set(cell.list[1].list[i].list[0].name, cell.list[1].list[i].list[1]);
    }
    Cell result = cell.list[2].eval(local_env);
    result.bound_env = local_env;
    return result;
}


void Cell::pretty_print(size_t tabs_no) const
{
    const std::string tabs(tabs_no, ' ');
    cout << tabs << type_to_string(type) << ": ";
    if (type == Int) cout << as_int << endl;
    else if (type == Symbol || type == Func) cout << name << endl;
    else if (type == List)
    {
        cout << endl;
        for (int i = 0; i < list.size(); ++i)
        {
            list[i].pretty_print(tabs_no + 2);
        }
    }
    else cout << endl;
}

Cell Cell::eval(shared_ptr<Env> env) const
{
    if (type == Int) return *this;
    else if (type == Nil) return Nil;
    else if (type == Symbol) return env->lookup(name);
    else if (type == List)
    {
        if (list.empty()) return Cell(Nil);
        const Cell car = list[0].eval(env);
        if (car.type == Func)
        {
            if (car.func) return car.func(*this, env); // that's built-in function
            else if (car.list.size() == 2) // that's lambda
            {
                const Cell& args = car.list[0];
                shared_ptr<Env> local_env;
                if (!car.bound_env) local_env.reset(new Env(env));
                else
                {
                    local_env = car.bound_env;
                    local_env->outer = env;
                }
                // evaluate arguments,
                // create new Env with evaluated arguments assigned to formal arguments
                // TODO: check if number of actual and formal arguments is the same
                for (int i = 0; i < args.list.size(); ++i)
                    local_env->set(args.list[i].name, list[i + 1].eval(env));
                // and call eval for a list stored in Env for Func
                return car.list[1].eval(local_env);
            }
        }
        else if (car.type == Int) return car;
        else if (car.type == Symbol) return env->lookup(car.name);
        else if (type == Nil) return Nil;
        else if (car.type == List)
        {
            Cell result(*this);
            for (int i = 0; i < result.list.size(); ++i)
                result.list[i] = result.list[i].eval(env);
            return result;
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


int main()
{
    shared_ptr<Env> env = Env::global();
    // lambdas
    parse_list("(define square (lambda (x) (* x x)))").eval(env);
    parse_list("(define sum-of-squares (lambda (x y) (+ (square x) (square y))))").eval(env);
    parse_list("(sum-of-squares 5 6)").eval(env).pretty_print();
    parse_list("(define apply-func (lambda (f x) (f x)))").eval(env);
    parse_list("(apply-func square 5)").eval(env).pretty_print();
    // fibonacci recursive
    parse_list("(define fibonacci (lambda (x) (cond (eq x 1) 1 (eq x 2) 1 (1) (+ (fibonacci (- x 1)) (fibonacci (- x 2))))))").eval(env);
    parse_list("(fibonacci 12)").eval(env).pretty_print();
    // lists
    parse_list("(car (list 4 5 6))").eval(env).pretty_print();
    parse_list("(define l (list 1 2 3 4 5))").eval(env);
    parse_list("(car l)").eval(env).pretty_print();
    parse_list("(cdr l)").eval(env).pretty_print();
    parse_list("(car (cdr l))").eval(env).pretty_print();
    parse_list("(cdr (cdr (cdr l)))").eval(env).pretty_print();
    // list functions
    parse_list("(define reverse (lambda (l) (cond (null? l) Nil (1) (append (reverse (cdr l)) (list (car l))))))").eval(env);
    parse_list("(define map (lambda (f l) (cond (null? l) Nil (1) (append (list (f (car l))) (map f (cdr l))))))").eval(env);
    parse_list("(reverse (map square l))").eval(env).pretty_print();
    parse_list("(define filter (lambda (pred l) \
                                        (cond (null? l) Nil \
                                              (1) (append (cond (pred (car l)) (list (car l)) \
                                                                (1) Nil) \
                                                          (filter pred (cdr l))))))").eval(env);
    parse_list("(define odd? (lambda (x) (eq (- x (* (/ x 2) 2)) 1)))").eval(env);
    parse_list("(define not (lambda (x) (cond (eq x 0) 1 (1) 0)))").eval(env);
    parse_list("(define even? (lambda (x) (not (odd? x))))").eval(env);
    parse_list("(map square (filter odd? l))").eval(env).pretty_print();
    parse_list("(map square (filter even? l))").eval(env).pretty_print();
    parse_list("(define accumulate (lambda (op start l) \
                                            (cond (null? l) start \
                                                    (1) (op (car l) (accumulate op start (cdr l))))))").eval(env);
    parse_list("(accumulate + 0 l)").eval(env).pretty_print();
    parse_list("(accumulate + 0 (map square (filter odd? l)))").eval(env).pretty_print();
    parse_list("(define remove (lambda (x l) (filter (lambda (k) (neq k x)) l)))").eval(env);
    parse_list("(remove 3 l)").eval(env).pretty_print();
 
    parse_list("(define balance 100)").eval(env);
    parse_list("(define withdraw (lambda (amount) (cond (more balance amount) (begin (set balance (- balance amount)) (balance))\
                                                        (1) Nil)))").eval(env);
    parse_list("(withdraw 10)").eval(env).pretty_print();
    parse_list("(withdraw 10)").eval(env).pretty_print();
    parse_list("(withdraw 10)").eval(env).pretty_print();
    parse_list("(withdraw 10)").eval(env).pretty_print();
 
    parse_list("(define new-withdraw (let ((balance 100)) (lambda (amount) (cond (more balance amount) (begin (set balance (- balance amount)) (balance)) (1) Nil))))").eval(env);
    parse_list("(new-withdraw 10)").eval(env).pretty_print();
    parse_list("(new-withdraw 10)").eval(env).pretty_print();
 
    parse_list("(undef new-withdraw)").eval(env);
 
    return 0;
}
