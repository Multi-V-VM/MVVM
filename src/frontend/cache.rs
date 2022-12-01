use std::collections::HashMap;
use crate::frontend::instruction::Instruction;

pub struct CodeCache {
    cache: HashMap<u64, Instruction>,
}

impl CodeCache {
    pub fn new() -> CodeCache {
        CodeCache {
            cache: HashMap::new(),
        }
    }

    pub fn get(&mut self, addr: u64) -> Option<&Instruction> {
        self.cache.get(&addr)
    }
    pub fn set(&mut self, addr: u64, instruction: Instruction) {
        self.cache.insert(addr, instruction);
    }
}
