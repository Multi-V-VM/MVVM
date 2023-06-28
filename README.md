    
# Migratable Velocity Virtual Machine
[![MacOS](https://github.com/Multi-V-VM/MVVM/actions/workflows/build-windows.yml/badge.svg)](https://github.com/Multi-V-VM/MVVM/actions/workflows/build-windows.yml)[![MacOS](https://github.com/Multi-V-VM/MVVM/actions/workflows/build-macos.yml/badge.svg)](https://github.com/Multi-V-VM/MVVM/actions/workflows/build-macos.yml)[![Ubuntu](https://github.com/Multi-V-VM/MVVM/actions/workflows/build-ubuntu.yml/badge.svg)](https://github.com/Multi-V-VM/MVVM/actions/workflows/build-ubuntu.yml)

![](https://avatars.githubusercontent.com/u/102379947?s=400&u=97b77214800bf74430760eaacddccfe6499033c0&v=4)


## Design Doc
1. All the pointer will be stored as offset to the linear memory.
2. Go forward and never go back.
3. Use AOT compiler convention with stable point to achieve cross platform.

![](https://asplos.dev/wordpress/wp-content/uploads/2023/06/mvvm-1024x695.png)