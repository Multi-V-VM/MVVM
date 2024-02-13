# MVVM Integration with Faasm and MPI
This design document outlines the process for integrating Faasm with MPI, leveraging the Faasm runtime for execution. The steps include setting up the environment, compiling the MPI code, and running tests to verify the integration.

## Setup and Compilation
1. clone the Faasm repository:

```bash
git clone https://github.com/faasm/examples
git submodule update --init --recursive
git clone https://github.com/Multi-V-VM/faasm dev/faasm-local
```

2. Compile the project and copy the resulting libraries to the Faasm system root:

```bash
source ./bin/workon.sh
inv build
inv lammps
faasmctl deploy.compose --workers=2
export FAASM_INI_FILE=../examples/faasm.ini
faasmctl invoke lammps main --cmdline '-in faasm://lammps-data/in.controller.wall' --mpi-world-size 2
faasmctl logs -s worker -f
```
