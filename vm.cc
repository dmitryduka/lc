#include <iostream>
#include <vector>
#include <cstdint>
#include <sstream>
#include <fstream>

#include <jit/jit.h>
#include <jit/jit-dump.h>

using std::cout;
using std::endl;

const size_t STACK_SIZE  = 500;
const size_t MEMORY_SIZE = 100000;

enum CellType : uint8_t { Nil, Pair, Int, String, Lambda };

std::string type_to_string(CellType type)
{
    if (type == Pair) return "Pair";
    else if (type == Int) return "Int";
    else if (type == String) return "String";
    else if (type == Lambda) return "Lambda";
    else if (type == Nil) return "Nil";
    return "Unknown";
}

template<typename T>
std::string data_to_string(const T& x)
{
    if (x.type == Pair) return "Pair";
    else if (x.type == Int) return std::to_string(x.integer);
    else if (x.type == String) return std::string(x.string);
    else if (x.type == Lambda) return std::to_string(x.lambda_addr);
    return "Unknown";
}

struct Cell
{
    CellType type;
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

    Cell(CellType x) : type(x), refcount(0), left(nullptr), right(nullptr) {}
    std::string pp() { return type_to_string(type) + ": " + data_to_string(*this); }

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

struct JitCell
{
    struct
    {
        union
        {
            struct {
                uint64_t            integer : 60;
                uint64_t            type : 4;
            } __attribute__((packed));
            struct {
                char                string[7];
                uint8_t             dummy_string : 8;
            } __attribute__((packed));
            struct {
                uint32_t            left : 30;
                uint32_t            right : 30;
                uint32_t            dummy_pair : 4;
            } __attribute__((packed));
            struct {
                uint32_t            lambda_addr : 32;
                uint32_t            lambda_env : 28;
                uint32_t            dummy_lambda : 4;
            } __attribute__((packed));
            uint64_t                as64;
        };
    } __attribute__((packed));
    JitCell() : as64(0) { }
    static JitCell make_integer(int x) { JitCell r; r.type = Int; r.integer = x; return r; }
    static JitCell make_nil() { JitCell r; r.type = Nil; return r; }
    static JitCell make_string(const std::string& x) { 
        JitCell r;
        r.type = String; 
        for (int i = 0; i < sizeof(string) - 1; ++i)
            if (x[i]) r.string[i] = x[i];
            else break;
        return r;
    }
    static JitCell make_lambda(int x) { JitCell r; r.type = Lambda; r.lambda_addr = x; return r; }
    static JitCell make_pair(uint32_t x, uint32_t y) { JitCell r; r.type = Pair; r.left = x; r.right = y; return r; }

    std::string pp() { return type_to_string(static_cast<CellType>(type)) + " : " + data_to_string(*this); }
}  __attribute__((packed));

struct VM
{
    // interpreter variables
    std::vector<Cell> stack;
    std::vector<Cell> memory;
    Cell* env;
    Cell nil;
    bool stop;
    int pc;
    int ticks;
    int stack_historic_max_size;
    // jit environment variables
    std::vector<JitCell> jit_stack;
    std::vector<JitCell> jit_memory;
    std::vector<jit_label_t> jit_jump_table;
    JitCell jit_env;
    size_t jit_stack_ptr;
    size_t jit_memory_ptr;
    uint64_t jit_dbg;
    jit_context_t ctx;
    jit_function_t main;
    jit_value_t stack_addr;
    jit_value_t stack_ptr;
    jit_value_t memory_addr;
    jit_value_t memory_ptr;
    jit_value_t env_ptr;
    jit_value_t dbg_ptr;

    VM() : env(nullptr), 
            nil(Cell::make_nil()), 
            stop(false), 
            pc(0), 
            ticks(0), 
            stack_historic_max_size(0), 
            ctx(nullptr), 
            jit_dbg(0)
    { 
    	stack.reserve(STACK_SIZE); memory.reserve(MEMORY_SIZE);
    	memory.push_back(Cell(Pair));
    	env = &memory[memory.size() - 1];
    }

    ~VM()
    {
        if (ctx) jit_context_destroy(ctx);
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
        if (ctx) prepare_jump_tables(program);
        while (pc < program.size())
        {
            if(ctx) step_jit(program[pc]);
            else step_interpret(program[pc]);
            stack_historic_max_size = stack.size() > stack_historic_max_size ? stack.size() : stack_historic_max_size;
            if (stop) 
                if (ctx)
                {
                    jit_function_set_optimization_level(main, JIT_OPTLEVEL_NORMAL);
                    jit_function_compile(main);
                    jit_int result = 0;
                    jit_function_apply(main, nullptr, &result);
                }
                else break;
        }
    }

    void step_interpret(const std::string& instruction)
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
            if (x.type != Int || y.type != Int) return panic(op, "Type mismatch");
            stack.push_back(Cell::make_integer(y.integer + x.integer));
        }
        else if (op == "SUB")
        {
            if (stack.size() < 2) return panic(op, "Not enough elements on the stack");
            Cell x = stack.back(); stack.pop_back();
            Cell y = stack.back(); stack.pop_back();
            if (x.type != Int || y.type != Int) return panic(op, "Type mismatch");
            stack.push_back(Cell::make_integer(y.integer - x.integer));
        }
        else if (op == "MUL")
        {
            if (stack.size() < 2) return panic(op, "Not enough elements on the stack");
            Cell x = stack.back(); stack.pop_back();
            Cell y = stack.back(); stack.pop_back();
            if (x.type != Int || y.type != Int) return panic(op, "Type mismatch");
            stack.push_back(Cell::make_integer(y.integer * x.integer));
        }
        else if (op == "DIV")
        {
            if (stack.size() < 2) return panic(op, "Not enough elements on the stack");
            Cell x = stack.back(); stack.pop_back();
            Cell y = stack.back(); stack.pop_back();
            if (x.type != Int || y.type != Int) return panic(op, "Type mismatch");
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
            stack.push_back(*xy.left);
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
            // migrate left and right from stack to memory
            const Cell x = stack.back(); stack.pop_back();
            const Cell y = stack.back(); stack.pop_back();
            if (x.type != Nil) memory.push_back(x); 
            if (y.type != Nil) memory.push_back(y); 
            const size_t memsize = memory.size();
            Cell* right = y.type != Nil ? &memory[memsize - 1] : &nil;
            Cell* left =  x.type != Nil ? (y.type != Nil ? &memory[memsize - 2] : &memory[memsize - 1]) : &nil;
            stack.push_back(Cell::make_pair(left, right));            
        }
        else if (op == "PUSHCAR")
        {
            if (stack.empty()) return panic(op, "Empty stack");
            //if (stack.back().type != Pair) return panic(op, "Type mismatch");
            if (stack.back().type == Int || 
                stack.back().type == String ||
                stack.back().type == Nil)                 
                stack.push_back(stack.back());
            if (stack.back().left)
                stack.push_back(*stack.back().left);
            else
                stack.push_back(Cell::make_nil());
        }
        else if (op == "PUSHCDR")
        {
            if (stack.empty()) return panic(op, "Empty stack");
            //if (stack.back().type != Pair) return panic(op, "Type mismatch");
            if (stack.back().type == Int || 
                stack.back().type == String ||
                stack.back().type == Nil)                 
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
            if (x.type == Int)
            {
                if (x.integer == y.integer) stack.push_back(Cell::make_integer(1));
                else stack.push_back(Cell::make_integer(0));
            }
            else if (x.type == String)
            {
                if (std::string(x.string) == std::string(y.string)) 
                    stack.push_back(Cell::make_integer(1));
                else stack.push_back(Cell::make_integer(0));
            }
            else if (x.type == Nil)
                stack.push_back(Cell::make_integer(1));
            else if (x.type == Lambda)
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
            if (x.type == Int)
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
            if (stack.back().type != String) return panic(op, "Type mismatch");
            stack.push_back(Cell::make_integer(tokens[1] == stack.back().string ? 1 : 0));            
        }
        else if (op == "RJNZ")
        {
            if (stack.empty()) return panic(op, "Empty stack");
            if (stack.back().type != Int) return panic(op, "Type mismatch");
            if (stack.back().integer)
            {
                pc += std::stoi(tokens[1]);
                dont_step_pc = true;
            }
        }
        else if (op == "RJZ")
        {
            if (stack.empty()) return panic(op, "Empty stack");
            if (stack.back().type != Int) return panic(op, "Type mismatch");
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
            if (stack.back().type != Lambda) return panic(op, "Type mismatch");
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
            if (stack.back().type != Pair) return panic(op, "Type mismatch");
            stack.back() = *stack.back().left;
        }
        else if (op == "CDR")
        {
            if (stack.empty()) return panic(op, "Empty stack");
            if (stack.back().type != Pair) return panic(op, "Type mismatch");
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
        if (ctx)
        {
            cout << "Jump table size: " << jit_jump_table.size() << endl;
            cout << "Disassembly:" << endl;
            jit_dump_function(stdout, main, "main");
            cout << "Stack:" << endl;
            for (int i = jit_stack_ptr - 1; i >= 0; --i)
                cout << "    " << jit_stack[i].pp() << endl;
            cout << "Memory:" << endl;            
            for (int i = jit_memory_ptr + 1; i >= 0; --i)
                cout << "    " << jit_memory[i].pp() << endl;
        }
        else
        {
            cout << "VM info" << endl;
            cout << "  PC: " << pc << endl;
            cout << "  Ticks: " << ticks << endl;
            cout << "  Stack historic max size: " << stack_historic_max_size << endl;
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
    }

    void dump_graph()
    {
        std::ofstream ofs("graph.txt");
        ofs << "digraph test {" << endl;
        for (auto& x : memory)
        {
            Cell* p = &x;
            std::string xtype;
            if (x.type == Pair) xtype = p == env ? "ENV_P_" : "P_";
            else if (x.type == Lambda) xtype = "L_";
            else if (x.type == Int) xtype = "I_" + std::to_string(x.integer) + "_";
            else if (x.type == Nil) xtype = "N_";
            else if (x.type == String) xtype = "S_" + std::string(x.string) + "_";
            for (auto& y : memory)
                if (y.type == Pair)
                    if (y.left == p || y.right == p) 
                    { 
                        ofs << "edge [style=solid,color=black];" << endl;
                        ofs << (&y == env ? "ENV_P_" : "P_") << &y << " -> " << xtype << p << endl;
                        if (!y.refcount) ofs << (&y == env ? "ENV_P_" : "P_") << &y << " [style=filled,color=gray];" << endl;
                        if (!p->refcount) ofs << xtype << p << " [style=filled,color=gray];" << endl;
                    }
                    else if (x.type == Lambda && x.lambda_env == &y)
                    {
                        ofs << "edge [style=dashed,color=red];" << endl;
                        ofs << "L_" << p << " -> P_" << &y  << endl;                         
                        if (!y.refcount) ofs << "P_" << &y << " [style=filled,color=gray];" << endl;
                        if (!p->refcount) ofs << "L_" << p << " [style=filled,color=gray];" << endl;
                    }
                else if (y.type == Lambda)
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
        if (root->type == Lambda) return gc_recursive(root->lambda_env);
        if (root->type == Pair) { gc_recursive(root->left); gc_recursive(root->right); }
    }

    void gc()
    {
        size_t used = 0;
        cout << "Garbage collecting: " << memory.size() << " cells" << endl;
        gc_recursive(env->left);
        gc_recursive(env->right);
        for (auto& x : memory) if (x.refcount) used += 1;
        cout << "  Found orphaned cells: " << memory.size() - used << endl;
        cout << "  Found used cells: " << used << endl;
    }

    void init_jit()
    {
        jit_stack.resize(STACK_SIZE);
        jit_memory.resize(MEMORY_SIZE);
        jit_stack_ptr = 0;
        jit_memory_ptr = 0;
        ctx = jit_context_create();
        jit_type_t signature = jit_type_create_signature(jit_abi_cdecl, jit_type_void, nullptr, 0, 1);
        main = jit_function_create(ctx, signature);
        // bind jit stack
        jit_constant_t stack_addr_const;
        stack_addr_const.type = jit_type_void_ptr;
        stack_addr_const.un.ptr_value = &jit_stack[0];
        stack_addr = jit_value_create_constant(main, &stack_addr_const);
        // bind jit memory
        jit_constant_t memory_addr_const;
        memory_addr_const.type = jit_type_void_ptr;
        memory_addr_const.un.ptr_value = &jit_memory[2]; // 0 - nil, 1 - env, 2 .. MEMORY_SIZE - memory
        memory_addr = jit_value_create_constant(main, &memory_addr_const);
        // bind jit stack pointer
        jit_constant_t stack_ptr_const;
        stack_ptr_const.type = jit_type_void_ptr;
        stack_ptr_const.un.ptr_value = &jit_stack_ptr;
        stack_ptr = jit_value_create_constant(main, &stack_ptr_const);
        // bind jit memory pointer
        jit_constant_t memory_ptr_const;
        memory_ptr_const.type = jit_type_void_ptr;
        memory_ptr_const.un.ptr_value = &jit_memory_ptr;
        memory_ptr = jit_value_create_constant(main, &memory_ptr_const);
        // bind jit dbg var
        jit_constant_t dbg_ptr_const;
        dbg_ptr_const.type = jit_type_void_ptr;
        dbg_ptr_const.un.ptr_value = &jit_dbg;
        dbg_ptr = jit_value_create_constant(main, &dbg_ptr_const);
        // bind jit env var
        jit_constant_t env_ptr_const;
        env_ptr_const.type = jit_type_void_ptr;
        env_ptr_const.un.ptr_value = &jit_memory[1]; // 0 - nil, 1 - env, 2 .. MEMORY_SIZE - memory
        env_ptr = jit_value_create_constant(main, &env_ptr_const);
        jit_insn_store_relative(main, env_ptr, 0, jit_value_create_long_constant(main, jit_type_ulong, 0x1000000000000000ull));
    }

    void prepare_jump_tables(const std::vector<std::string>& program)
    {
        size_t jumps = 0;
        for (const auto& instr : program)
        {
            auto x = tokenize(instr)[0];
            if (x == "CALL" || x == "RET" || 
                x == "RJMP" || x == "JMP" || 
                x == "RJNZ" || x == "RJNZ")
                jumps += 1;
        }
        jit_jump_table.resize(jumps);
    }

    void step_jit(const std::string& instruction)
    {
        auto tokens = tokenize(instruction);
        static jit_value_t c8 = jit_value_create_nint_constant(main, jit_type_uint, 8);
        static jit_value_t c2 = jit_value_create_nint_constant(main, jit_type_uint, 2);
        static jit_value_t c1 = jit_value_create_nint_constant(main, jit_type_uint, 1);
        static jit_value_t cm1 = jit_value_create_nint_constant(main, jit_type_int, -1);
        static jit_value_t cm2 = jit_value_create_nint_constant(main, jit_type_int, -2);
        static jit_value_t ctypemask = jit_value_create_long_constant(main, jit_type_ulong, 0xF000000000000000l);
        static jit_value_t cdatamask = jit_value_create_long_constant(main, jit_type_ulong, 0x0FFFFFFFFFFFFFFFl);

        if (tokens.empty()) return;
        const std::string op = tokens[0];
        if (op == "FIN") stop = true;
        else if (op == "PUSHCI" || op == "PUSHNIL" || op == "PUSHS" || op == "PUSHL")
        {
            // increment sp
            JitCell cell;
            if (op == "PUSHCI") cell = JitCell::make_integer(std::stoi(tokens[1]));
            else if(op == "PUSHNIL") cell = JitCell::make_nil();
            else if(op == "PUSHS") cell = JitCell::make_string(tokens[1]);
            else if(op == "PUSHL") cell = JitCell::make_lambda(std::stoi(tokens[1])); // TODO
            // current sp
            jit_value_t sp = jit_insn_load_relative(main, stack_ptr, 0, jit_type_int);
            // stack_addr + sp
            jit_value_t stack_addr_offsetted = jit_insn_add(main, stack_addr, jit_insn_mul(main, sp, c8));
            // store
            jit_insn_store_relative(main, stack_addr_offsetted, 0, jit_value_create_long_constant(main, jit_type_long, cell.as64));
            // modify sp
            jit_insn_store_relative(main, stack_ptr, 0, jit_insn_add(main, sp, c1));
        }
        else if (op == "ADD" || op == "SUB" || op == "MUL" || op == "DIV" || op == "EQ" || op == "LT")
        {
            // current sp
            jit_value_t sp = jit_insn_load_relative(main, stack_ptr, 0, jit_type_int);
            jit_value_t sp_1 = jit_insn_add(main, sp, cm1);
            jit_value_t sp_2 = jit_insn_add(main, sp, cm2);
            jit_value_t v1_addr = jit_insn_add(main, stack_addr, jit_insn_mul(main, sp_1, c8));
            jit_value_t v2_addr = jit_insn_add(main, stack_addr, jit_insn_mul(main, sp_2, c8));
            // load sp-1 snd sp-2 values, save v1 type and clear type bits on both values
            jit_value_t v1t = jit_insn_load_relative(main, v1_addr, 0, jit_type_long);
            jit_value_t t1 = jit_insn_and(main, v1t, ctypemask);
            jit_value_t v1 = jit_insn_and(main, v1t, cdatamask);
            jit_value_t v2 = jit_insn_and(main, jit_insn_load_relative(main, v2_addr, 0, jit_type_long), cdatamask);
            // add them as 64 bit values
            jit_value_t r;
            if (op == "ADD") r = jit_insn_add(main, v1, v2);
            else if (op == "SUB") r = jit_insn_add(main, jit_insn_neg(main, v1), v2);
            else if (op == "MUL") r = jit_insn_mul(main, v1, v2);
            else if (op == "DIV") r = jit_insn_div(main, v2, v1);
            else if (op == "EQ")  r = jit_insn_eq(main, v1, v2);
            else if (op == "LT")  r = jit_insn_lt(main, v2, v1); // TODO: check this
            // and fix type in case result occupies more than 60 bits
            jit_value_t rf = jit_insn_or(main, jit_insn_and(main, r, cdatamask), t1);
            // store value on top of the stack
            jit_insn_store_relative(main, v2_addr, 0, rf);
            // modify sp
            jit_insn_store_relative(main, stack_ptr, 0, sp_1);
        }
        else if (op == "POP")
        {
            jit_value_t sp = jit_insn_load_relative(main, stack_ptr, 0, jit_type_int);
            jit_insn_store_relative(main, stack_ptr, 0, jit_insn_add(main, sp, c1));
        }
        else if (op == "CONS")
        {
            // current sp
            jit_value_t sp = jit_insn_load_relative(main, stack_ptr, 0, jit_type_int);
            jit_value_t sp_1 = jit_insn_add(main, sp, cm1);
            jit_value_t sp_2 = jit_insn_add(main, sp, cm2);
            jit_value_t v1_addr = jit_insn_add(main, stack_addr, jit_insn_mul(main, sp_1, c8));
            jit_value_t v2_addr = jit_insn_add(main, stack_addr, jit_insn_mul(main, sp_2, c8));
            // load sp-1 snd sp-2 values, save v1 type and clear type bits on both values
            jit_value_t v1 = jit_insn_load_relative(main, v1_addr, 0, jit_type_long);
            jit_value_t v2 = jit_insn_load_relative(main, v2_addr, 0, jit_type_long);
            // migrate values to memory and modify mp
            jit_value_t mp = jit_insn_convert(main, jit_insn_load_relative(main, memory_ptr, 0, jit_type_int), jit_type_ulong, 0);
            jit_value_t mp1 = jit_insn_add(main, mp, c1);
            jit_value_t v1m_addr = jit_insn_add(main, memory_addr, jit_insn_mul(main, mp, c8));
            jit_value_t v2m_addr = jit_insn_add(main, memory_addr, jit_insn_mul(main, mp1, c8));
            jit_insn_store_relative(main, v1m_addr, 0, v1);
            jit_insn_store_relative(main, v2m_addr, 0, v2);
            jit_insn_store_relative(main, memory_ptr, 0, jit_insn_add(main, mp, c2));
            // create a pair and place it on the stack
            jit_value_t mp1s = jit_insn_shl(main, mp1, jit_value_create_nint_constant(main, jit_type_uint, 30));
            // modify sp
            jit_value_t pair = jit_insn_or(main, jit_insn_or(main, mp1s, mp),
                                                 jit_value_create_long_constant(main, jit_type_ulong, 0x1000000000000000ull));
            // store
            jit_insn_store_relative(main, jit_insn_add(main, stack_addr, jit_insn_mul(main, sp_2, c8)), 0, pair);
            // modify sp
            jit_insn_store_relative(main, stack_ptr, 0, sp_1);
        }
        else if (op == "SWAP")
        {
            jit_value_t sp = jit_insn_load_relative(main, stack_ptr, 0, jit_type_int);
            jit_value_t sp_v1 = jit_insn_add(main, sp, jit_value_create_nint_constant(main, jit_type_int, -1));
            jit_value_t sp_v2 = jit_insn_add(main, sp, jit_value_create_nint_constant(main, jit_type_int, -(std::stoi(tokens[1]) + 2)));
            jit_value_t v1_addr = jit_insn_add(main, stack_addr, jit_insn_mul(main, sp_v1, c8));
            jit_value_t v2_addr = jit_insn_add(main, stack_addr, jit_insn_mul(main, sp_v2, c8));
            // load values
            jit_value_t v1 = jit_insn_load_relative(main, v1_addr, 0, jit_type_ulong);
            jit_value_t v2 = jit_insn_load_relative(main, v2_addr, 0, jit_type_ulong);
            // store them in different order
            jit_insn_store_relative(main, v1_addr, 0, v2);            
            jit_insn_store_relative(main, v2_addr, 0, v1);
        }
        else if (op == "DEF")
        {
        }
        else if (op == "PUSHCAR" || op == "PUSHCDR" || op == "CAR" || op == "CDR")
        {
            bool car = (op == "PUSHCAR" || op == "CAR") ? true : false;
            bool remove_from_stack = (op == "CAR" || op == "CDR") ? true : false;
            jit_value_t sp = jit_insn_load_relative(main, stack_ptr, 0, jit_type_int);
            jit_value_t sp1 = jit_insn_add(main, sp, cm1);
            jit_value_t pair_addr = jit_insn_add(main, stack_addr, jit_insn_mul(main, sp1, c8));
            // load pair from stack
            jit_value_t pair = jit_insn_load_relative(main, pair_addr, 0, jit_type_ulong);
            // read 'left' part of a cell
            jit_value_t mask;
            if (car) mask = jit_value_create_long_constant(main, jit_type_ulong, 0x000000003FFFFFFFull);
            else mask = jit_value_create_long_constant(main, jit_type_ulong, 0x0FFFFFFFC0000000ull);
            jit_value_t cell_addr = jit_insn_and(main, pair, mask);
            if (!car) cell_addr = jit_insn_shr(main, cell_addr, jit_value_create_nint_constant(main, jit_type_int, 30));
            // load left cell from memory
            jit_value_t result_addr = jit_insn_convert(main, 
                                                        jit_insn_add(main, 
                                                                        memory_addr, 
                                                                        jit_insn_mul(main, cell_addr, c8)), jit_type_uint, 0);
            if (remove_from_stack) sp = sp1;
            jit_insn_store_relative(main, jit_insn_add(main, stack_addr, jit_insn_mul(main, sp, c8)), 0, 
                                          jit_insn_load_relative(main, result_addr, 0, jit_type_ulong));
            // modify sp
            if (remove_from_stack) jit_insn_store_relative(main, stack_ptr, 0, sp1);
            else jit_insn_store_relative(main, stack_ptr, 0, jit_insn_add(main, sp, c1));
        }
        pc += 1;
    }
};


int main()
{    
    std::string line;
    std::vector<std::string> program;
    while (std::getline(std::cin, line))
       program.push_back(line);
    VM vm;
    vm.init_jit(); 
    vm.run(program);
    vm.debug();
    vm.gc();
    return 0;    
}

