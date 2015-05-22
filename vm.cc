#include <iostream>
#include <vector>
#include <cstdint>
#include <sstream>
#include <fstream>

using std::cout;
using std::endl;

const size_t STACK_SIZE  = 100000;
const size_t MEMORY_SIZE = 100000;

struct Cell
{
    enum Type : uint8_t { Nil, Pair, Int, String, Lambda };
    Type type;
    char refcount;
    union {
        char      string[8];
        int       integer;
        struct {
            uint32_t  lambda_addr;
            Cell*     lambda_env;
        };
        struct {
            Cell* left;
            Cell* right;
        };
    };

    Cell(Type x) : type(x), refcount(0), left(nullptr), right(nullptr) {}
    std::string type_to_string()
    {
        if (type == Pair) return "Pair";
        else if (type == Int) return "Int";
        else if (type == String) return "String";
        else if (type == Lambda) return "Lambda";
        return "Unknown";
    }
    std::string data_to_string()
    {
        if (type == Pair) return "Pair";
        else if (type == Int) return std::to_string(integer);
        else if (type == String) return std::string(string);
        else if (type == Lambda) return std::to_string(lambda_addr);
        return "Unknown";
    }
    std::string pp() { return type_to_string() + ": " + data_to_string(); }

    static Cell make_integer(int x) { Cell c(Int); c.integer = x; return c; }
    static Cell make_lambda(size_t x) { Cell c(Lambda); c.lambda_addr = x; return c; }
    static Cell make_nil() { return Cell(Nil); }
    static Cell make_string(const char* x) { 
        Cell c(String);
        for (int i = 0; i < sizeof(string) - 1; ++i)
            if (x[i]) c.string[i] = x[i];
            else break;
        return c; 
    }
    static Cell make_pair(Cell* left, Cell* right) { Cell c(Pair); c.left = left; c.right = right; return c; }
} __attribute__((packed));

struct VM
{
    std::vector<Cell> stack;
    std::vector<Cell> memory;
    Cell* env;
    bool stop;
    int pc;
    int ticks;
    
    VM() : env(nullptr), stop(false), pc(0), ticks(0) 
    { 
    	stack.reserve(STACK_SIZE); memory.reserve(MEMORY_SIZE);
    	memory.push_back(Cell(Cell::Pair));
    	env = &memory[memory.size() - 1];
    }

    int get_env_size() 
    { 
        int c = 0; 
        Cell* cur = env; 
        while (cur->left && cur->left->left) { cur = cur->right; c++; } 
        return c; 
    }

    void panic(const std::string& op, const std::string& text) { cout << "PANIC: " << op << ", " << text << endl; stop = true; }

    std::vector<std::string> tokenize(const std::string x)
    {
        std::vector<std::string> strings;
        std::istringstream f(x);
        std::string s;    
        while (getline(f, s, ' ')) strings.push_back(s);
        return strings;
    }

    void run(const std::vector<std::string>& program)
    {
        pc = 0;
        while (pc < program.size())
        {
            step(program[pc]);
            if (stop) break;
        }
    }

    void step(const std::string& instruction)
    {
        bool dont_step_pc = false;
        auto tokens = tokenize(instruction);
        if (tokens.empty()) return;
        const std::string op = tokens[0];
        if (op == "PUSHCI")
        {
            stack.push_back(Cell::make_integer(std::stoi(tokens[1])));
        }
        else if (op == "PUSHS")
        {
            stack.push_back(Cell::make_string(tokens[1].c_str()));
        }
        else if (op == "ADD")
        {
            if (stack.size() < 2) return panic(op, "Not enough elements on the stack");
            Cell x = stack.back(); stack.pop_back();
            Cell y = stack.back(); stack.pop_back();
            if (x.type != Cell::Int || y.type != Cell::Int) return panic(op, "Type mismatch");
            stack.push_back(Cell::make_integer(y.integer + x.integer));
        }
        else if (op == "SUB")
        {
            if (stack.size() < 2) return panic(op, "Not enough elements on the stack");
            Cell x = stack.back(); stack.pop_back();
            Cell y = stack.back(); stack.pop_back();
            if (x.type != Cell::Int || y.type != Cell::Int) return panic(op, "Type mismatch");
            stack.push_back(Cell::make_integer(y.integer - x.integer));
        }
        else if (op == "MUL")
        {
            if (stack.size() < 2) return panic(op, "Not enough elements on the stack");
            Cell x = stack.back(); stack.pop_back();
            Cell y = stack.back(); stack.pop_back();
            if (x.type != Cell::Int || y.type != Cell::Int) return panic(op, "Type mismatch");
            stack.push_back(Cell::make_integer(y.integer * x.integer));
        }
        else if (op == "DIV")
        {
            if (stack.size() < 2) return panic(op, "Not enough elements on the stack");
            Cell x = stack.back(); stack.pop_back();
            Cell y = stack.back(); stack.pop_back();
            if (x.type != Cell::Int || y.type != Cell::Int) return panic(op, "Type mismatch");
            stack.push_back(Cell::make_integer(y.integer / x.integer));
        }
        else if (op == "DEF")
        {
            if (stack.empty()) return panic(op, "Not enough elements on the stack");
            Cell xy = stack.back(); stack.pop_back();
            memory.push_back(xy);
           	memory.push_back(*env);
           	env->right = &memory.back(); 
           	env->left = &memory[memory.size() - 2];
        }
        else if (op == "LOADENV")
        {
            stack.push_back(*env);
        }
        else if (op == "STOREENV")
        {
            if (stack.empty()) panic(op, "Not enough elements on the stack");
            // migrate env from stack to memory
            Cell e = stack.back(); stack.pop_back();
            memory.push_back(e);
            env = &memory[memory.size() - 1];
        }
        else if (op == "CONS")
        {
            if (stack.size() < 2) return panic(op, "Not enought elements on the stack");
            {
                // migrate left and right from stack to memory
                Cell x = stack.back(); stack.pop_back();
                Cell y = stack.back(); stack.pop_back();
                memory.push_back(x); memory.push_back(y);
            }
            const size_t memsize = memory.size();
            Cell* right = &memory[memsize - 1];
            Cell* left = &memory[memsize - 2];
            stack.push_back(Cell::make_pair(left, right));            
        }
        else if (op == "PUSHCAR")
        {
            if (stack.empty()) return panic(op, "Empty stack");
            //if (stack.back().type != Cell::Pair) return panic(op, "Type mismatch");
            if (stack.back().type == Cell::Int || 
                stack.back().type == Cell::String ||
                stack.back().type == Cell::Nil)                 
                stack.push_back(stack.back());
            if (stack.back().left)
                stack.push_back(*stack.back().left);
            else
                stack.push_back(Cell::make_nil());
        }
        else if (op == "PUSHCDR")
        {
            if (stack.empty()) return panic(op, "Empty stack");
            //if (stack.back().type != Cell::Pair) return panic(op, "Type mismatch");
            if (stack.back().type == Cell::Int || 
                stack.back().type == Cell::String ||
                stack.back().type == Cell::Nil)                 
                stack.push_back(Cell::make_nil());
            if (stack.back().right)
                stack.push_back(*stack.back().right);
            else
                stack.push_back(Cell::make_nil());
        }
        else if (op == "EQ")
        {
            if (stack.size() < 2) return panic(op, "Not enought elements on the stack");
            Cell x = stack.back(); stack.pop_back();
            Cell y = stack.back(); stack.pop_back();
            if (x.type != y.type) return panic(op, "Type mismatch");
            if (x.type == Cell::Int)
            {
                if (x.integer == y.integer) stack.push_back(Cell::make_integer(1));
                else stack.push_back(Cell::make_integer(0));
            }
            else if (x.type == Cell::String)
            {
                if (std::string(x.string) == std::string(y.string)) 
                    stack.push_back(Cell::make_integer(1));
                else stack.push_back(Cell::make_integer(0));
            }
            else if (x.type == Cell::Nil)
                stack.push_back(Cell::make_integer(1));
            else if (x.type == Cell::Lambda)
            {
                if (x.lambda_addr == y.lambda_addr) stack.push_back(Cell::make_integer(1));
                else stack.push_back(Cell::make_integer(0));
            }
            else return panic(op, "Comparing pairs is not supported");
        }
        else if (op == "LT")
        {
            if (stack.size() < 2) return panic(op, "Not enought elements on the stack");
            Cell x = stack.back(); stack.pop_back();
            Cell y = stack.back(); stack.pop_back();
            if (x.type != y.type) return panic(op, "Type mismatch");
            if (x.type == Cell::Int)
            {
                if (y.integer < x.integer) stack.push_back(Cell::make_integer(1));
                else stack.push_back(Cell::make_integer(0));
            }
            else return panic(op, "Type mismatch");
        }
        else if (op == "EQT")
        {
            if (stack.size() < 2) return panic(op, "Not enought elements on the stack");
            Cell& x = stack[stack.size() - 1];
            Cell& y = stack[stack.size() - 2];
            if (x.type == y.type) stack.push_back(Cell::make_integer(1));
            else stack.push_back(Cell::make_integer(0));            
        }
        else if (op == "EQSI")
        {
            if (stack.empty()) return panic(op, "Empty stack");
            if (stack.back().type != Cell::String) return panic(op, "Type mismatch");
            stack.push_back(Cell::make_integer(tokens[1] == stack.back().string ? 1 : 0));            
        }
        else if (op == "RJNZ")
        {
            if (stack.empty()) return panic(op, "Empty stack");
            if (stack.back().type != Cell::Int) return panic(op, "Type mismatch");
            if (stack.back().integer)
            {
                pc += std::stoi(tokens[1]);
                dont_step_pc = true;
            }
        }
        else if (op == "RJZ")
        {
            if (stack.empty()) return panic(op, "Empty stack");
            if (stack.back().type != Cell::Int) return panic(op, "Type mismatch");
            if (!stack.back().integer)
            {
                pc += std::stoi(tokens[1]);
                dont_step_pc = true;
            }
        }
        else if (op == "RJMP")
        {
            pc += std::stoi(tokens[1]);
            dont_step_pc = true;
        }
        else if (op == "JMP")
        {
            pc = std::stoi(tokens[1]);
            dont_step_pc = true;
        }
        else if (op == "PUSHNIL")
        {
            stack.push_back(Cell::make_nil());
        }
        else if (op == "PUSHPC")
        {
            stack.push_back(Cell::make_integer(pc));
        }
        else if (op == "PUSHL")
        {
            Cell l = Cell::make_lambda(std::stoi(tokens[1]));
            l.lambda_env = env;
            stack.push_back(l);
        }
        else if (op == "PUSHFS")
        {
            stack.push_back(stack[stack.size() - std::stoi(tokens[1]) - 1]);
        }
        else if (op == "FIN")
        {
            stop = true;
        }
        else if (op == "CALL")
        {
            if (stack.empty()) return panic(op, "Empty stack");
            if (stack.back().type != Cell::Lambda) return panic(op, "Type mismatch");
            int old_pc = pc;
            pc = stack.back().lambda_addr;
            Cell oldenv = *env;
            if (stack.back().lambda_env) env = stack.back().lambda_env;
            else return panic(op, "Lambda has no bound env");
            stack.pop_back();            
            stack.push_back(Cell::make_integer(old_pc + 1));
            stack.push_back(oldenv);
            dont_step_pc = true;                        
        }
        else if (op == "RET")
        {
            // migrate env from stack to memory
            Cell e = stack.back(); stack.pop_back();
            memory.push_back(e);
            env = &memory.back();
            pc = stack.back().integer; stack.pop_back();
            dont_step_pc = true;
        }
        else if (op == "POP")
        {
            if (stack.empty()) return panic(op, "Empty stack");
            stack.pop_back();
        }
        else if (op == "CAR")
        {
            if (stack.empty()) return panic(op, "Empty stack");
            if (stack.back().type != Cell::Pair) return panic(op, "Type mismatch");
            stack.back() = *stack.back().left;
        }
        else if (op == "CDR")
        {
            if (stack.empty()) return panic(op, "Empty stack");
            if (stack.back().type != Cell::Pair) return panic(op, "Type mismatch");
            stack.back() = *stack.back().right;
        }
        else if (op == "SWAP")
        {
            if (stack.size() < 2) return panic(op, "Not enought elements on the stack");
            Cell tmp = stack.back();
            stack.back() = stack[stack.size() - 2 - std::stoi(tokens[1])];
            stack[stack.size() - 2 - std::stoi(tokens[1])] = tmp;
        }
        if (!dont_step_pc) pc += 1;
        ticks += 1;
    }
    
    void debug()
    {
        cout << "VM info" << endl;
        cout << "  PC: " << pc << endl;
        cout << "  Ticks: " << ticks << endl;
        cout << "  Stack size: " << stack.size() << endl;
        cout << "  Stack contents: " << endl;
        for (auto x : stack)
            cout << "    " << x.pp() << endl;            
        cout << "  Env size: " << get_env_size() << endl;
        Cell* cur = env;
        while (cur->left && cur->left->left)
        {
            cout << "    " << cur->left->left->pp() << " -> " << cur->left->right->pp() << endl;
            cur = cur->right;
        }
        cout << "  Memory size: " << memory.size() << endl;
    }

    void dump_graph()
    {
        std::ofstream ofs("graph.txt");
        ofs << "digraph test {" << endl;
        for (auto& x : memory)
        {
            Cell* p = &x;
            std::string xtype;
            if (x.type == Cell::Pair) xtype = p == env ? "ENV_P_" : "P_";
            else if (x.type == Cell::Lambda) xtype = "L_";
            else if (x.type == Cell::Int) xtype = "I_" + std::to_string(x.integer) + "_";
            else if (x.type == Cell::Nil) xtype = "N_";
            else if (x.type == Cell::String) xtype = "S_" + std::string(x.string) + "_";
            for (auto& y : memory)
                if (y.type == Cell::Pair)
                    if (y.left == p || y.right == p) 
                    { 
                        ofs << "edge [style=solid,color=black];" << endl;
                        ofs << (&y == env ? "ENV_P_" : "P_") << &y << " -> " << xtype << p << endl;
                        if (!y.refcount) ofs << (&y == env ? "ENV_P_" : "P_") << &y << " [style=filled,color=gray];" << endl;
                        if (!p->refcount) ofs << xtype << p << " [style=filled,color=gray];" << endl;
                    }
                    else if (x.type == Cell::Lambda && x.lambda_env == &y)
                    {
                        ofs << "edge [style=dashed,color=red];" << endl;
                        ofs << "L_" << p << " -> P_" << &y  << endl;                         
                        if (!y.refcount) ofs << "P_" << &y << " [style=filled,color=gray];" << endl;
                        if (!p->refcount) ofs << "L_" << p << " [style=filled,color=gray];" << endl;
                    }
                else if (y.type == Cell::Lambda)
                    if (y.lambda_env == p)
                    { 
                        ofs << "edge [style=dashed,color=red];" << endl;
                        ofs << "L_" << &y << " -> " << xtype << p << endl; 
                        if (!y.refcount) ofs << "L_" << &y << " [style=filled,color=gray];" << endl;
                        if (!p->refcount) ofs << xtype << p << " [style=filled,color=gray];" << endl;
                    }
        }
        ofs << "}" << endl;
    }

    void gc_recursive(Cell* root)
    {
        if (!root) return;
        if (root->refcount) return;
        root->refcount++;
        if (root->type == Cell::Lambda) return gc_recursive(root->lambda_env);
        if (root->type == Cell::Pair) { gc_recursive(root->left); gc_recursive(root->right); }
    }

    void gc()
    {
        size_t used = 0;
        cout << "Garbage collecting: " << memory.size() << " cells" << endl;
        gc_recursive(env->left);
        gc_recursive(env->right);
        std::ofstream ofs("stat.txt");
        for (auto& x : memory)
            if (x.refcount)
            {
                used += 1;
                ofs << "1" << endl;
            }
            else ofs << "0" << endl;                
        cout << "  Found orphaned cells: " << memory.size() - used << endl;
        cout << "  Found used cells: " << used << endl;
    }
};


int main()
{
    std::string line;
    std::vector<std::string> program;
    while (std::getline(std::cin, line))
       program.push_back(line);
    VM vm;   
    vm.run(program);
    vm.debug();
    vm.gc();
    return 0;    
}

