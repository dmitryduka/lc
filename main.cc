#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <memory>
#include <chrono>
#include <cstring>

using std::cout;
using std::cerr;
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
   for (size_t i = 1; i < list.size(); ++i)
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
                for (size_t i = 1; i < list.size() - 1; ++i)
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
                for (size_t i = 1; i < list.size(); ++i)
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
                for (size_t i = 0; i < conditions.size(); ++i)
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
                        for (size_t j = i + 1; j < conditions.size(); ++j)
                            jump += conditions[j].size() + results[j].size() + 4;
                        program.push_back("RJMP " + std::to_string(jump));
                    }
                }                
            }
            else if (list[0].name == "lambda")
            {
                // create new environment and bind arguments
                const size_t args_count = list[1].list.size();
                size_t retcount = 0;
                std::vector<std::string> func;
                func.push_back("LOADENV");
                func.push_back("STOREENV");
                for (size_t i = 0; i < args_count; ++i)
                {
                    func.push_back("LOADENV");
                    func.push_back("PUSHFS " + std::to_string(3 + args_count - i)); // 3 - PC, env and fp
                    func.push_back("PUSHS " + list[1].list[i].name);
                    func.push_back("CONS");
                    func.push_back("CONS");
                    func.push_back("STOREENV");
                }
                // compile body
                list[2].compile(func, functions);
                if (args_count == 0)
                {
                    func.push_back("SWAP 2");
                    func.push_back("SWAP 1");
                    func.push_back("SWAP 0");
                }
                else
                {
                    func.push_back("SWAP " + std::to_string(2 + args_count));
                    func.push_back("POP");
                    retcount = args_count - 1;
                }
                func.push_back("RET " + std::to_string(retcount));
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
                    if (symbol.size() > 6) { std::cout << "Long names are not supported: " << symbol << endl; exit(1); }
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
    for (size_t i = 0; i < relocs.size(); ++i)
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

static size_t removed_instructions = 0;

std::vector<std::string> remove_instructions(std::vector<std::string>& f, size_t start, size_t remove_count)
{
    // array of jumps' line # covering line 'start'
    std::vector<size_t> jumps;
    size_t lineno = 0;
    // fill jumps map
    for (auto& line : f)
    {
        auto tokens = tokenize(line);
        auto& op = tokens[0];
        if (op == "RJZ" || op == "RJNZ" || op == "RJMP")
             if (start > lineno && start < lineno + std::stoi(tokens[1])) 
                jumps.push_back(lineno);
        lineno += 1;
    }
    auto result = f;
    for (auto j : jumps)
    {
        auto tokens = tokenize(result[j]);
        std::string op = tokens[0] + " ";
        size_t jmp = std::stoi(tokens[1]);
        if (j < start) jmp -= remove_count;
        else jmp += remove_count;
        op += std::to_string(jmp);
        result[j] = op;
    }
    result.erase(result.begin() + start, result.begin() + start + remove_count);
    removed_instructions += remove_count;
    return result;
}

std::vector<std::string> cond_optimize(const std::vector<std::string>& func)
{
    auto f = func;
    for (size_t i = 2; i < f.size(); ++i)
    {
        if (tokenize(f[i])[0] == "POP" &&
            tokenize(f[i - 1])[0] == "RJZ")
        {
            auto tokens = tokenize(f[i - 2]);
            if (tokens[0] == "PUSHCI" && std::stoi(tokens[1]) > 0)
            {
                f = remove_instructions(f, i - 2, 3);
                i = 2;
            }
        }
    }
    return f;
}

std::vector<std::string> get_function_arguments(const std::vector<std::string>& f)
{
    std::vector<std::string> bound_names;
    for (size_t i = 5; i < f.size(); ++i)
        if (f[i] == "STOREENV" &&
            f[i - 1] == "CONS" &&
            f[i - 2] == "CONS" &&
            tokenize(f[i - 3])[0] == "PUSHS" &&
            tokenize(f[i - 4])[0] == "PUSHFS" &&
            f[i - 5] == "LOADENV")
            bound_names.push_back(tokenize(f[i - 3])[1]);
    return bound_names;    
}

std::vector<std::string> funarg_optimize(const std::vector<std::string>& func)
{
    auto f = func;
    const std::vector<std::string> bound_names = get_function_arguments(f);

    // check this function doesn't produce lambda
    // otherwise we can't remove argument binding to env
    // but we still can use FP to take arguments from stack
    // rather than deferencing them from env
    bool produces_lambda = false;
    for (auto line : f)
    {
        auto tokens = tokenize(line);
        if (tokens[0] == "PUSHL" && tokens[1] != "-1")
            produces_lambda = true;
    }

    if (!produces_lambda)
        for (size_t i = 5; i < f.size(); ++i)
            if (f[i] == "STOREENV" &&
                f[i - 1] == "CONS" &&
                f[i - 2] == "CONS" &&
                tokenize(f[i - 3])[0] == "PUSHS" &&
                tokenize(f[i - 4])[0] == "PUSHFS" &&
                f[i - 5] == "LOADENV")
            {
                f = remove_instructions(f, i - 5, 6);
                i = 4;       
            }

    for (size_t i = 14; i < f.size(); ++i)
        if (f[i] == "POP" &&
            tokenize(f[i - 1])[0] == "SWAP" &&
            f[i - 2] == "CDR" &&
            f[i - 3] == "POP" &&
            f[i - 4] == "POP" &&
            tokenize(f[i - 5])[0] == "RJMP" &&
            f[i - 6] == "CDR" &&
            f[i - 7] == "POP" &&
            f[i - 8] == "POP" &&
            f[i - 9] == "POP" &&
            tokenize(f[i - 10])[0] == "RJNZ" &&
            tokenize(f[i - 11])[0] == "EQSI" &&
            f[i - 12] == "PUSHCAR" &&
            f[i - 13] == "PUSHCAR" &&
            f[i - 14] == "LOADENV")
    {
        const std::string name = tokenize(f[i - 11])[1];
        auto it = std::find(bound_names.begin(), bound_names.end(), name);
        if (it != bound_names.end())
        {
            size_t index = it - bound_names.begin();
            f = remove_instructions(f, i - 14, 14);
            f[i - 14] = std::string("PUSHFP ") + std::to_string(-int32_t(bound_names.size() - index - 1));
            i = 13;
        }
    }    
    return f;
}

// cond optimization: eliminate (PUSHCI 1, RJZ, POP)
// functions argument optimization: eliminate defining/searching for arguments in the env
void optimize(std::vector<std::string>& program,
                std::vector<std::vector<std::string>>& functions)
{
    size_t cond_removed_instructions = 0, funarg_removed_instructions = 0;
    for (auto& func : functions)
    {
        func = cond_optimize(func); 
        cond_removed_instructions += removed_instructions;
        removed_instructions = 0;
        func = funarg_optimize(func);
        funarg_removed_instructions += removed_instructions;
        removed_instructions = 0;
    }
    cerr << "cond_optimized: removed " << cond_removed_instructions << " instructions" << endl;
    cerr << "funarg_optimized: removed " << funarg_removed_instructions << " instructions" << endl;
}

std::vector<std::string> break_into_forms(const std::vector<std::string>& input)
{
    std::string p = std::accumulate(input.begin(), input.end(), std::string(""));
    std::vector<std::string> result;
    size_t bracket_count = 0;
    std::string form;
    char prevc = 'x'; // any non whitespace character will do
    for (auto c : p)
    {
        if ((isspace(c) && !isspace(prevc)) || !isspace(c)) form += c;
        if (c == '(') bracket_count += 1;
        else if (c == ')') bracket_count -= 1;

        if (!bracket_count && !form.empty())
        {
            result.push_back(form);
            form.clear();
        }
        prevc = c;
    }
    return result;
}

int main(int argc, char** argv)
{
    // read input program
    std::string line;
    std::vector<std::string> input;
    while (std::getline(std::cin, line))
       input.push_back(line);
    // reorganize input to have each form on the separate line
    input = break_into_forms(input);
    std::vector<std::string> program;
    std::vector<std::vector<std::string>> functions;
    // compile each form
    for (auto form : input)
        parse_list(form.c_str()).compile(program, functions);
    program.push_back("FIN");
    // optionally optimize the program
    if (argc > 1 && strcmp(argv[1],"-o") == 0)
        optimize(program, functions);
    // link program
    link(program, functions);
    // print bytecode
    for (auto x : program)
        cout << x << endl;
    return 0;
}

