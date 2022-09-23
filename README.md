# Multi V Virtual Machine
MVVM is a double JIT VM from RiscV assembly or elf to wasm, with all linux implemented. Here 'V' can be translated into multiple meanings: RiscV, Variable Level, etc. 

## Comparison of WebAssembly and RISC-V
1. Code/Data Separation

Most modern architectures, including RISC-V, use the same address space for code and data, but WebAssembly does not. In fact, the running code does not even have a way to read/write itself.

Simplify the implementation of the JIT compiler. If the code is self-modifying, then the JIT compiler needs to have the ability to detect changes and regenerate the target code, which requires a fairly complex implementation mechanism.

WebAssembly assumes a fully functional runtime environment. The runtime environment handles the linking, relocation, and other preparations, and the program does not need to care about getting it up and running on its own.

Security. Code that can be dynamically generated and modified is a dangerous point of attack.

2. Static types and control flow constraints

WebAssembly is very "structural". The standard requires that all function calls, loops, jumps and value types follow specific structural constraints, e.g. passing two arguments to a function that takes three, jumping to a position in another function, performing a floating point add operation on two integers, etc. will result in compilation/validation failures; RISC-V has no such constraints, and the validity of instructions depends only on their own coding.

3. Machine Model

WebAssembly is a stack machine instruction set, while RISC-V is a register machine instruction set.

In WebAssembly, each instruction semantically pops its operands off the value stack and then pushes the result onto the value stack. However, unlike other stack-machine based bytecode formats such as Java, the structure of the value stack at any instruction in the program can be statically determined. This design facilitates better compilation optimization.

In RISC-V, each instruction is encoded with 0 - 3 register numbers. Where with is the input register and is the output register. Except for special types of instructions such as memory access and privileged instructions, each instruction only reads data from the input register and stores the result in the output register.

4. Memory Management

Although WebAssembly and RISC-V both define an untyped, byte-addressable memory, there are some detailed differences between them; WebAssembly's memory is equivalent to a large array: the effective address starts at 0 and expands continuously up to some program-defined initial value and can grow. RISC-V, on the other hand, uses virtual memory, using page tables to map addresses to physical memory.


Memory layout

WebAssembly's memory design, while clean and easy to implement, has a number of problems.

Address 0 is valid, which can cause some programs to behave differently than expected when dereferencing null pointers.

It is not possible to create an "invalid" address range that does not map to any physical address, so it is not possible to implement a stack guard page in a multi-threaded environment. 5.

5. Synchronization mechanism

A Turing-complete calculator requires at least one conditional branch instruction. Similarly, an instruction set architecture that supports multi-threaded synchronization requires at least one "atomic conditional branch" instruction. Such instructions are available under WebAssembly and under RISC-V, corresponding to the CAS model and the LL/SC model, respectively.

LL/SC has stronger semantics than CAS, which suffers from intractable ABA issues, but LL/SC does not. This also means that it is much more difficult to simulate LL/SC on a CAS architecture than vice versa.

## Features
- [ ] qemu like API
- [ ] libc wasm wrapper
- [ ] JIT RV64IMACGVF ISA to wasm
- [ ] doubly JIT codebase infrastructure

### WIP ideas
- [ ] 🚧 Lazy loaded memory?
- [ ] 🚧 Lazy loaded csr and fp.