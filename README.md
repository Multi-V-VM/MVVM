    
# Migratable Velocity Virtual Machine
[![MacOS](https://github.com/Multi-V-VM/MVVM/actions/workflows/build-windows.yml/badge.svg)](https://github.com/Multi-V-VM/MVVM/actions/workflows/build-windows.yml)[![MacOS](https://github.com/Multi-V-VM/MVVM/actions/workflows/build-macos.yml/badge.svg)](https://github.com/Multi-V-VM/MVVM/actions/workflows/build-macos.yml)[![Ubuntu](https://github.com/Multi-V-VM/MVVM/actions/workflows/build-ubuntu.yml/badge.svg)](https://github.com/Multi-V-VM/MVVM/actions/workflows/build-ubuntu.yml)[![Generate page](https://github.com/Multi-V-VM/Multi-V-VM.github.io/actions/workflows/mkdocs.yml/badge.svg)](https://github.com/Multi-V-VM/Multi-V-VM.github.io/actions/workflows/mkdocs.yml)

<img width="526" alt="arch" src="https://github.com/Multi-V-VM/MVVM/assets/40686366/0e600853-c2d2-44dc-83cb-4a6904f63019">

## Just run
```bash
wamrc --opt-level=3 --threshold-bits=16 --disable-aux-stack-check --enable-counter-loop-checkpoint -o bc.aot bench/bc.aot
./MVVM_checkpoint -t bench/bc.aot -a -g20,-n1000
./MVVM_restore -t bench/bc.aot
```
Policy for `wamrc`
1. counter loop checkpoint `--enable-counter-loop-checkpoint`
2. loop threshold for 1 << x per checkpoint
3. loop checkpoint `--enable-loop-checkpoint`, meaning without the counter to checkpoint
3. loop dirty checkpoint `--enable-loop-dirty-checkpoint`, meaning use the dirty bit to checkpoint
4. `--enable-checkpoint`, enable function level checkpoint

## To debug mode checkpoint and migrate a WAMR nano process
First comment out the following in `checkpoint.cpp`
```c++
wamr->get_int3_addr();
wamr->replace_int3_with_nop();
wamr->replace_mfence_with_nop();
```
Then
```bash
python3 ../artifact/common_util.py # return $recv is 193
SPDLOG_LEVEL=debug ./MVVM_checkpoint -t test/tcp_client.aot -f 193 -c 0 -x 10 -a "10" -e OMP_NUM_THREADS=1 -i
SPDLOG_LEVEL=debug ./MVVM_restore -t test/tcp_client.aot # All the wasi env will be restored
```
1. -t Target: The path to the WASM interpreter or AOT executable
2. -i Debug Mode: Switch on for debugging
3. -f Function: The function to stop and checkpoint
4. -x Function Counter: The WASM function counter to stop and checkpoint
5. -c Counter: The WASM instruction counter to stop and checkpoint(Conflict with -f and -x)
6. -a Arguments: The arguments to the function
7. -e Environment: The environment variables to the function
<img width="585" alt="image" src="https://github.com/Multi-V-VM/MVVM/assets/40686366/e10dba2b-51f2-4373-a119-0b53f7622407">

## Design Doc
1. All the pointers will be stored as offset to the linear memory.
2. Go forward and never go back, all the calling into WASI land will row back to the call on recovery.
3. Use AOT compiler convention with a stable point to achieve cross-platform.

## Performance
<img width="506" alt="image" src="https://github.com/Multi-V-VM/MVVM/assets/40686366/ab5fb538-82e7-4a62-9516-d29052670c38">
<img width="506" alt="image" src="https://github.com/Multi-V-VM/MVVM/assets/40686366/1f3dc51d-75ee-44cb-ae89-288157d8f498">

## Usecase
1. Cloud Edge integration
2. Optimistic Computing
3. [FAASM](https://github.com/faasm/examples) Warm start
<img width="407" alt="image" src="https://github.com/Multi-V-VM/MVVM/assets/40686366/aa9a9291-cf2c-4703-ab73-be2934a613fd">
