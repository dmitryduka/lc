#include <iostream>
#include <sstream>
#include <fstream>
#include <cstring>
#include <vector>
#include <chrono>

#if WITH_JIT
#include <map>
#include <jit/jit.h>
#include <jit/jit-dump.h>
#endif

#include <signal.h>

using std::cout;
using std::endl;

const size_t STACK_SIZE  = 500;
const size_t MEMORY_SIZE = 50000;

enum CellType : uint8_t { Nil, Pair, Int, String, Lambda, InstructionPointer, Environment };

struct VM;

std::string type_to_string(CellType type)
{
    if (type == Pair) return "Pair";
    else if (type == Int) return "Int";
    else if (type == String) return "String";
    else if (type == Lambda) return "Lambda";
    else if (type == Nil) return "Nil";
    else if (type == InstructionPointer) return "InstructionPointer (Call)";
    else if (type == Environment) return "Environment (Call)";
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
    Cell() : as64(0) { }
    Cell(uint64_t x) : as64(x) { }
    static Cell make_integer(int x) { Cell r; r.type = Int; r.integer = x; return r; }
    static Cell make_nil() { Cell r; r.type = Nil; return r; }
    static Cell make_string(const std::string& x) { 
        Cell r;
        r.type = String; 
        for (int i = 0; i < sizeof(string) - 1; ++i)
            if (x[i]) r.string[i] = x[i];
            else break;
        return r;
    }
    static Cell make_lambda(uint32_t addr, uint32_t env) 
    { 
        Cell r; 
        r.type = Lambda; 
        r.lambda_addr = addr; 
        r.lambda_env = env; 
        return r; 
    }
    static Cell make_pair(uint32_t x, uint32_t y) { Cell r; r.type = Pair; r.left = x; r.right = y; return r; }

    std::string pp() { return type_to_string(static_cast<CellType>(type)) + " : " + data_to_string(*this); }
}  __attribute__((packed));

void vm_print_cell(const Cell cell)
{
    if (cell.type == Int) cout << cell.integer << std::flush;
    else if (cell.type == String) cout << cell.string << std::flush;
    else if (cell.type == Nil) cout << "Nil" << endl;
}

void jit_vm_gc(VM* vm);

struct VM
{
    // VM vars
    std::vector<Cell> stack;
    std::vector<Cell> heap;
    uint32_t stack_ptr;
    uint32_t heap_ptr;
    uint32_t env_ptr;
    bool stop;
    // stat
    int pc;
    int ticks;
    int stack_historic_max_size;
    size_t jit_time;
    size_t execution_time;
    uint32_t gc_count;
    uint32_t gc_collected;
#if WITH_JIT
    // jit
    jit_context_t ctx;
    jit_function_t main;
    jit_value_t jit_stack_addr;
    jit_value_t jit_stack_ptr;
    jit_value_t jit_memory_addr;
    jit_value_t jit_memory_ptr;
    jit_value_t jit_env_ptr;
    jit_value_t jit_gc_count_ptr;
    std::map<size_t, size_t> jit_jump_map;
    std::vector<jit_label_t> jit_jump_table;
    uint32_t jit_jump_table_current_index;
#endif

    VM() :  stop(false), 
            pc(0), 
            ticks(0), 
            stack_historic_max_size(0), 
#if WITH_JIT
            ctx(nullptr),
            jit_jump_table_current_index(0),
#endif
            gc_count(0),
            gc_collected(0)
    { 
        stack.resize(STACK_SIZE);
        heap.resize(MEMORY_SIZE);
        stack_ptr = 0;
        env_ptr = 1;
        heap_ptr = 2; // 0 - nil, 1 - global env, 2 - user data
        // create default env
        heap[1] = Cell::make_pair(0, 0);
    }

    ~VM()
    {
#if WITH_JIT
        if (ctx) jit_context_destroy(ctx);
#endif
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
        auto start = std::chrono::steady_clock::now();
#if WITH_JIT
        if (ctx) prepare_jump_table(program);
#endif
        while (pc < program.size())
        {
#if WITH_JIT
            if(ctx) step_jit(program[pc]);
            else 
#endif
                step_interpret(program[pc]);
#if WITH_JIT
            if (!ctx)
#endif
                stack_historic_max_size = stack.size() > stack_historic_max_size ? stack.size() : stack_historic_max_size;
            if (stop) break;
        }
#if WITH_JIT
        if (ctx)
        {
            jit_function_set_optimization_level(main, JIT_OPTLEVEL_NORMAL);
            jit_function_compile(main);
            jit_int result = 0;
            jit_time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
            auto start = std::chrono::steady_clock::now();
            jit_function_apply(main, nullptr, &result);
            auto diff = std::chrono::steady_clock::now() - start;
            execution_time = std::chrono::duration_cast<std::chrono::microseconds>(diff).count();
        }
        else
#endif
        {
            auto diff = std::chrono::steady_clock::now() - start;
            execution_time = std::chrono::duration_cast<std::chrono::microseconds>(diff).count();
        }     
    }

    void step_interpret(const std::string& instruction)
    {
        bool dont_step_pc = false;
        auto tokens = tokenize(instruction);
        if (tokens.empty()) return;
        const std::string op = tokens[0];

        // for operations allocating heap space, check if we need to start GC
        if (op == "CONS" || op == "DEF" || op == "STOREENV")
        {
            const size_t offset = (gc_count & 1) ? (MEMORY_SIZE >> 1) : 0;
            if ((heap_ptr - offset) > ((MEMORY_SIZE >> 1) - 3)) gc();
        }

        if (op == "GC") gc();
        else if (op == "PRN")
        {
            if (stack_ptr < 1) return panic(op, "Not enough elements on the stack");
            vm_print_cell(stack[--stack_ptr]);
        }
        else if (op == "PRNL")
            vm_print_cell(Cell::make_string("\n"));
        else if (op == "PUSHCI")
            stack[stack_ptr++] = Cell::make_integer(std::stoi(tokens[1]));
        else if (op == "PUSHS")
            stack[stack_ptr++] = Cell::make_string(tokens[1].c_str());
        else if (op == "ADD" || op == "SUB" || op == "MUL" || op == "DIV" || op == "MOD")
        {
            if (stack_ptr < 2) return panic(op, "Not enough elements on the stack");
            Cell x = stack[--stack_ptr];
            Cell y = stack[--stack_ptr];
            if (x.type != Int || y.type != Int) return panic(op, "Type mismatch");
            if (op == "ADD") stack[stack_ptr++] = Cell::make_integer(y.integer + x.integer);
            else if (op == "SUB") stack[stack_ptr++] = Cell::make_integer(y.integer - x.integer);
            else if (op == "MUL") stack[stack_ptr++] = Cell::make_integer(y.integer * x.integer);
            else if (op == "DIV") stack[stack_ptr++] = Cell::make_integer(y.integer / x.integer);
            else if (op == "MOD") stack[stack_ptr++] = Cell::make_integer(y.integer % x.integer);
        }
        else if (op == "DEF")
        {
            if (!stack_ptr) return panic(op, "Not enough elements on the stack");
            Cell xy = stack[stack_ptr - 1];
            heap[heap_ptr++] = xy;
           	heap[heap_ptr++] = heap[env_ptr];
            heap[env_ptr].right = heap_ptr - 1;
            heap[env_ptr].left = heap_ptr - 2;
            stack[stack_ptr - 1] = heap[xy.left];
        }
        else if (op == "LOADENV")
            stack[stack_ptr++] = heap[env_ptr];
        else if (op == "STOREENV")
        {
            if (!stack_ptr) panic(op, "Not enough elements on the stack");
            // migrate env from stack to memory
            heap[heap_ptr++] = stack[--stack_ptr];
            env_ptr = heap_ptr - 1;
        }
        else if (op == "CONS")
        {
            if (stack_ptr < 2) return panic(op, "Not enought elements on the stack");            
            // migrate left and right from stack to memory
            const Cell x = stack[--stack_ptr];
            const Cell y = stack[--stack_ptr];
            heap[heap_ptr++] = x; 
            heap[heap_ptr++] = y;
            stack[stack_ptr++] = Cell::make_pair(heap_ptr - 2, heap_ptr - 1);
        }
        else if (op == "PUSHCAR" || op == "PUSHCDR")
        {
            if (!stack_ptr) return panic(op, "Empty stack");
            const Cell& cell = stack[stack_ptr - 1];
            if (cell.type != Pair) return panic(op, "Type mismatch");
            if (op == "PUSHCAR" && stack[stack_ptr - 1].left)
                stack[stack_ptr] = heap[stack[stack_ptr - 1].left];
            else if (op == "PUSHCAR" && stack[stack_ptr - 1].right)
                stack[stack_ptr] = heap[stack[stack_ptr - 1].right];
            else stack[stack_ptr] = Cell::make_nil();
            stack_ptr += 1;
        }
        else if (op == "EQ")
        {
            if (stack_ptr < 2) return panic(op, "Not enought elements on the stack");
            Cell x = stack[stack_ptr - 1];
            Cell y = stack[stack_ptr - 2];
            stack_ptr -= 2;
            if (x.type != y.type) return panic(op, "Type mismatch");
            if (x.type == Int)
                stack[stack_ptr++] = Cell::make_integer(x.integer == y.integer);
            else if (x.type == String)
                stack[stack_ptr++] = Cell::make_integer(std::string(x.string) == std::string(y.string));
            else if (x.type == Nil)
                stack[stack_ptr++] = Cell::make_integer(1);
            else if (x.type == Lambda)
                stack[stack_ptr++] = Cell::make_integer(x.lambda_addr == y.lambda_addr);
            else return panic(op, "Comparing pairs is not supported");
        }
        else if (op == "LT")
        {
            if (stack_ptr < 2) return panic(op, "Not enought elements on the stack");
            Cell x = stack[stack_ptr - 1];
            Cell y = stack[stack_ptr - 2];
            stack_ptr -= 2;
            if (x.type != y.type) return panic(op, "Type mismatch");
            if (x.type == Int) stack[stack_ptr++] = Cell::make_integer(y.integer < x.integer);
            else return panic(op, "Type mismatch");
        }
        else if (op == "EQT")
        {
            if (stack_ptr < 2) return panic(op, "Not enought elements on the stack");
            const Cell& x = stack[stack_ptr - 1];
            const Cell& y = stack[stack_ptr - 2];
            stack[stack_ptr++] = Cell::make_integer(x.type == y.type);
        }
        else if (op == "EQSI")
        {
            if (!stack_ptr) return panic(op, "Empty stack");
            const Cell& x = stack[stack_ptr - 1];
            if (x.type != String) return panic(op, "Type mismatch");
            stack[stack_ptr] = Cell::make_integer(tokens[1] == x.string ? 1 : 0);
            stack_ptr += 1;
        }
        else if (op == "RJNZ" || op == "RJZ")
        {
            if (!stack_ptr) return panic(op, "Empty stack");
            Cell& cell = stack[stack_ptr - 1];
            if (cell.type != Int) return panic(op, "Type mismatch");
            if ((op == "RJNZ" && cell.integer) ||
                (op == "RJZ" && !cell.integer))
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
        else if (op == "PUSHNIL") stack[stack_ptr++] = Cell::make_nil();
        else if (op == "PUSHFS")
        { 
            stack[stack_ptr] = stack[stack_ptr - std::stoi(tokens[1]) - 1];
            stack_ptr += 1;
        }
        else if (op == "FIN") stop = true;
        else if (op == "PUSHL")
            stack[stack_ptr++] = Cell::make_lambda(std::stoi(tokens[1]), env_ptr);
        else if (op == "CALL")
        {
            if (!stack_ptr) return panic(op, "Empty stack");
            Cell& cell = stack[--stack_ptr];
            if (cell.type != Lambda) return panic(op, "Type mismatch");
            const int old_pc = pc;
            const uint32_t oldenv = env_ptr;
            pc = cell.lambda_addr;
            if (cell.lambda_env) env_ptr = cell.lambda_env;
            else return panic(op, "Lambda has no bound env");
            stack[stack_ptr++] = Cell::make_integer(old_pc + 1);
            Cell oenv = Cell::make_integer(oldenv);
            oenv.type = Environment;
            stack[stack_ptr++] = oenv;
            dont_step_pc = true;                        
        }
        else if (op == "RET")
        {
            // migrate env from stack to memory
            env_ptr = stack[--stack_ptr].integer;
            pc = stack[--stack_ptr].integer;
            dont_step_pc = true;
        }
        else if (op == "POP")
        {
            if (!stack_ptr) return panic(op, "Empty stack");
            stack_ptr -= 1;
        }
        else if (op == "CAR" || op == "CDR")
        {
            if (!stack_ptr) return panic(op, "Empty stack");
            Cell& cell = stack[stack_ptr - 1];
            if (cell.type != Pair) return panic(op, "Type mismatch");
            cell = heap[op == "CAR" ? cell.left : cell.right];
        }
        else if (op == "SWAP")
        {
            // TODO: check swap argument and issue panic in case needed
            const uint32_t elno = stack_ptr - 2 - std::stoi(tokens[1]);
            if (stack_ptr < 2) return panic(op, "Not enought elements on the stack");
            Cell tmp = stack[stack_ptr - 1];
            stack[stack_ptr - 1] = stack[elno];
            stack[elno] = tmp;
        }
        else if (op == "NOP")
        {
        }
        if (!dont_step_pc) pc += 1;
        ticks += 1;
    }
    
    void debug()
    {
        // cout << "Disassembly:" << endl;
        // jit_dump_function(stdout, main, "program");
        const size_t offset = (gc_count & 1) ? (MEMORY_SIZE >> 1) : 0;
        cout << "PC: " << pc << endl;
        cout << "JIT time: " << jit_time << " us" << endl;
        cout << "Execution time: " << execution_time<< " us" << endl;
        cout << "GC ran: " << gc_count << " time(s)" << endl;
        cout << "  Collected: " << gc_collected << " cells" << endl;
        cout << "Environment pointer: " << env_ptr << endl;
        cout << "Stack size: " << stack_ptr << endl;
        cout << "Memory size: " << heap_ptr - offset << endl;
        cout << "Stack:" <<  endl;
        for (int i = stack_ptr - 1; i >= 0; --i)
            cout << "    " << stack[i].pp() << endl;
        // cout << "Memory:" << endl;
        // for (int i = offset; i < heap_ptr; ++i)
        //     cout << "    " << heap[i].pp() << endl;
    }

    void gc_mark_recursive(Cell& c)
    {
        if (c.as64 & 0x8000000000000000ull) return;
        const Cell tmp = c;
        c.as64 |= 0x8000000000000000ull;
        if (tmp.type == Lambda) gc_mark_recursive(heap[tmp.lambda_env]);
        else if (tmp.type == Pair)
        {
            gc_mark_recursive(heap[tmp.left]);
            gc_mark_recursive(heap[tmp.right]);
        }
        else if (tmp.type == Environment)
        {
            gc_mark_recursive(heap[tmp.as64 & 0x0FFFFFFFFFFFFFFFull]);
        }
    }

    size_t gc_mark()
    {
        gc_mark_recursive(heap[env_ptr]);
        for (int i = 0; i < stack_ptr; ++i)
            gc_mark_recursive(stack[i]);
        for (int i = 0; i < stack_ptr; ++i)
            stack[i].as64 &= 0x7FFFFFFFFFFFFFFFull;
        // count used, optional, for stats only
        size_t unused = 0;
        const size_t offset = (gc_count & 1) ? (MEMORY_SIZE >> 1) : 0;
        for (int i = offset; i < heap_ptr; ++i)
            if ((heap[i].as64 & 0x8000000000000000ull) == 0) 
                unused += 1;
        return unused;
    }

    void gc_scavenge()
    {
        size_t new_heap_ptr = 0;
        const size_t offset = (gc_count & 1) ? 0 : (MEMORY_SIZE >> 1);
        const size_t source_offset = (gc_count & 1) ? (MEMORY_SIZE >> 1) : 0;
        Cell* new_heap = &heap[offset], *cur_heap = new_heap;
        for (int i = source_offset; i < heap_ptr; ++i)
        {
            Cell& cell = heap[i];
            if (cell.as64 & 0x8000000000000000ull)
            {
                // copy the cell clearing 'reachable' bit
                *cur_heap = cell.as64 & 0x7FFFFFFFFFFFFFFFull;
                // save relocation info in the old cell, saving 'reachable' bit
                cell.as64 = (cur_heap - new_heap + offset) & 0x8FFFFFFFFFFFFFFFull;
                cur_heap += 1;
            }
        }
        heap_ptr = cur_heap - new_heap;
        // fix relocations
        cur_heap = new_heap;
        // stack
        for (int i = 0; i < stack_ptr; ++i)
        {
            Cell& cell = stack[i];
            if (cell.type == Pair)
            {
                cell.left = heap[cell.left].as64 & 0x7FFFFFFFFFFFFFFFull;
                cell.right = heap[cell.right].as64 & 0x7FFFFFFFFFFFFFFFull;
            }
            else if (cell.type == Lambda)
                cell.lambda_env = heap[cell.lambda_env].as64 & 0x7FFFFFFFFFFFFFFFull;
            else if (cell.type == Environment)
                cell.as64 = (heap[cell.as64 & 0x7FFFFFFFFFFFFFFFull].as64) | 0x6000000000000000ull;
        }
        // heap
        for (int i = 0; i < heap_ptr; ++i)
        {
            Cell& cell = *cur_heap++;
            if (cell.type == Pair)
            {
                cell.left = heap[cell.left].as64 & 0x7FFFFFFFFFFFFFFFull;
                cell.right = heap[cell.right].as64 & 0x7FFFFFFFFFFFFFFFull;
            }
            else if (cell.type == Lambda)
            {
                cell.lambda_env = heap[cell.lambda_env].as64 & 0x7FFFFFFFFFFFFFFFull;
            }
        }
        // save new mp
        heap_ptr = cur_heap - new_heap + offset;
        // fix ep
        env_ptr = heap[env_ptr].as64 & 0x7FFFFFFFFFFFFFFFull;
    }

    void gc()
    {
        const size_t unused = gc_mark();
        gc_collected += unused;
        gc_scavenge();
        gc_count += 1;
    }

#if WITH_JIT
    void init_jit()
    {
        ctx = jit_context_create();
        jit_type_t signature = jit_type_create_signature(jit_abi_cdecl, jit_type_void, nullptr, 0, 1);
        main = jit_function_create(ctx, signature);
        // bind jit stack
        jit_constant_t stack_addr_const;
        stack_addr_const.type = jit_type_void_ptr;
        stack_addr_const.un.ptr_value = &stack[0];
        jit_stack_addr = jit_value_create_constant(main, &stack_addr_const);
        // bind jit memory
        jit_constant_t memory_addr_const;
        memory_addr_const.type = jit_type_void_ptr;
        memory_addr_const.un.ptr_value = &heap[0]; // 0 - nil, 1 - env, 2 .. MEMORY_SIZE - memory
        jit_memory_addr = jit_value_create_constant(main, &memory_addr_const);
        // bind jit stack pointer
        jit_constant_t stack_ptr_const;
        stack_ptr_const.type = jit_type_void_ptr;
        stack_ptr_const.un.ptr_value = &stack_ptr;
        jit_stack_ptr = jit_value_create_constant(main, &stack_ptr_const);
        // bind jit memory pointer
        jit_constant_t memory_ptr_const;
        memory_ptr_const.type = jit_type_void_ptr;
        memory_ptr_const.un.ptr_value = &heap_ptr;
        jit_memory_ptr = jit_value_create_constant(main, &memory_ptr_const);
        // bind jit dbg var
        jit_constant_t gc_count_ptr_const;
        gc_count_ptr_const.type = jit_type_void_ptr;
        gc_count_ptr_const.un.ptr_value = &gc_count;
        jit_gc_count_ptr = jit_value_create_constant(main, &gc_count_ptr_const);
        // bind jit env var
        jit_constant_t env_ptr_const;
        env_ptr_const.type = jit_type_void_ptr;
        env_ptr_const.un.ptr_value = &env_ptr;
        jit_env_ptr = jit_value_create_constant(main, &env_ptr_const);
    }

    void prepare_jump_table(const std::vector<std::string>& program)
    {
        size_t jumps = 0, local_pc = 0;
        for (const auto& instr : program)
        {
            auto tokens = tokenize(instr);
            const auto& x = tokens[0];
            if (x == "CALL" || x == "RET" || x == "FIN" ||
                x == "RJMP" || x == "RJNZ" || x == "RJZ")
            {
                int jpc = 0;
                if (x == "CALL" || x == "RET" || x == "FIN") jpc = local_pc + 1;
                else jpc = local_pc + std::stoi(tokens[1]);

                if (jpc < program.size() && !jit_jump_map.count(jpc)) 
                    jit_jump_map[jpc] = jumps++;
             }
            local_pc += 1;
        }
        jit_jump_table.resize(jit_jump_map.size());
        for (auto& x : jit_jump_table) x = jit_label_undefined;
    }

    void step_jit(const std::string& instruction)
    {
        auto tokens = tokenize(instruction);
        if (tokens.empty()) return;

        static jit_value_t c8 = jit_value_create_nint_constant(main, jit_type_uint, 8);
        static jit_value_t c2 = jit_value_create_nint_constant(main, jit_type_uint, 2);
        static jit_value_t c1 = jit_value_create_nint_constant(main, jit_type_uint, 1);
        static jit_value_t cm1 = jit_value_create_nint_constant(main, jit_type_int, -1);
        static jit_value_t cm2 = jit_value_create_nint_constant(main, jit_type_int, -2);
        static jit_value_t ctypemask = jit_value_create_long_constant(main, jit_type_ulong, 0xF000000000000000l);
        static jit_value_t cdatamask = jit_value_create_long_constant(main, jit_type_ulong, 0x0FFFFFFFFFFFFFFFl);
        static jit_value_t cmemthreshold = jit_value_create_nint_constant(main, jit_type_uint, (MEMORY_SIZE >> 1) - 3);

        const std::string op = tokens[0];

        // for operations allocating heap space, check if we need to start GC
        if (op == "CONS" || op == "DEF" || op == "STOREENV")
        {
            jit_label_t run_gc = jit_label_undefined, 
                        no_gc = jit_label_undefined,
                        first_half = jit_label_undefined;
            jit_value_t mp = jit_insn_load_relative(main, jit_memory_ptr, 0, jit_type_uint);
            jit_value_t gcc = jit_insn_load_relative(main, jit_gc_count_ptr, 0, jit_type_uint);
            gcc = jit_insn_eq(main, jit_insn_and(main, gcc, c1), c1);
            jit_insn_branch_if_not(main, gcc, &first_half);
            mp = jit_insn_sub(main, mp, jit_value_create_nint_constant(main, jit_type_uint, MEMORY_SIZE >> 1));
            jit_insn_label(main, &first_half);
            jit_value_t needs_gc = jit_insn_gt(main, mp, cmemthreshold);
            jit_insn_branch_if(main, needs_gc, &run_gc);
            jit_insn_branch(main, &no_gc);
            jit_insn_label(main, &run_gc);
            jit_type_t type[] = { jit_type_void_ptr };
            jit_type_t signature = jit_type_create_signature(jit_abi_cdecl, jit_type_void, type, 1, 1);
            jit_constant_t val_const;
            val_const.type = jit_type_void_ptr;
            val_const.un.ptr_value = this;
            jit_value_t val = jit_value_create_constant(main, &val_const);
            jit_insn_call_native(main, "gc", reinterpret_cast<void*>(&jit_vm_gc), signature, &val, 1, JIT_CALL_NOTHROW);    
            jit_insn_label(main, &no_gc);
        }

        // insert label in case this is the target of a jump or instruction next to a call
        if (jit_jump_map.count(pc))
            jit_insn_label(main, &jit_jump_table[jit_jump_map[pc]]);
        
        if (op == "FIN") jit_insn_return(main, nullptr);
        else if (op == "GC")
        {
            jit_type_t type[] = { jit_type_void_ptr };
            jit_type_t signature = jit_type_create_signature(jit_abi_cdecl, jit_type_void, type, 1, 1);
            jit_constant_t val_const;
            val_const.type = jit_type_void_ptr;
            val_const.un.ptr_value = this;
            jit_value_t val = jit_value_create_constant(main, &val_const);
            jit_insn_call_native(main, "gc", reinterpret_cast<void*>(&jit_vm_gc), signature, &val, 1, JIT_CALL_NOTHROW);    
        }
        else if (op == "PRN" || op == "PRNL")
        {
            jit_type_t type[] = { jit_type_ulong };
            jit_type_t signature = jit_type_create_signature(jit_abi_cdecl, jit_type_void, type, 1, 1);
            jit_value_t val;
            if (op == "PRNL")
            {
                Cell newline = Cell::make_string("\n");
                val = jit_value_create_long_constant(main, jit_type_ulong, newline.as64);
            }
            else
            {
                jit_value_t sp = jit_insn_load_relative(main, jit_stack_ptr, 0, jit_type_uint);
                jit_value_t sp1 = jit_insn_add(main, sp, cm1);
                jit_value_t sp_addr = jit_insn_add(main, jit_stack_addr, jit_insn_mul(main, sp1, c8));
                jit_insn_store_relative(main, jit_stack_ptr, 0, sp1);
                val = jit_insn_load_relative(main, sp_addr, 0, jit_type_ulong);
            }
            jit_insn_call_native(main, "print", reinterpret_cast<void*>(&vm_print_cell), signature, &val, 1, JIT_CALL_NOTHROW);
        }
        else if (op == "PUSHCI" || op == "PUSHNIL" || op == "PUSHS" || op == "PUSHL")
        {
            // increment sp
            Cell cell;
            jit_value_t cellval;
            if (op == "PUSHCI") cell = Cell::make_integer(std::stoi(tokens[1]));
            else if(op == "PUSHNIL") cell = Cell::make_nil();
            else if(op == "PUSHS") cell = Cell::make_string(tokens[1]);
            else if(op == "PUSHL") 
            {
                uint32_t lambda_start = std::stoi(tokens[1]);
                // check if this is a dummy/test lambda for type checking (see EQT)
                if (lambda_start == -1) cell = Cell::make_lambda(0, 0);
                else
                {
                    if (jit_jump_map.count(lambda_start))
                        cell = Cell::make_lambda(jit_jump_map[lambda_start], 0);
                    else panic(op, "Lambda has no entry in the jump table");
                }            
            }

            cellval = jit_value_create_long_constant(main, jit_type_ulong, cell.as64);

            if (op == "PUSHL" && std::stoi(tokens[1]) != -1) 
            {
                jit_value_t ep = jit_insn_convert(main, jit_insn_load_relative(main, jit_env_ptr, 0, jit_type_uint), jit_type_ulong, 0);
                ep = jit_insn_shl(main, ep, jit_value_create_nint_constant(main, jit_type_uint, 32));
                cellval = jit_insn_or(main, cellval, ep);
            }
            // current sp
            jit_value_t sp = jit_insn_load_relative(main, jit_stack_ptr, 0, jit_type_uint);
            // jit_stack_addr + sp
            jit_value_t jit_stack_addr_offsetted = jit_insn_add(main, jit_stack_addr, jit_insn_mul(main, sp, c8));
            // store
            jit_insn_store_relative(main, jit_stack_addr_offsetted, 0, cellval);
            // modify sp
            jit_insn_store_relative(main, jit_stack_ptr, 0, jit_insn_add(main, sp, c1));
        }
        else if (op == "ADD" || op == "SUB" || 
                op == "MUL" || op == "DIV" || 
                op == "EQ" || op == "LT" || 
                op == "EQT" || op == "MOD")
        {
            // current sp
            jit_value_t sp = jit_insn_load_relative(main, jit_stack_ptr, 0, jit_type_uint);
            jit_value_t sp_1 = jit_insn_add(main, sp, cm1);
            jit_value_t sp_2 = jit_insn_add(main, sp, cm2);
            jit_value_t v1_addr = jit_insn_add(main, jit_stack_addr, jit_insn_mul(main, sp_1, c8));
            jit_value_t v2_addr = jit_insn_add(main, jit_stack_addr, jit_insn_mul(main, sp_2, c8));
            // load sp-1 snd sp-2 values, save v1 type and clear type bits on both values
            jit_value_t v1t = jit_insn_load_relative(main, v1_addr, 0, jit_type_long);
            jit_value_t v2t = jit_insn_load_relative(main, v2_addr, 0, jit_type_long);
            jit_value_t v1 = jit_insn_and(main, v1t, cdatamask);
            jit_value_t v2 = jit_insn_and(main, v2t, cdatamask);
            jit_value_t r;
            if (op == "ADD") r = jit_insn_add(main, v1, v2);
            else if (op == "SUB") r = jit_insn_sub(main, v2, v1);
            else if (op == "MUL") r = jit_insn_mul(main, v1, v2);
            else if (op == "DIV") r = jit_insn_div(main, v2, v1);
            else if (op == "MOD") r = jit_insn_rem(main, v2, v1);
            else if (op == "EQ")  r = jit_insn_eq(main, v1, v2);
            else if (op == "LT")  r = jit_insn_lt(main, v2, v1); // TODO: check this
            else if (op == "EQT") r = jit_insn_eq(main, jit_insn_and(main, v1t, ctypemask), jit_insn_and(main, v2t, ctypemask));
            // and fix type in case result occupies more than 60 bits
            jit_value_t rf = jit_insn_or(main, jit_insn_and(main, r, cdatamask), 
                                            jit_value_create_long_constant(main, jit_type_ulong, Cell::make_integer(0).as64));
            // store value on top of the stack
             // EQT operations doesn't pop operands from stack
            if (op == "EQT") v2_addr = jit_insn_add(main, jit_stack_addr, jit_insn_mul(main, sp, c8));
            jit_insn_store_relative(main, v2_addr, 0, rf);
            // modify sp
            if (op == "EQT") sp_1 = jit_insn_add(main, sp, c1);
            jit_insn_store_relative(main, jit_stack_ptr, 0, sp_1);
        }
        else if (op == "POP")
        {
            jit_value_t sp = jit_insn_load_relative(main, jit_stack_ptr, 0, jit_type_uint);
            jit_insn_store_relative(main, jit_stack_ptr, 0, jit_insn_add(main, sp, cm1));
        }
        else if (op == "CONS")
        {
            // current sp
            jit_value_t sp = jit_insn_load_relative(main, jit_stack_ptr, 0, jit_type_uint);
            jit_value_t sp_1 = jit_insn_add(main, sp, cm1);
            jit_value_t sp_2 = jit_insn_add(main, sp, cm2);
            jit_value_t v1_addr = jit_insn_add(main, jit_stack_addr, jit_insn_mul(main, sp_1, c8));
            jit_value_t v2_addr = jit_insn_add(main, jit_stack_addr, jit_insn_mul(main, sp_2, c8));
            // load sp-1 snd sp-2 values, save v1 type and clear type bits on both values
            jit_value_t v1 = jit_insn_load_relative(main, v1_addr, 0, jit_type_long);
            jit_value_t v2 = jit_insn_load_relative(main, v2_addr, 0, jit_type_long);
            // migrate values to memory and modify mp
            jit_value_t mp = jit_insn_convert(main, jit_insn_load_relative(main, jit_memory_ptr, 0, jit_type_uint), jit_type_ulong, 0);
            jit_value_t mp1 = jit_insn_add(main, mp, c1);
            jit_value_t v1m_addr = jit_insn_add(main, jit_memory_addr, jit_insn_mul(main, mp, c8));
            jit_value_t v2m_addr = jit_insn_add(main, jit_memory_addr, jit_insn_mul(main, mp1, c8));
            jit_insn_store_relative(main, v1m_addr, 0, v1);
            jit_insn_store_relative(main, v2m_addr, 0, v2);
            jit_insn_store_relative(main, jit_memory_ptr, 0, jit_insn_convert(main, jit_insn_add(main, mp, c2), jit_type_uint, 0));
            // create a pair and place it on the stack
            jit_value_t mp1s = jit_insn_shl(main, mp1, jit_value_create_nint_constant(main, jit_type_uint, 30));
            // modify sp
            jit_value_t pair = jit_insn_or(main, jit_insn_or(main, mp1s, mp),
                                                 jit_value_create_long_constant(main, jit_type_ulong, 0x1000000000000000ull));
            // store
            jit_insn_store_relative(main, jit_insn_add(main, jit_stack_addr, jit_insn_mul(main, sp_2, c8)), 0, pair);
            // modify sp
            jit_insn_store_relative(main, jit_stack_ptr, 0, sp_1);
        }
        else if (op == "SWAP")
        {
            jit_value_t sp = jit_insn_load_relative(main, jit_stack_ptr, 0, jit_type_uint);
            jit_value_t sp_v1 = jit_insn_add(main, sp, cm1);
            jit_value_t sp_v2 = jit_insn_add(main, sp, jit_value_create_nint_constant(main, jit_type_int, -(std::stoi(tokens[1]) + 2)));
            jit_value_t v1_addr = jit_insn_add(main, jit_stack_addr, jit_insn_mul(main, sp_v1, c8));
            jit_value_t v2_addr = jit_insn_add(main, jit_stack_addr, jit_insn_mul(main, sp_v2, c8));
            // load values
            jit_value_t v1 = jit_insn_load_relative(main, v1_addr, 0, jit_type_ulong);
            jit_value_t v2 = jit_insn_load_relative(main, v2_addr, 0, jit_type_ulong);
            // store them in different order
            jit_insn_store_relative(main, v1_addr, 0, v2);
            jit_insn_store_relative(main, v2_addr, 0, v1);
        }
        else if (op == "PUSHFS")
        {
            jit_value_t sp = jit_insn_load_relative(main, jit_stack_ptr, 0, jit_type_uint);
            jit_value_t sp_v1 = jit_insn_add(main, sp, jit_value_create_nint_constant(main, jit_type_int, -(std::stoi(tokens[1]) + 1)));
            jit_value_t sp_addr = jit_insn_add(main, jit_stack_addr, jit_insn_mul(main, sp, c8));
            jit_value_t v1_addr = jit_insn_add(main, jit_stack_addr, jit_insn_mul(main, sp_v1, c8));
            jit_value_t v1 = jit_insn_load_relative(main, v1_addr, 0, jit_type_ulong);
            jit_insn_store_relative(main, sp_addr, 0, v1);
            // modify sp
            jit_insn_store_relative(main, jit_stack_ptr, 0, jit_insn_add(main, sp, c1));
        }
        else if (op == "DEF")
        {
            jit_value_t sp = jit_insn_load_relative(main, jit_stack_ptr, 0, jit_type_uint);
            jit_value_t sp_v1 = jit_insn_add(main, sp, cm1);
            jit_value_t sp_addr = jit_insn_add(main, jit_stack_addr, jit_insn_mul(main, sp_v1, c8));
            jit_value_t ep = jit_insn_load_relative(main, jit_env_ptr, 0, jit_type_uint);
            jit_value_t ep_addr = jit_insn_add(main, jit_memory_addr, jit_insn_mul(main, ep, c8));
            // migrate def pair from stack to memory
            jit_value_t mp = jit_insn_load_relative(main, jit_memory_ptr, 0, jit_type_uint);
            jit_value_t defpair_mpaddr = jit_insn_add(main, jit_memory_addr, jit_insn_mul(main, mp, c8));
            jit_value_t defpair = jit_insn_load_relative(main, sp_addr, 0, jit_type_ulong);
            jit_insn_store_relative(main, defpair_mpaddr, 0, defpair);
            // migrate oldenv to the back of the main memory
            jit_value_t oldenv_mpaddr = jit_insn_add(main, jit_memory_addr, jit_insn_mul(main, jit_insn_add(main, mp, c1), c8));
            jit_value_t env = jit_insn_load_relative(main, ep_addr, 0, jit_type_ulong);
            jit_insn_store_relative(main, oldenv_mpaddr, 0, env);
            // modify mp
            jit_insn_store_relative(main, jit_memory_ptr, 0, jit_insn_add(main, mp, c2));
            // modify current env
            jit_value_t right = jit_insn_shl(main, jit_insn_convert(main, jit_insn_add(main, mp, c1), jit_type_ulong, 0),
                                                    jit_value_create_nint_constant(main, jit_type_uint, 30));
            env = jit_insn_or(main, mp, right);
            env = jit_insn_or(main, env, jit_value_create_long_constant(main, jit_type_ulong, 0x1000000000000000ull));
            jit_insn_store_relative(main, ep_addr, 0, env);
            // load left cell (a string likely) and store it on the stack instead of the defpair 
            jit_value_t left_idx = jit_insn_and(main, defpair, jit_value_create_nint_constant(main, jit_type_uint, 0x000000003FFFFFFFull));
            jit_value_t left_addr = jit_insn_add(main, jit_memory_addr, jit_insn_mul(main, left_idx, c8));
            jit_insn_store_relative(main, sp_addr, 0, jit_insn_load_relative(main, left_addr, 0, jit_type_ulong));
        }
        else if (op == "EQSI")
        {
            Cell cell = Cell::make_string(tokens[1]);
            // load a value from the top of the stack
            jit_value_t sp = jit_insn_load_relative(main, jit_stack_ptr, 0, jit_type_uint);
            jit_value_t sp_v1 = jit_insn_add(main, sp, cm1);
            jit_value_t v1_addr = jit_insn_add(main, jit_stack_addr, jit_insn_mul(main, sp_v1, c8));
            jit_value_t v1 = jit_insn_load_relative(main, v1_addr, 0, jit_type_ulong);
            // check cells are equal
            jit_value_t v1eq = jit_insn_eq(main, v1, jit_value_create_long_constant(main, jit_type_ulong, cell.as64));
            // set type to Int
            jit_value_t v1eqt = jit_insn_or(main, v1eq, jit_value_create_long_constant(main, jit_type_ulong, 0x2000000000000000ull));
            jit_value_t result_addr = jit_insn_add(main, jit_stack_addr, jit_insn_mul(main, sp, c8));
            // store eq result
            jit_insn_store_relative(main, result_addr, 0, v1eqt);
            // modify sp
            jit_insn_store_relative(main, jit_stack_ptr, 0, jit_insn_add(main, sp, c1));
        }
        else if (op == "PUSHCAR" || op == "PUSHCDR" || op == "CAR" || op == "CDR")
        {
            bool car = (op == "PUSHCAR" || op == "CAR") ? true : false;
            bool remove_from_stack = (op == "CAR" || op == "CDR") ? true : false;
            jit_value_t sp = jit_insn_load_relative(main, jit_stack_ptr, 0, jit_type_uint);
            jit_value_t sp1 = jit_insn_add(main, sp, cm1);
            jit_value_t pair_addr = jit_insn_add(main, jit_stack_addr, jit_insn_mul(main, sp1, c8));
            // load pair from stack
            jit_value_t pair = jit_insn_load_relative(main, pair_addr, 0, jit_type_ulong);
            // read 'left' part of a cell
            jit_value_t mask;
            if (car) mask = jit_value_create_long_constant(main, jit_type_ulong, 0x000000003FFFFFFFull);
            else mask = jit_value_create_long_constant(main, jit_type_ulong, 0x0FFFFFFFC0000000ull);
            jit_value_t cell_addr = jit_insn_and(main, pair, mask);
            if (!car) cell_addr = jit_insn_shr(main, cell_addr, jit_value_create_nint_constant(main, jit_type_uint, 30));
            // load left cell from memory
            jit_value_t result_addr = jit_insn_convert(main, 
                                                        jit_insn_add(main, 
                                                                        jit_memory_addr, 
                                                                        jit_insn_mul(main, cell_addr, c8)), jit_type_uint, 0);
            // store it on the stack
            if (remove_from_stack)
                jit_insn_store_relative(main, jit_insn_add(main, jit_stack_addr, jit_insn_mul(main, sp1, c8)), 0, 
                                          jit_insn_load_relative(main, result_addr, 0, jit_type_ulong));
            else
                jit_insn_store_relative(main, jit_insn_add(main, jit_stack_addr, jit_insn_mul(main, sp, c8)), 0, 
                                          jit_insn_load_relative(main, result_addr, 0, jit_type_ulong));
            // modify sp
            if (!remove_from_stack) jit_insn_store_relative(main, jit_stack_ptr, 0, jit_insn_add(main, sp, c1));
        }
        else if (op == "LOADENV")
        {
            jit_value_t sp = jit_insn_load_relative(main, jit_stack_ptr, 0, jit_type_uint);
            jit_value_t sp_addr = jit_insn_add(main, jit_stack_addr, jit_insn_mul(main, sp, c8));
            jit_value_t ep = jit_insn_load_relative(main, jit_env_ptr, 0, jit_type_uint);
            jit_value_t ep_addr = jit_insn_add(main, jit_memory_addr, jit_insn_mul(main, ep, c8));
            jit_value_t env = jit_insn_load_relative(main, ep_addr, 0, jit_type_ulong);
            jit_insn_store_relative(main, sp_addr, 0, env);   
            jit_insn_store_relative(main, jit_stack_ptr, 0, jit_insn_add(main, sp, c1));   
        }
        else if (op == "STOREENV")
        {
            jit_value_t sp = jit_insn_load_relative(main, jit_stack_ptr, 0, jit_type_uint);
            jit_value_t sp1 = jit_insn_add(main, sp, cm1);
            jit_value_t sp_addr = jit_insn_add(main, jit_stack_addr, jit_insn_mul(main, sp1, c8));
            jit_value_t mp = jit_insn_load_relative(main, jit_memory_ptr, 0, jit_type_uint);
            jit_value_t envmp = jit_insn_add(main, jit_memory_addr, jit_insn_mul(main, mp, c8));
            jit_insn_store_relative(main, jit_env_ptr, 0, mp);
            jit_insn_store_relative(main, envmp, 0, jit_insn_load_relative(main, sp_addr, 0, jit_type_ulong));
            jit_insn_store_relative(main, jit_stack_ptr, 0, sp1);
            jit_insn_store_relative(main, jit_memory_ptr, 0, jit_insn_add(main, mp, c1));
        }
        else if (op == "NOP")
        {
        }
        else if (op == "CALL")
        {
            // load IP from stack            
            jit_value_t sp = jit_insn_load_relative(main, jit_stack_ptr, 0, jit_type_uint);
            jit_value_t sp1 = jit_insn_add(main, sp, cm1);
            jit_value_t l_addr = jit_insn_add(main, jit_stack_addr, jit_insn_mul(main, sp1, c8));
            jit_value_t lambda = jit_insn_load_relative(main, l_addr, 0, jit_type_ulong);
            jit_value_t lambda_addr = jit_insn_and(main, lambda, jit_value_create_long_constant(main, jit_type_ulong, 0x00000000FFFFFFFFull));
            jit_value_t lambda_env = jit_insn_and(main, lambda, jit_value_create_long_constant(main, jit_type_ulong, 0x0FFFFFFF00000000ull));
            lambda_env = jit_insn_shr(main, lambda_env, jit_value_create_nint_constant(main, jit_type_uint, 32));
            // push env, setting cell type to Environment
            jit_value_t env = jit_insn_convert(main, 
                                                    jit_insn_load_relative(main, jit_env_ptr, 0, jit_type_uint), 
                                                    jit_type_ulong, 0);
            env = jit_insn_or(main, env, jit_value_create_long_constant(main, jit_type_ulong, 0x6000000000000000ull));
            jit_insn_store_relative(main, l_addr, 0, env);
            // push ip, setting cell type to InstructionPointer
            jit_value_t ip = jit_value_create_long_constant(main, jit_type_ulong, jit_jump_map[pc + 1]);
            ip = jit_insn_or(main, ip, jit_value_create_long_constant(main, jit_type_ulong, 0x5000000000000000ull));
            jit_insn_store_relative(main, jit_insn_add(main, jit_stack_addr, jit_insn_mul(main, sp, c8)), 0, ip);
            // load lambda's env
            jit_insn_store_relative(main, jit_env_ptr, 0, lambda_env);
            // we popped lambda object and pushed env + pc
            jit_insn_store_relative(main, jit_stack_ptr, 0, jit_insn_add(main, sp, c1));
            // branch to 'function'
            jit_insn_jump_table(main, lambda_addr, &jit_jump_table[0], jit_jump_table.size());
        }
        else if (op == "RET")
        {
            // at this point pc and env should be at the top of the stack, otherwise boom
            jit_value_t sp = jit_insn_load_relative(main, jit_stack_ptr, 0, jit_type_uint);
            jit_value_t sp1 = jit_insn_add(main, sp, cm1);
            jit_value_t sp2 = jit_insn_add(main, sp, cm2);
            // load pc, clearing its type
            jit_value_t pc_addr = jit_insn_add(main, jit_stack_addr, jit_insn_mul(main, sp1, c8));
            jit_value_t pc = jit_insn_convert(main, jit_insn_load_relative(main, pc_addr, 0, jit_type_ulong), jit_type_uint, 0);
            pc = jit_insn_and(main, pc, jit_value_create_long_constant(main, jit_type_ulong, 0x0FFFFFFFFFFFFFFFull));
            // load env, clearing its type
            jit_value_t env_addr = jit_insn_add(main, jit_stack_addr, jit_insn_mul(main, sp2, c8));
            jit_value_t env = jit_insn_load_relative(main, env_addr, 0, jit_type_ulong);
            env = jit_insn_and(main, env, jit_value_create_long_constant(main, jit_type_ulong, 0x0FFFFFFFFFFFFFFFull));
            // set env
            jit_insn_store_relative(main, jit_env_ptr, 0, jit_insn_convert(main, env, jit_type_uint, 0));
            // we popped env + pc
            jit_insn_store_relative(main, jit_stack_ptr, 0, sp2);
            // branch back
            jit_insn_jump_table(main, pc, &jit_jump_table[0], jit_jump_table.size());
        }
        else if (op == "RJMP" || op == "RJNZ" || op == "RJZ")
        {
            jit_label_t if_yes = jit_label_undefined, if_no = jit_label_undefined;
            // load value from stack
            jit_value_t sp = jit_insn_load_relative(main, jit_stack_ptr, 0, jit_type_uint);
            jit_value_t sp1 = jit_insn_add(main, sp, cm1);
            jit_value_t val_addr = jit_insn_add(main, jit_stack_addr, jit_insn_mul(main, sp1, c8));
            jit_value_t val = jit_insn_and(main, jit_insn_load_relative(main, val_addr, 0, jit_type_ulong), cdatamask);
            if (op != "RJMP")
            {
                if (op == "RJNZ") jit_insn_branch_if(main, val, &if_yes);
                else if (op == "RJZ") jit_insn_branch_if_not(main, val, &if_yes);
                jit_insn_branch(main, &if_no);
                jit_insn_label(main, &if_yes);
            }
            // jump
            jit_insn_jump_table(main, jit_value_create_nint_constant(main, jit_type_uint, jit_jump_map[pc + std::stoi(tokens[1])]),
                                    &jit_jump_table[0], jit_jump_table.size());            
            if (op != "RJMP")
                jit_insn_label(main, &if_no);
        }
        else panic(op, "JIT: not implemented");
        pc += 1;
    }
#endif
};

void jit_vm_gc(VM* vm) { vm->gc(); }

VM vm;

int main(int argc, char** argv)
{    
    signal(SIGINT, [](int) { vm.debug(); exit(1); });

    std::string line;
    std::vector<std::string> program;
    while (std::getline(std::cin, line))
       program.push_back(line);
#if WITH_JIT
    if (argc > 1 && strcmp(argv[1],"-j") == 0) vm.init_jit();
#endif
    vm.run(program);
    vm.debug();
    return 0;    
}

