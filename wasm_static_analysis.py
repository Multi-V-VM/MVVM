import os
import sys

def wat_to_inst(wat_file, inst_file):
    with open(wat_file, "r") as f:
        wat = f.readlines()
    wat = [line.strip() for line in wat]
    wat = [line.strip(")") for line in wat]

    def critical_line(line):
        if line.startswith("(func"):
            return True
        elif line.startswith("block") or line.startswith("loop") or line.startswith("end"):
            return True
        elif line.startswith("br") or line.startswith("return"):
            return True
        elif line.startswith("local.tee") or line.startswith("local.set"):
            return True
        elif line.startswith("call") or line.startswith("call_indirect"):
            return True
        return False

    func_name_to_idx = {}
    import_func_cnt = 0
    for line in wat:
        if line.startswith("(import "):
            import_func_cnt += 1
            pos = line.find("(func ")
            assert pos != -1, "Invalid import function"
            func_name = line[pos + 6:].split()[0]
            if func_name in func_name_to_idx:
                assert False, f"Function {func_name} already exists"
            func_name_to_idx[func_name] = len(func_name_to_idx)
        if line.startswith("(func"):
            func_name = line.split()[1]
            if func_name in func_name_to_idx:
                assert False, f"Function {func_name} already exists"
            func_name_to_idx[func_name] = len(func_name_to_idx)

    for k in func_name_to_idx:
        func_name_to_idx[k] = func_name_to_idx[k] - import_func_cnt
        # print(k, func_name_to_idx[k])
    
    def lookup_func_idx(func_name):
        if func_name not in func_name_to_idx:
            assert False, f"Function {func_name} not found"
        return func_name_to_idx[func_name]

    wat = [line for line in wat if critical_line(line)]

    func_idx = 0
    loop_idx = 0
    first_func = True

    with open(inst_file, "w") as f:
        for line in wat:
            if line.startswith("(func"):
                if first_func:
                    first_func = False
                else:
                    f.write(f"end_block\n")
                    f.write(f"end_func\n")
                f.write(f"begin_func {func_idx}\n")
                f.write(f"begin_block\n")
                func_idx += 1
                loop_idx = 0
            elif line.startswith("block"):
                f.write(f"begin_block\n")
            elif line.startswith("loop"):
                f.write(f"begin_loop {loop_idx}\n")
                loop_idx += 1
            elif line.startswith("end"):
                f.write(f"end_block\n")

            elif line.startswith("br_if"):
                depth = int(line.split()[1])
                f.write(f"op_br_if {depth}\n")
            elif line.startswith("br_table"):
                f.write(f"op_br_table\n")
            elif line.startswith("br"):
                depth = int(line.split()[1])
                f.write(f"op_br {depth}\n")
            elif line.startswith("return"):
                f.write(f"op_return\n")

            elif line.startswith("local.tee"):
                local_idx = int(line.split()[1])
                f.write(f"op_tee {local_idx}\n")
            elif line.startswith("local.set"):
                local_idx = int(line.split()[1])
                f.write(f"op_set {local_idx}\n")

            elif line.startswith("call_indirect"):
                f.write(f"op_call_indirect\n")
            elif line.startswith("call"):
                callee_idx = lookup_func_idx(line.split()[1])
                f.write(f"op_call {callee_idx}\n")

            else:
                assert False, "Invalid instruction"
        
        if not first_func:
            f.write(f"end_block\n")
            f.write(f"end_func\n")

class InstStream(object):
    def __init__(self, inst_file):
        self.inst_file = inst_file
        self.insts = []
        self.inst_idx = 0 # next instruction
        self.load_insts()

    def load_insts(self):
        with open(self.inst_file, "r") as f:
            self.insts = f.readlines()
        self.insts = [inst.strip().split(" ") for inst in self.insts]
    
    def expect(self, inst_type):
        if self.inst_idx >= len(self.insts):
            assert False, "Instruction stream is empty"
        if self.insts[self.inst_idx][0] == inst_type:
            return True
        return False

    def next(self):
        inst = self.insts[self.inst_idx]
        self.inst_idx += 1
        # print(inst)
        return inst

    def peek(self):
        return self.insts[self.inst_idx]

    def end(self):
        return self.inst_idx >= len(self.insts)

TOKEN_FUNC_BEGIN = "begin_func"
TOKEN_FUNC_END = "end_func"

TOKEN_LOOP_BEGIN = "begin_loop"
TOKEN_BLOCK_BEGIN = "begin_block"
TOKEN_BLOCK_END = "end_block"

TOKEN_OP_BR = "op_br"
TOKEN_OP_BR_IF = "op_br_if"
TOKEN_OP_BR_TABLE = "op_br_table"
TOKEN_OP_RETURN = "op_return"

TOKEN_OP_CALL = "op_call"
TOKEN_OP_CALL_INDIRECT = "op_call_indirect"
TOKEN_OP_RETURN_CALL = "op_return_call"
TOKEN_OP_RETURN_CALL_INDIRECT = "op_return_call_indirect"

TOKEN_OP_SET = "op_set"
TOKEN_OP_TEE = "op_tee"

token_to_class = {}

class WasmBase(object):
    def __init__(self, inst_stream):
        self.inst_stream = inst_stream
        self.data = {}

    def expect(self, inst_type):
        return self.inst_stream.expect(inst_type)
    def next(self):
        return self.inst_stream.next()
    def peek(self):
        return self.inst_stream.peek()
    def end(self):
        return self.inst_stream.end()

class WasmBranch(WasmBase):
    def __init__(self, inst_stream):
        super(WasmBranch, self).__init__(inst_stream)
        self.parse()
    
    def parse(self):
        if self.expect(TOKEN_OP_BR):
            self.next()
        elif self.expect(TOKEN_OP_BR_IF):
            self.next()
        elif self.expect(TOKEN_OP_BR_TABLE):
            self.next()
        elif self.expect(TOKEN_OP_RETURN):
            self.next()
        else:
            assert False, "Invalid branch instruction"

class WasmCall(WasmBase):
    def __init__(self, inst_stream):
        super(WasmCall, self).__init__(inst_stream)
        self.callee_idx = -1
        self.parse()
    
    def parse(self):
        if self.expect(TOKEN_OP_CALL):
            self.callee_idx = int(self.next()[1])
        elif self.expect(TOKEN_OP_CALL_INDIRECT):
            self.next()
        elif self.expect(TOKEN_OP_RETURN_CALL):
            assert False, "Invalid return call"
        elif self.expect(TOKEN_OP_RETURN_CALL_INDIRECT):
            assert False, "Invalid return call"
        else:
            assert False, "Invalid call instruction"

class WasmSet(WasmBase):
    def __init__(self, inst_stream):
        super(WasmSet, self).__init__(inst_stream)
        self.local_idx = -1
        self.parse()
    
    def parse(self):
        if self.expect(TOKEN_OP_SET):
            self.local_idx = int(self.next()[1])
        elif self.expect(TOKEN_OP_TEE):
            self.local_idx = int(self.next()[1])
        else:
            assert False, "Invalid set instruction"

class WasmLoop(WasmBase):
    def __init__(self, inst_stream):
        super(WasmLoop, self).__init__(inst_stream)
        self.insts = []
        self.loop_idx = -1
        self.parse()
    
    def parse(self):
        assert self.expect(TOKEN_LOOP_BEGIN)
        self.loop_idx = int(self.next()[1])
        
        while not self.end():
            if self.peek()[0] == TOKEN_BLOCK_END:
                break
            inst_type = self.peek()[0]
            if inst_type in token_to_class:
                inst_cls = token_to_class[inst_type]
                inst = inst_cls(self.inst_stream)
                self.insts.append(inst)
            else:
                assert False, "Invalid instruction type"
        
        assert self.expect(TOKEN_BLOCK_END)
        self.next()

class WasmBlock(WasmBase):
    def __init__(self, inst_stream):
        super(WasmBlock, self).__init__(inst_stream)
        self.insts = []
        self.parse()
    
    def parse(self):
        assert self.expect(TOKEN_BLOCK_BEGIN)
        self.next()
        
        while not self.end():
            if self.peek()[0] == TOKEN_BLOCK_END:
                break
            inst_type = self.peek()[0]
            if inst_type in token_to_class:
                inst_cls = token_to_class[inst_type]
                inst = inst_cls(self.inst_stream)
                self.insts.append(inst)
            else:
                assert False, f"Invalid instruction type {inst_type}"
        
        assert self.expect(TOKEN_BLOCK_END)
        self.next()

class WasmFunction(WasmBase):
    def __init__(self, inst_stream):
        super(WasmFunction, self).__init__(inst_stream)
        self.func_idx = -1
        self.block = None
        self.parse()
 
    def parse(self):
        assert self.expect(TOKEN_FUNC_BEGIN)
        self.func_idx = int(self.next()[1])
        
        self.block = WasmBlock(self.inst_stream)

        assert self.expect(TOKEN_FUNC_END)
        self.next()

class WasmProgram(WasmBase):
    def __init__(self, inst_stream):
        super(WasmProgram, self).__init__(inst_stream)
        self.funcs = []
        self.parse()
    
    def parse(self):
        while not self.inst_stream.end():
            func = WasmFunction(self.inst_stream)
            self.funcs.append(func)

def SimpleFuncPass(program: WasmProgram):
    pname = "simple_func"

    simple_func_set = set()

    def empty(base: WasmBase):
        base.data[pname] = True
    def visit_call(call: WasmCall):
        if call.callee_idx in simple_func_set:
            call.data[pname] = True
        call.data[pname] = False
    def visit_loop(loop: WasmLoop):
        loop.data[pname] = False
    def visit_block(block: WasmBlock):
        for inst in block.insts:
            visit(inst)
            if not inst.data[pname]:
                block.data[pname] = False
                return
        block.data[pname] = True
    def visit_func(func: WasmFunction):
        visit(func.block)
        func.data[pname] = func.block.data[pname]

    def visit(base: WasmBase):
        if base.data.get(pname, None) is not None:
            return
        if isinstance(base, WasmLoop):
            visit_loop(base)
        elif isinstance(base, WasmBlock):
            visit_block(base)
        elif isinstance(base, WasmFunction):
            visit_func(base)
        elif isinstance(base, WasmCall):
            visit_call(base)
        else:
            empty(base)
    
    def iteration():
        for func in program.funcs:
            visit(func)
            if func.data[pname]:
                simple_func_set.add(func.func_idx)
    iteration()

    while True:
        previous_cnt = len(simple_func_set)
        iteration()
        if len(simple_func_set) == previous_cnt:
            break
        else:
            print(f"Continue to iterate, new simple function count: {len(simple_func_set) - previous_cnt}")

    simple_func = list(simple_func_set)
    simple_func.sort()
    # print(simple_func)

    return simple_func

def ModifiedLocalsPass(program: WasmProgram):
    pname = "modified_locals"

    cur_func_idx = -1

    results = []

    def empty(base: WasmBase):
        base.data[pname] = set()
    def visit_set(inst_set: WasmSet):
        inst_set.data[pname] = set([inst_set.local_idx])
    def visit_block(block: WasmBlock | WasmLoop):
        modified_locals = set()
        for inst in block.insts:
            visit(inst)
            modified_locals |= inst.data[pname]
        block.data[pname] = modified_locals

        if isinstance(block, WasmLoop):
            results.append((cur_func_idx, block.loop_idx, modified_locals))
    def visit_func(func: WasmFunction):
        nonlocal cur_func_idx
        cur_func_idx = func.func_idx
        visit(func.block)
        func.data[pname] = func.block.data[pname]

    def visit(base: WasmBase):
        if base.data.get(pname, None) is not None:
            return
        elif isinstance(base, WasmBlock) or isinstance(base, WasmLoop):
            visit_block(base)
        elif isinstance(base, WasmFunction):
            visit_func(base)
        elif isinstance(base, WasmSet):
            visit_set(base)
        else:
            empty(base)
    
    for func in program.funcs:
        visit(func)
    
    return results

def LoopCkptPass(program: WasmProgram, simple_func_set: set[int]):
    pname = "loop_ckpt"

    cur_func_idx = -1

    results = []

    def empty(base: WasmBase):
        base.data[pname] = {
            "noinf": True,
            "ckpt": False
        }
    def visit_call(call: WasmCall):
        if call.callee_idx in simple_func_set:
            call.data[pname] = {
                "noinf": True,
                "ckpt": False
            }
        else:
            call.data[pname] = {
                "noinf": False,
                "ckpt": True
            }
    def visit_block(block: WasmBlock):
        noinf = True
        ckpt = False
        for inst in block.insts:
            visit(inst)
            ckpt = ckpt or inst.data[pname]["ckpt"]
            if not inst.data[pname]["noinf"]:
                noinf = False
                break
        block.data[pname] = {
            "noinf": noinf,
            "ckpt": ckpt
        }
    
    def visit_loop(loop: WasmLoop):
        visit_block(loop)
        ignore_emit_ckpt = loop.data[pname]["ckpt"]
        loop.data[pname] = {
            "noinf": False,
            "ckpt": True
        }
        if ignore_emit_ckpt:
            # print(f"Func {cur_func_idx} Loop {loop.loop_idx} ignore emit checkpoint")
            results.append((cur_func_idx, loop.loop_idx))

    def visit_func(func: WasmFunction):
        nonlocal cur_func_idx
        cur_func_idx = func.func_idx
        visit(func.block)

    def visit(base: WasmBase):
        if base.data.get(pname, None) is not None:
            return
        elif isinstance(base, WasmBlock):
            visit_block(base)
        elif isinstance(base, WasmLoop):
            visit_loop(base)
        elif isinstance(base, WasmFunction):
            visit_func(base)
        elif isinstance(base, WasmCall):
            visit_call(base)
        else:
            empty(base)
    
    for func in program.funcs:
        visit(func)
    
    return results

if __name__ == "__main__":
    token_to_class = {
        TOKEN_OP_BR: WasmBranch,
        TOKEN_OP_BR_IF: WasmBranch,
        TOKEN_OP_BR_TABLE: WasmBranch,
        TOKEN_OP_RETURN: WasmBranch,

        TOKEN_OP_CALL: WasmCall,
        TOKEN_OP_CALL_INDIRECT: WasmCall,
        TOKEN_OP_RETURN_CALL: WasmCall,
        TOKEN_OP_RETURN_CALL_INDIRECT: WasmCall,

        TOKEN_OP_SET: WasmSet,
        TOKEN_OP_TEE: WasmSet,

        TOKEN_BLOCK_BEGIN: WasmBlock,
        TOKEN_LOOP_BEGIN: WasmLoop,
    }

    assert len(sys.argv) == 2, "Invalid arguments"

    # wasm_file = "/workspaces/MVVM/build/test/vadd.wasm"
    # wasm_file = "/workspaces/MVVM/build/bench/redis.wasm"
    wasm_file = sys.argv[1]
    assert wasm_file.endswith(".wasm"), "Invalid wasm file"
    assert os.path.exists(wasm_file), "Wasm file not found"

    wat_file = wasm_file.replace(".wasm", ".wat")
    os.system(f"wasm2wat --enable-all '{wasm_file}' -o '{wat_file}'")

    wat_inst_file = wasm_file + ".inst"
    wat_to_inst(wat_file, wat_inst_file)
    inst_stream = InstStream(wat_inst_file)
    program = WasmProgram(inst_stream)

    simple_func = SimpleFuncPass(program)
    modified_locals = ModifiedLocalsPass(program)
    skip_ckpt = LoopCkptPass(program, set(simple_func))

    print(f"Simple Functions: {simple_func}")

    for func_idx, loop_idx, locals in modified_locals:
        print(f"Func {func_idx} Loop {loop_idx} Modified Locals: {locals}")

    for func_idx, loop_idx in skip_ckpt:
        print(f"Func {func_idx} Loop {loop_idx} Skip Checkpoint")
    
    simple_func_opt = wasm_file + ".simple_func.opt"
    with open(simple_func_opt, "w") as f:
        f.write(f"{len(simple_func)}\n")
        for func_idx in simple_func:
            f.write(f"{func_idx}\n")
    
    modified_locals_opt = wasm_file + ".modified_locals.opt"
    with open(modified_locals_opt, "w") as f:
        f.write(f"{len(modified_locals)}\n")
        for func_idx, loop_idx, locals in modified_locals:
            f.write(f"{func_idx} {loop_idx} {len(locals)} {' '.join([str(local_idx) for local_idx in locals])}\n")

    skip_ckpt_opt = wasm_file + ".skip_ckpt.opt"
    with open(skip_ckpt_opt, "w") as f:
        f.write(f"{len(skip_ckpt)}\n")
        for func_idx, loop_idx in skip_ckpt:
            f.write(f"{func_idx} {loop_idx}\n")