Simple lisp compiler/vm (with optional jit) in C++
==================================================

Cell types: Nil, Pair, Int, String, Lambda
		    + 2 internal types: InstructionPointer, Environment

64 bit Cell format:
* Any cell type: .... .... ........ ........ ........ ........ ........ ........ ........
	* 4 bits type | 60 bits data
* Nil cell
	* 0000 | 60 bits unused
* Pair    cell
	* 0001 | 30 bit heap address (left) | 30 bit heap address (right)
* Integer cell
	* 0010 | 60 bits integer
* String  cell
	* 0011 | 4 bits unused | 56 bits, 7 characters string
* Lambda  cell 
	* 0100 | 32 bit lambda address (left) | 28 bit heap address (lambdas' bound environment, for closures mainly)
* InstructionPointer and Environment special types are used because CALL and RET instruction save/restore a return address and environment pointer on/from the same stack where the actual data belongs.

### *main.cc*: 
Compiles pseduo-lisp code to bytecode
```
parse_list("(+ 2 (- 3 1))") 
```
returns Cell object which contains a list of other Cell objects, thus representing tree structure of the code. Cell object can be compiled to a bytecode, using predefined cases for supported special forms:
```
+-*/%, less, eq, cons, car, cdr, define, func?, str?, int?, null?, begin, cond, lambda and gc
```
### *vm.cc*: 
Either interprets bytecode directly (no **-j** command argument) or generates x86 native code using libjit (-j command argument).
VM class represent a virtual machine with _stack_, _heap_ and special _'env'_ pointer register. Sizes of both stack and heap are hard-coded in the beginning of *vm.cc*. VM class contains 2 functions to execute the code - step_interpret and step_jit. Both are called from VM::run functionb for each instruction. **VM::step_interpret** function interprets an instruction and returns while step_jit generates a piece of code which upon the end of input should be compiled and executed in **VM::run** function (after all instruction were consumed). VM class implements simple garbage collection, stop-and-collect, mark-and-sweep algorithm which moves/compacts used cells from one half of the heap to another. Only 3 instructions could lead to heap growth - **CONS**, **DEF** and **STOREENV**, thus both step_interpret and step_jit check if heap pointer is approaching the end of current half of the heap and call **VM::gc()** automatically. Alternatively it's possible to run gc manually by calling **(gc)** special form or generating **GC** instruction.

### Usage example: 
./main < edigits.lsp | ./vm -j