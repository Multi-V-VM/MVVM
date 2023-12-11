#include "wamr.h"
#include "wamr_export.h"
#include <mpi.h>
#include <stdio.h>
#include <vector>
std::vector<WAMRInstance *> wamr;
template <typename... Args> std::string sstr(Args &&...args) {
    std::ostringstream sstr;
    // fold expression
    ((sstr << std::dec) << ... << args);
    return sstr.str();
}
void create_wamr_counter(int world_rank) {
    wamr[world_rank] = new WAMRInstance("./test/counter.aot", false);
    auto input = sstr(world_rank);
    wamr[world_rank]->set_wasi_args({""}, {""}, {""}, {input}, {""}, {""});
    wamr[world_rank]->instantiate();
    wamr[world_rank]->invoke_main();
};
void serialize_to_file(WASMExecEnv *instance) {
    auto writer = new FwriteStream("./test/counter.bin");
    struct_pack::serialize(*writer, *instance);
    exit(0);
}
int main(int argc, char **argv) {
    // Initialize the MPI environment
    MPI_Init(&argc, &argv);

    // Get the number of processes
    int world_size;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    wamr.resize(world_size);
    // Get the rank of the process
    int world_rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

    // Get the name of the processor
    char processor_name[MPI_MAX_PROCESSOR_NAME];
    int name_len;
    MPI_Get_processor_name(processor_name, &name_len);

    // Print off a hello world message
    printf("Hello world from processor %s, rank %d out of %d processors\n", processor_name, world_rank, world_size);
    create_wamr_counter(world_rank);

    // Finalize the MPI environment.
    MPI_Finalize();
}