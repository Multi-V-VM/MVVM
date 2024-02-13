    
# Migratable Velocity Virtual Machine
[![MacOS](https://github.com/Multi-V-VM/MVVM/actions/workflows/build-windows.yml/badge.svg)](https://github.com/Multi-V-VM/MVVM/actions/workflows/build-windows.yml)[![MacOS](https://github.com/Multi-V-VM/MVVM/actions/workflows/build-macos.yml/badge.svg)](https://github.com/Multi-V-VM/MVVM/actions/workflows/build-macos.yml)[![Ubuntu](https://github.com/Multi-V-VM/MVVM/actions/workflows/build-ubuntu.yml/badge.svg)](https://github.com/Multi-V-VM/MVVM/actions/workflows/build-ubuntu.yml)

![](https://avatars.githubusercontent.com/u/102379947?s=400&u=97b77214800bf74430760eaacddccfe6499033c0&v=4)

## To checkpoint and migrate a WAMR nano process
```bash
python3 ../artifact/common_util.py # return $recv is 193
SPDLOG_LEVEL=debug ./MVVM_checkpoint -t ./test/tcp_client.aot -f 193 -c 0 -x 10 -a "10" -e OMP_NUM_THREADS=1 -i
SPDLOG_LEVEL=debug ./MVVM_restore -t ./test/tcp_client.aot # All the wasi env will be restored
```
1. -t Target: The path to the WASM interpreter or AOT executable
2. -i Debug Mode: Switch on for debugging
3. -f Function: The function to stop and checkpoint
4. -x Function Counter: The WASM function counter to stop and checkpoint
5. -c Counter: The WASM instruction counter to stop and checkpoint(Conflict with -f and -x)
6. -a Arguments: The arguments to the function
7. -e Environment: The environment variables to the function

## Design Doc
1. All the pointer will be stored as offset to the linear memory.
2. Go forward and never go back.
3. Use AOT compiler convention with stable point to achieve cross platform.

![](https://asplos.dev/wordpress/wp-content/uploads/2023/06/mvvm-1024x695.png)