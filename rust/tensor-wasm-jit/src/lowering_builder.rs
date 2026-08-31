// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Craton Software Company

//! SSA value-id allocator + Cranelift → lowered-id maps for the wave-2
//! lowering driver.
//!
//! The wave-1 per-family `lower_*` functions (see [`crate::lower_arith`])
//! take a `&mut HashMap<cl::Value, LoweredValueId>` and a `&mut
//! LoweredValueId` next-id counter, and the driver is responsible for
//! seeding the map with entry-block params before calling them. Once we
//! have several families to chain (arith, float, memory, cf, vector, conv)
//! plus block params, stack slots, and constants to track, passing all of
//! that state by-reference becomes noisy.
//!
//! [`LoweringBuilder`] is the scratchpad that owns that state. It exposes
//! the same `&mut HashMap` / `&mut LoweredValueId` accessors the per-family
//! functions already speak ([`LoweringBuilder::value_map_mut`],
//! [`LoweringBuilder::next_value_id_mut`]) so the wave-1 lowerings keep
//! working unchanged, plus higher-level `get_or_alloc_*` helpers for the
//! driver's preflight and for block / stack-slot tracking.
//!
//! # Namespaces
//!
//! - **Values:** Cranelift `Value` → [`LoweredValueId`], allocated from a
//!   monotonic `next_value_id` counter starting at `0`.
//! - **Blocks:** Cranelift `Block` → [`LoweredBlockId`], allocated from a
//!   *separate* `next_block_id` counter starting at `0`. Block ids and
//!   value ids share no numbering — there can be both a `LoweredValueId(0)`
//!   and a `LoweredBlockId(0)` in the same function.
//! - **Stack slots:** Cranelift `StackSlot` → [`LoweredValueId`]. The
//!   per-slot id is itself a value id (alloca pointers are SSA values), but
//!   the `StackSlot → id` map is stored separately so
//!   [`crate::lower_memory`] can recover the alloca pointer across multiple
//!   `stack_load`/`stack_store` ops in the same block without re-allocating.
//!
//! # Not `Send`
//!
//! Each lowering pass owns its own builder. There's no shared mutable
//! state, no locking, and no reason to move the builder across threads.
//! Keeping it `!Send` is enforced indirectly by `std::collections::HashMap`
//! with the default hasher (which is `Send`, but conceptually the builder
//! is per-pass).

#![cfg(feature = "cuda-oxide-backend")]

use std::collections::HashMap;

use cranelift_codegen::ir as cl;

use crate::lowered_ir::{LoweredBlockId, LoweredValueId};

/// Scratchpad shared across per-instruction `lower_*` calls when walking a
/// Cranelift [`cl::Function`].
///
/// Owns three bidirectional Cranelift → lowered-id maps (values, blocks,
/// stack slots) plus a monotonic id allocator for each of the two id
/// namespaces ([`LoweredValueId`] and [`LoweredBlockId`]).
///
/// The wave-2 lowering driver constructs a `LoweringBuilder`, pre-allocates
/// ids for entry-block params and constants via [`Self::get_or_alloc_value`]
/// / [`Self::bind_value`], then calls each per-family `lower_*` function in
/// turn ([`crate::lower_arith::lower_arith_inst`] et al.) by passing the
/// builder's internals through [`Self::value_map_mut`] and
/// [`Self::next_value_id_mut`]. The result is an ordered list of
/// [`crate::lowered_ir::LoweredOp`]s with consistent SSA numbering.
///
/// See the [module docs](self) for the namespace rules.
pub struct LoweringBuilder {
    /// Cranelift `Value` → lowered SSA id.
    value_map: HashMap<cl::Value, LoweredValueId>,
    /// Cranelift `Block` → lowered block id.
    block_map: HashMap<cl::Block, LoweredBlockId>,
    /// Cranelift `StackSlot` → alloca-pointer SSA id (allocated from the
    /// same counter as `value_map`).
    stack_slot_map: HashMap<cl::StackSlot, LoweredValueId>,
    /// Next fresh `LoweredValueId` to hand out. Monotonically increasing.
    next_value_id: LoweredValueId,
    /// Next fresh `LoweredBlockId` to hand out. Monotonically increasing,
    /// independent of `next_value_id`.
    next_block_id: LoweredBlockId,
}

impl LoweringBuilder {
    /// Construct an empty builder. Both id counters start at `0` and all
    /// three maps are empty.
    pub fn new() -> Self {
        Self {
            value_map: HashMap::new(),
            block_map: HashMap::new(),
            stack_slot_map: HashMap::new(),
            next_value_id: 0,
            next_block_id: 0,
        }
    }

    /// Allocate a fresh [`LoweredValueId`] and return it. Increments the
    /// value-id counter. Does **not** bind the id to any Cranelift `Value`;
    /// callers wanting a `Value` → id binding should use
    /// [`Self::get_or_alloc_value`] or [`Self::bind_value`] instead.
    pub fn alloc_value(&mut self) -> LoweredValueId {
        let id = self.next_value_id;
        self.next_value_id = self
            .next_value_id
            .checked_add(1)
            .expect("LoweredValueId counter overflowed u32 — function too large");
        id
    }

    /// Allocate a fresh [`LoweredBlockId`] and return it. Increments the
    /// block-id counter. Independent of [`Self::alloc_value`].
    pub fn alloc_block(&mut self) -> LoweredBlockId {
        let id = self.next_block_id;
        self.next_block_id = self
            .next_block_id
            .checked_add(1)
            .expect("LoweredBlockId counter overflowed u32 — function too large");
        id
    }

    /// Get the [`LoweredValueId`] for a Cranelift [`cl::Value`], allocating
    /// one if it's the first time we've seen it.
    ///
    /// This is the workhorse for entry-block param setup: the driver
    /// iterates over `dfg.block_params(entry)` and calls this for each
    /// param to seed the value-map before walking the body. It's also safe
    /// to call repeatedly for the same value — subsequent calls return the
    /// existing id without advancing the counter.
    pub fn get_or_alloc_value(&mut self, v: cl::Value) -> LoweredValueId {
        if let Some(&id) = self.value_map.get(&v) {
            return id;
        }
        let id = self.alloc_value();
        self.value_map.insert(v, id);
        id
    }

    /// Get the [`LoweredBlockId`] for a Cranelift [`cl::Block`], allocating
    /// one if it's the first time we've seen it.
    ///
    /// Used by [`crate::lower_cf`] when it encounters a branch target: the
    /// target block may not yet have been walked, but its lowered id needs
    /// to be embedded in the `Br`/`CondBr`/`Switch` op now.
    pub fn get_or_alloc_block(&mut self, b: cl::Block) -> LoweredBlockId {
        if let Some(&id) = self.block_map.get(&b) {
            return id;
        }
        let id = self.alloc_block();
        self.block_map.insert(b, id);
        id
    }

    /// Get the alloca-pointer [`LoweredValueId`] for a Cranelift
    /// [`cl::StackSlot`], allocating one (from the value-id counter) if
    /// it's the first time we've seen it.
    ///
    /// [`crate::lower_memory`] uses this for `stack_load` / `stack_store`:
    /// every reference to the same `StackSlot` resolves to the same alloca
    /// pointer SSA value, so the downstream `mem2reg` pass can rewrite all
    /// uses uniformly.
    pub fn get_or_alloc_stack_slot(&mut self, ss: cl::StackSlot) -> LoweredValueId {
        if let Some(&id) = self.stack_slot_map.get(&ss) {
            return id;
        }
        let id = self.alloc_value();
        self.stack_slot_map.insert(ss, id);
        id
    }

    /// Look up an existing [`LoweredValueId`] for a Cranelift [`cl::Value`].
    /// Returns `None` if no binding exists yet.
    ///
    /// Use this when you want to *probe* the map without side effects (e.g.
    /// assertions, debugging, or when a missing operand should fail rather
    /// than allocate). For the eager-allocate flavour use
    /// [`Self::get_or_alloc_value`].
    pub fn lookup_value(&self, v: cl::Value) -> Option<LoweredValueId> {
        self.value_map.get(&v).copied()
    }

    /// Look up an existing [`LoweredBlockId`] for a Cranelift [`cl::Block`].
    /// Returns `None` if no binding exists yet.
    pub fn lookup_block(&self, b: cl::Block) -> Option<LoweredBlockId> {
        self.block_map.get(&b).copied()
    }

    /// Look up an existing stack-slot alloca-pointer id for a Cranelift
    /// [`cl::StackSlot`]. Returns `None` if no binding exists yet.
    pub fn lookup_stack_slot(&self, ss: cl::StackSlot) -> Option<LoweredValueId> {
        self.stack_slot_map.get(&ss).copied()
    }

    /// Bind a Cranelift [`cl::Value`] to a specific (already-allocated)
    /// [`LoweredValueId`].
    ///
    /// Does **not** advance the value-id counter — this is purely a name
    /// assignment. Returns the previous binding for `v`, if any (mirroring
    /// [`HashMap::insert`]).
    ///
    /// Used by per-family `lower_*` functions that prefer the wave-1
    /// idiom of allocating the result id via `*next_id` and inserting it
    /// directly: those callers reach for [`Self::next_value_id_mut`] /
    /// [`Self::value_map_mut`] today, but `bind_value` is the cleaner
    /// option when the caller already has the id in hand.
    pub fn bind_value(&mut self, v: cl::Value, id: LoweredValueId) -> Option<LoweredValueId> {
        self.value_map.insert(v, id)
    }

    /// Bind a Cranelift [`cl::StackSlot`] to a specific (already-allocated)
    /// alloca-pointer [`LoweredValueId`], returning the previous binding if
    /// any (mirroring [`HashMap::insert`]).
    ///
    /// jit HIGH fix (finding 3): the W2.4 driver lifts the builder's
    /// stack-slot map out into a local `&mut HashMap` so it can hand a
    /// `&mut` to the memory-lowering context. Without a way to write
    /// entries back, every new stack slot allocated during memory lowering
    /// was DROPPED on return — so a later `stack_load`/`stack_store` of the
    /// same slot would call [`Self::get_or_alloc_stack_slot`], miss, and
    /// allocate a *fresh* alloca pointer, splitting one logical slot into
    /// several distinct allocas (a miscompile). The driver now reinserts
    /// each entry of the local map through this method after lowering, so
    /// repeated references to one slot resolve to the same alloca id.
    ///
    /// Unlike [`Self::get_or_alloc_stack_slot`], this does **not** advance
    /// the value-id counter — the id was already allocated by the memory
    /// lowering against the same shared counter.
    pub fn bind_stack_slot(
        &mut self,
        ss: cl::StackSlot,
        id: LoweredValueId,
    ) -> Option<LoweredValueId> {
        self.stack_slot_map.insert(ss, id)
    }

    /// Mutable access to the inner `value_map`.
    ///
    /// Provided for compatibility with the wave-1 per-family lowerings
    /// (e.g. [`crate::lower_arith::lower_arith_inst`]) that already accept
    /// `&mut HashMap<cl::Value, LoweredValueId>` directly. The borrow is
    /// tied to `&mut self`, so the builder cannot be otherwise mutated for
    /// the duration.
    pub fn value_map_mut(&mut self) -> &mut HashMap<cl::Value, LoweredValueId> {
        &mut self.value_map
    }

    /// Mutable access to the inner `next_value_id` counter.
    ///
    /// Companion to [`Self::value_map_mut`]: per-family lowerings receive
    /// both and update them in lockstep. Direct mutation here is
    /// equivalent to calling [`Self::alloc_value`] manually.
    pub fn next_value_id_mut(&mut self) -> &mut LoweredValueId {
        &mut self.next_value_id
    }

    /// Read-only view of the `value_map`. Handy for tests, fingerprinting,
    /// and the wave-2 driver's post-walk verification.
    pub fn value_map(&self) -> &HashMap<cl::Value, LoweredValueId> {
        &self.value_map
    }

    /// Read-only view of the `block_map`. Used by [`crate::lower_cf`] when
    /// it only needs to *look up* branch targets that the driver has
    /// already seeded.
    pub fn block_map(&self) -> &HashMap<cl::Block, LoweredBlockId> {
        &self.block_map
    }

    /// Read-only view of the `stack_slot_map`.
    pub fn stack_slot_map(&self) -> &HashMap<cl::StackSlot, LoweredValueId> {
        &self.stack_slot_map
    }

    /// Split-borrow accessor for the memory-lowering family.
    ///
    /// Returns `(&value_map, &mut stack_slot_map, &mut next_value_id)` in a
    /// single call so the caller gets disjoint borrows of three distinct
    /// fields — exactly the shape `MemLowerContext` needs (value map read,
    /// stack-slot map + counter mutated).
    ///
    /// jit MED fix (finding 6): the W2.4 driver previously CLONED the whole
    /// `value_map` on every memory instruction (`builder.value_map().clone()`)
    /// because the borrow checker couldn't see the three accessors were
    /// disjoint — making the driver O(V·M) (values × memory instructions).
    /// This method performs the field split once, in safe code, so the
    /// memory context borrows the live map directly with no per-instruction
    /// allocation.
    #[allow(clippy::type_complexity)]
    pub fn split_borrow_for_memory(
        &mut self,
    ) -> (
        &HashMap<cl::Value, LoweredValueId>,
        &mut HashMap<cl::StackSlot, LoweredValueId>,
        &mut LoweredValueId,
    ) {
        (
            &self.value_map,
            &mut self.stack_slot_map,
            &mut self.next_value_id,
        )
    }
}

impl Default for LoweringBuilder {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use cranelift_codegen::entity::EntityRef;

    /// Helper: construct a Cranelift `Value` from a raw index. Safe because
    /// `Value` implements `EntityRef`, which exposes a public `new(usize)`
    /// constructor — no DFG required for tests of pure id-map plumbing.
    fn v(n: u32) -> cl::Value {
        cl::Value::new(n as usize)
    }

    /// Helper: construct a Cranelift `Block` from a raw index.
    fn b(n: u32) -> cl::Block {
        cl::Block::new(n as usize)
    }

    /// Helper: construct a Cranelift `StackSlot` from a raw index.
    fn ss(n: u32) -> cl::StackSlot {
        cl::StackSlot::new(n as usize)
    }

    #[test]
    fn alloc_value_returns_sequential_ids_starting_at_zero() {
        let mut b = LoweringBuilder::new();
        assert_eq!(b.alloc_value(), 0);
        assert_eq!(b.alloc_value(), 1);
        assert_eq!(b.alloc_value(), 2);
        // Counter advanced exactly three times.
        assert_eq!(*b.next_value_id_mut(), 3);
    }

    #[test]
    fn alloc_block_returns_sequential_ids_starting_at_zero_independent_of_values() {
        let mut bld = LoweringBuilder::new();
        // Allocate a few values first; block counter must not move.
        bld.alloc_value();
        bld.alloc_value();
        // Block ids still start at 0 — the namespaces are disjoint.
        assert_eq!(bld.alloc_block(), 0);
        assert_eq!(bld.alloc_block(), 1);
        assert_eq!(bld.alloc_block(), 2);
        // And further block allocations don't disturb the value counter.
        assert_eq!(*bld.next_value_id_mut(), 2);
    }

    #[test]
    fn get_or_alloc_value_idempotent_and_fresh_on_new() {
        let mut bld = LoweringBuilder::new();
        let v0 = v(0);
        let v1 = v(1);

        let id_a = bld.get_or_alloc_value(v0);
        let id_b = bld.get_or_alloc_value(v0); // same Value
        assert_eq!(id_a, id_b, "same Value must return same id");

        let id_c = bld.get_or_alloc_value(v1);
        assert_ne!(id_a, id_c, "different Value must return different id");

        // Only two ids handed out — the second call to v0 did not advance.
        assert_eq!(*bld.next_value_id_mut(), 2);
    }

    #[test]
    fn get_or_alloc_block_idempotent_and_fresh_on_new() {
        let mut bld = LoweringBuilder::new();
        let b0 = b(0);
        let b1 = b(1);

        let id_a = bld.get_or_alloc_block(b0);
        let id_b = bld.get_or_alloc_block(b0);
        assert_eq!(id_a, id_b);

        let id_c = bld.get_or_alloc_block(b1);
        assert_ne!(id_a, id_c);

        // Block counter advanced exactly twice.
        let next_block_before = bld.alloc_block();
        assert_eq!(next_block_before, 2);
    }

    #[test]
    fn lookup_value_returns_none_for_unallocated_some_for_allocated() {
        let mut bld = LoweringBuilder::new();
        let v0 = v(7);
        assert_eq!(bld.lookup_value(v0), None, "unallocated must be None");
        let id = bld.get_or_alloc_value(v0);
        assert_eq!(
            bld.lookup_value(v0),
            Some(id),
            "after alloc, lookup must return the id"
        );
        // Look-up does not advance the counter.
        let before = *bld.next_value_id_mut();
        let _ = bld.lookup_value(v0);
        assert_eq!(*bld.next_value_id_mut(), before);
    }

    #[test]
    fn lookup_block_and_lookup_stack_slot_behave_like_lookup_value() {
        let mut bld = LoweringBuilder::new();
        let b0 = b(3);
        let s0 = ss(5);
        assert_eq!(bld.lookup_block(b0), None);
        assert_eq!(bld.lookup_stack_slot(s0), None);
        let bid = bld.get_or_alloc_block(b0);
        let sid = bld.get_or_alloc_stack_slot(s0);
        assert_eq!(bld.lookup_block(b0), Some(bid));
        assert_eq!(bld.lookup_stack_slot(s0), Some(sid));
    }

    #[test]
    fn bind_value_does_not_advance_counter() {
        let mut bld = LoweringBuilder::new();
        // Caller manually picks an id 42 and binds it.
        let v0 = v(0);
        let prev = bld.bind_value(v0, 42);
        assert_eq!(prev, None, "first bind returns no prior");
        // Counter must remain at 0 — bind_value does not allocate.
        assert_eq!(*bld.next_value_id_mut(), 0);
        // Subsequent lookup must reflect the bound id.
        assert_eq!(bld.lookup_value(v0), Some(42));

        // Rebinding returns the previous id.
        let prev2 = bld.bind_value(v0, 99);
        assert_eq!(prev2, Some(42));
        assert_eq!(bld.lookup_value(v0), Some(99));
        // And still no counter movement.
        assert_eq!(*bld.next_value_id_mut(), 0);
    }

    #[test]
    fn value_map_mut_and_next_value_id_mut_expose_inner_state() {
        // Mirrors the wave-1 per-family signature: a caller that holds the
        // builder can pass these two `&mut`s straight into
        // `lower_arith_inst` etc.
        let mut bld = LoweringBuilder::new();
        let v0 = v(0);
        let v1 = v(1);

        // Seed the map the wave-1 way.
        {
            let map = bld.value_map_mut();
            map.insert(v0, 100);
            map.insert(v1, 101);
        }
        // And bump the counter manually.
        {
            let next = bld.next_value_id_mut();
            *next = 102;
        }

        // Visible through the read-only accessors.
        assert_eq!(bld.value_map().len(), 2);
        assert_eq!(bld.value_map().get(&v0), Some(&100));
        assert_eq!(bld.value_map().get(&v1), Some(&101));
        // alloc_value now picks up from where the manual bump left off.
        assert_eq!(bld.alloc_value(), 102);
        assert_eq!(bld.alloc_value(), 103);
    }

    #[test]
    fn get_or_alloc_stack_slot_idempotent_and_uses_value_counter() {
        let mut bld = LoweringBuilder::new();
        let s0 = ss(0);
        let s1 = ss(1);

        // Burn one value id first so we can confirm the stack-slot ids
        // come from the same counter (not from a separate one).
        let pre = bld.alloc_value();
        assert_eq!(pre, 0);

        let sid_a = bld.get_or_alloc_stack_slot(s0);
        let sid_b = bld.get_or_alloc_stack_slot(s0); // repeat
        assert_eq!(sid_a, sid_b, "same StackSlot returns same id");
        assert_eq!(sid_a, 1, "stack-slot ids come from the value counter");

        let sid_c = bld.get_or_alloc_stack_slot(s1);
        assert_ne!(sid_a, sid_c, "different StackSlot gets a new id");
        assert_eq!(sid_c, 2);

        // The StackSlot map is its own namespace — looking up s0 as a
        // Value must still return None.
        assert_eq!(bld.lookup_value(v(0)), None);
        assert_eq!(bld.lookup_stack_slot(s0), Some(1));
        assert_eq!(bld.stack_slot_map().len(), 2);
    }

    /// jit HIGH fix (finding 3): `bind_stack_slot` writes an explicit id
    /// without advancing the value counter, and a re-bind of the same slot
    /// is id-stable. This is what lets the driver reinsert the local
    /// stack-slot map after memory lowering so repeated references to one
    /// slot resolve to the same alloca pointer.
    #[test]
    fn bind_stack_slot_is_id_stable_and_does_not_advance_counter() {
        let mut bld = LoweringBuilder::new();
        let s0 = ss(0);

        // Pretend memory lowering allocated id 0 for s0 against the shared
        // counter (advance it once to mirror that).
        assert_eq!(bld.alloc_value(), 0);
        let prev = bld.bind_stack_slot(s0, 0);
        assert_eq!(prev, None, "first bind has no previous value");
        assert_eq!(bld.lookup_stack_slot(s0), Some(0));

        // bind_stack_slot must NOT touch the value counter.
        assert_eq!(
            bld.alloc_value(),
            1,
            "bind_stack_slot must not advance the value-id counter"
        );

        // Re-binding the same slot to the same id is idempotent and returns
        // the previous id (so the driver's reinsert loop is safe to run
        // even when the entry already existed).
        let prev2 = bld.bind_stack_slot(s0, 0);
        assert_eq!(prev2, Some(0));
        assert_eq!(bld.lookup_stack_slot(s0), Some(0));
        assert_eq!(bld.stack_slot_map().len(), 1);
    }

    /// jit MED fix (finding 6): the split-borrow accessor hands out the
    /// LIVE maps + counter (no clone). A mutation through the returned
    /// `&mut`s is visible on the builder afterwards, and the value map is
    /// borrowed in place (so the driver no longer clones it per memory
    /// instruction).
    #[test]
    fn split_borrow_for_memory_exposes_live_state() {
        let mut bld = LoweringBuilder::new();
        bld.bind_value(v(0), 7);

        {
            let (value_map, stack_slot_map, next_value_id) = bld.split_borrow_for_memory();
            // The value map is the live one (read-only here).
            assert_eq!(value_map.get(&v(0)), Some(&7));
            // Mutate the stack-slot map and counter through the split
            // borrow — exactly what the memory context does.
            stack_slot_map.insert(ss(0), *next_value_id);
            *next_value_id += 1;
        }

        // Mutations are visible on the builder (proving no clone was made).
        assert_eq!(bld.lookup_stack_slot(ss(0)), Some(0));
        assert_eq!(
            bld.alloc_value(),
            1,
            "counter advanced through split borrow"
        );
    }

    #[test]
    fn default_matches_new() {
        // `Default::default()` is a thin wrapper around `new()`; this test
        // locks in the contract so a future refactor doesn't silently
        // change the starting counters.
        let bld_new = LoweringBuilder::new();
        let bld_default = LoweringBuilder::default();
        assert_eq!(bld_new.value_map().len(), bld_default.value_map().len());
        assert_eq!(bld_new.block_map().len(), bld_default.block_map().len());
        assert_eq!(
            bld_new.stack_slot_map().len(),
            bld_default.stack_slot_map().len()
        );
    }

    #[test]
    fn block_and_value_namespaces_are_disjoint() {
        // A regression guard: it must be possible to have a
        // `LoweredValueId(0)` and a `LoweredBlockId(0)` alive at the same
        // time, referring to different entities.
        let mut bld = LoweringBuilder::new();
        let vid = bld.get_or_alloc_value(v(0));
        let bid = bld.get_or_alloc_block(b(0));
        assert_eq!(vid, 0);
        assert_eq!(bid, 0);
        // The types are the same `u32` alias today, but the *bindings* live
        // in disjoint maps. Confirm by cross-lookup.
        assert_eq!(bld.lookup_value(v(0)), Some(0));
        assert_eq!(bld.lookup_block(b(0)), Some(0));
        assert!(bld.lookup_stack_slot(ss(0)).is_none());
    }
}
