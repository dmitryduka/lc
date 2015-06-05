#include <iostream>
#include <fstream>
#include <vector>
#include <set>
#include <sstream>
#include <memory>
#include <unordered_map>
#include <algorithm>
#include <chrono>

struct Env;

using std::cout;
using std::endl;
using std::shared_ptr;

std::set<Env*> envs;

struct Cell
{
    enum CellType { Symbol, Func, Int, List, Nil, Tag, Unknown } type;
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
    void compile(std::vector<std::string>&, std::vector<std::vector<std::string>>&) const;

    static std::string type_to_string(const CellType type)
    {
        if (type == List) return "List";
        else if (type == Symbol) return "Symbol";
        else if (type == Func) return "Func";
        else if (type == Nil) return "Nil";
        else if (type == Int) return "Int";
        else if (type == Tag) return "Tag";
        else return "Unknown";
    }
};

static Cell Nil(Cell::Nil);

struct AddOp {  int operator()(int x, int y) { return x + y; } };
struct MinusOp { int operator()(int x, int y) { return x - y; } };
struct MulOp { int operator()(int x, int y) { return x * y; } };
struct DivOp { int operator()(int x, int y) { return x / y; } };
struct ModOp { int operator()(int x, int y) { return x % y; } };
struct EqOp { int operator()(int x, int y) { return x == y ? 1 : 0; } };
struct LessOp { int operator()(int x, int y) { return x < y ? 1 : 0; } };

template<typename Op>
Cell binary_func(const Cell& cell, shared_ptr<Env> env)
{
    // should have at least 3 cells inside (op arg1 arg2)
    if (cell.list.size() < 3) return Cell(Cell::Nil);
    Cell result(cell.list[1].eval(env).as_int);
    for (int i = 2; i < cell.list.size(); ++i)
    {
        Cell c = cell.list[i].eval(env);
        result.as_int = Op()(result.as_int, c.as_int);
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

Cell func_func(const Cell& cell, shared_ptr<Env> env)
{
    return cell.list[1].eval(env).type == Cell::Func ? Cell(1) : Cell(0);
}

Cell list_func(const Cell& cell, shared_ptr<Env> env)
{
    Cell::CellType type = cell.list[1].eval(env).type;
    if (type == Cell::List) return Cell(1);
    return Cell(0);
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
    return Nil;
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

Cell begin_func(const Cell& cell, shared_ptr<Env> env)
{
    // TODO: add checks
    for (int i = 0; i < cell.list.size() - 1; ++i)
        cell.list[i].eval(env);
    return cell.list[cell.list.size() - 1].eval(env);
}

Cell quote_func(const Cell& cell, shared_ptr<Env> env)
{
    // TODO: add checks
    return cell.list[1];
}

Cell eval_func(const Cell& cell, shared_ptr<Env> env)
{
    // TODO: add checks
    return cell.list[1].type == Cell::Symbol ? cell.list[1].eval(env).eval(env) :
           cell.list[1].type == Cell::List ? cell.list[1].eval(env) : Nil;
}

Cell print_func(const Cell& cell, shared_ptr<Env> env)
{
    if (cell.list.size() == 1) cout << endl;
    else {
        const Cell& result = cell.list[1].eval(env);
        if (result.type == Cell::Nil) cout << "Nil";
        else if (result.type == Cell::Int) cout << result.as_int;
        else if (result.type == Cell::Func) cout << "Func";
        else if (result.type == Cell::List) cout << "List";
        else if (result.type == Cell::Symbol) cout << cell.name;
        cout << " ";
    }
    return Nil;
}

Cell args_func(const Cell& cell, shared_ptr<Env> env)
{
    return cell.type == Cell::Func ? cell.list[0] : Nil;
}

Cell tagbody_func(const Cell& cell, shared_ptr<Env> env)
{
    if (cell.list.size() > 1)
    {
        bool goto_active = true;
        while (goto_active)
        {
            Cell active_tag(Cell::Tag);
            for (int i = 0; i < cell.list.size(); ++i)
            {
                if (active_tag.type == Cell::Tag && !active_tag.name.empty())
                    if(!(cell.list[i].type == Cell::Symbol &&
                        cell.list[i].name == active_tag.name)) continue;

                if (cell.list[i].type == Cell::List)
                {
                    goto_active = false;
                    active_tag = cell.list[i].eval(env);
                    if (active_tag.type == Cell::Tag) 
                    {
                        goto_active = true;
                        break;
                    }
                }
            }
        }
    }
    return Nil;
}

Cell goto_func(const Cell& cell, shared_ptr<Env> env)
{
    if (cell.list.size() == 2 &&
        cell.list[1].type == Cell::Symbol)
    {
        Cell result(Cell::Tag);
        result.name = cell.list[1].name;
        return result;
    }
    return Nil;
}

Cell set_func(const Cell& cell, shared_ptr<Env> env);
Cell define_func(const Cell& cell, shared_ptr<Env> env);
Cell undef_func(const Cell& cell, shared_ptr<Env> env);
Cell lambda_func(const Cell& cell, shared_ptr<Env> env);
Cell let_func(const Cell& cell, shared_ptr<Env> env);

struct Env
{
    std::unordered_map<std::string, Cell> data;
    shared_ptr<Env> outer;

    Env() { envs.insert(this); }
    ~Env() { envs.erase(this); }
    Env(shared_ptr<Env> outer_env) : outer(outer_env) { envs.insert(this); }

    Cell& lookup(const std::string& name)
    {
        if (data.count(name)) return data[name];
        else if (outer) return outer->lookup(name);
        else return Nil;  // unbound variable, should cause error here
    }

    void set(const std::string& name, const Cell& cell) { data[name] = cell; }

    void unset(const std::string& name) 
    {
        if (data.count(name)) data.erase(name);
        else outer->unset(name); 
    }
    void clear() 
    {
        data.clear();
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
        env->data["%"] = Cell(&binary_func<ModOp>);
        env->data["eq"] = Cell(&binary_func<EqOp>);
        env->data["less"] = Cell(&binary_func<LessOp>);
        env->data["null?"] = Cell(&null_func);
        env->data["func?"] = Cell(&func_func);
        env->data["list?"] = Cell(&list_func);
        env->data["define"] = Cell(&define_func);
        env->data["cond"] = Cell(&cond_func);
        env->data["set"] = Cell(&set_func);
        env->data["lambda"] = Cell(&lambda_func);
        env->data["car*"] = Cell(&car_func);
        env->data["cdr*"] = Cell(&cdr_func);
        env->data["let"] = Cell(&let_func);
        env->data["begin"] = Cell(&begin_func);
        env->data["undef"] = Cell(&undef_func);
        env->data["quote"] = Cell(&quote_func);
        env->data["eval"] = Cell(&eval_func);
        env->data["print"] = Cell(&print_func);
        env->data["args*"] = Cell(&args_func);
        env->data["tagbody"] = Cell(&tagbody_func);
        env->data["goto"] = Cell(&goto_func);
        return env;
    }
};

Cell lambda_func(const Cell& cell, shared_ptr<Env> env)
{
    Cell result(Cell::Func);
    result.list.reserve(2);
    result.list.push_back(cell.list[1]); // push formal argument names (that'll be a list of symbols)
    result.list.push_back(cell.list[2]); // push lambda body
    return result;
}

Cell set_func(const Cell& cell, shared_ptr<Env> env)
{
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
    else if (type == Symbol) cout << name << endl;
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
    else if (type == Tag) return *this;
    else if (type == Func) return *this;
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
                shared_ptr<Env> local_env(new Env(car.bound_env ? car.bound_env : env));
//                if (local_env->outer == car.bound_env) local_env->outer = env;
                // evaluate arguments,
                // create new Env with evaluated arguments assigned to formal arguments
                if (args.list.size() == list.size() - 1)
                    for (int i = 0; i < args.list.size(); ++i)
                        local_env->set(args.list[i].name, list[i + 1].eval(env));
                else return Nil; // TODO: in case number of actual and formal arguments differ signal error somehow
                // and call eval for a list stored in Env for Func
                Cell result = car.list[1].eval(local_env);
                if (result.type == Func && !result.bound_env)  result.bound_env = local_env;
                return result;
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

void dump_graph()
{
    std::ofstream ofs("graph.txt");
    ofs << "digraph test {" << endl;
    for (auto& it : envs)
    {
        ofs << "edge [style=solid,color=black];" << endl;
        ofs << "p" << &*it;
        if (it->outer) ofs << " -> p" << &*(it->outer) << ";";
        ofs << endl;
        for (auto& envit : it->data)
        {
           //cout << envit.first << endl;
           std::string name = envit.first;
           std::replace(name.begin(), name.end(), '*', '_');
           std::replace(name.begin(), name.end(), '+', '_');
           std::replace(name.begin(), name.end(), '-', '_');
           std::replace(name.begin(), name.end(), '/', '_');
           std::replace(name.begin(), name.end(), '?', '_');
           ofs << "edge [style=dashed,color=red];" << endl;
           ofs << "p" << &*it << " -> p"  << &*it << "_" << name << ";" << endl;                
           if (envit.second.bound_env)
           {
                ofs << "edge [style=solid,color=black];" << endl;
                ofs <<  "p" << &*it << "_" << name << " -> p" << &*envit.second.bound_env << ";" << endl;                 
           }
        }
    }
    ofs << "}" << endl;
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
    parse_list("(define otloop (lambda (l x n) (cond (eq n 10) (print) (1) (begin (define r (mnloop l x n)) (gc) (print (cdr r)) (otloop (car r) (cdr r) (- n 1))))))").compile(program, functions);

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


    parse_list("(define l1 (cons 0 (cons 2 (gen1 17))))").compile(program, functions);
    parse_list("(define l2 (otloop l1 0 18))").compile(program, functions);
    parse_list("(apply prnel (car l2))").compile(program, functions);
    parse_list("(print)").compile(program, functions);
    parse_list("(gc)").compile(program, functions);
    program.push_back("FIN");
    link(program, functions);
    for (auto x : program)
        cout << x << endl;
    return 0;
}

