    
# Migratable Velocity Virtual Machine
[![MacOS](https://github.com/Multi-V-VM/MVVM/actions/workflows/build-windows.yml/badge.svg)](https://github.com/Multi-V-VM/MVVM/actions/workflows/build-windows.yml)[![MacOS](https://github.com/Multi-V-VM/MVVM/actions/workflows/build-macos.yml/badge.svg)](https://github.com/Multi-V-VM/MVVM/actions/workflows/build-macos.yml)[![Ubuntu](https://github.com/Multi-V-VM/MVVM/actions/workflows/build-ubuntu.yml/badge.svg)](https://github.com/Multi-V-VM/MVVM/actions/workflows/build-ubuntu.yml)

![](https://avatars.githubusercontent.com/u/102379947?s=400&u=97b77214800bf74430760eaacddccfe6499033c0&v=4)

## To checkpoint and migrate a WAMR nano process
```bash
LOGV=1 ./MVVM_checkpoint -t ./test/counter.aot -f poll_oneoff -c 0 -x 10 -a "10" -e OMP_NUM_THREADS=1
LOGV=1 ./MVVM_restore -t ./test/counter.aot # All the wasi env will be restored
```
1. -t Target: The path to the WASM interpreter or AOT executable
2. -f Function: The function to stop and checkpoint
3. -x Function Counter: The WASM function counter to stop and checkpoint
4. -c Counter: The WASM instruction counter to stop and checkpoint(Conflict with -f and -x)
5. -a Arguments: The arguments to the function
6. -e Environment: The environment variables to the function

## Design Doc
1. All the pointer will be stored as offset to the linear memory.
2. Go forward and never go back.
3. Use AOT compiler convention with stable point to achieve cross platform.

![](https://asplos.dev/wordpress/wp-content/uploads/2023/06/mvvm-1024x695.png)