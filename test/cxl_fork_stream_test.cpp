#include "wamr_cxl_stream.h"
#include "wamr_memory_instance.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

[[noreturn]] void childFail(const char *message) {
    std::cerr << "child: " << message << '\n';
    _exit(2);
}

struct ChildResult {
    char success;
    std::uint64_t restore_nanoseconds;
    int memory_node;
};

} // namespace

int main(int argc, char **argv) {
    try {
        std::size_t linear_memory_size = 1024 * 1024;
        std::optional<int> numa_node;
        bool prepopulate = true;
        std::string dax_path;
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--numa-node" && index + 1 < argc)
                numa_node = std::stoi(argv[++index]);
            else if (argument == "--memory-bytes" && index + 1 < argc)
                linear_memory_size = std::stoull(argv[++index]);
            else if (argument == "--no-prefault")
                prepopulate = false;
            else if (!argument.starts_with("--") && dax_path.empty())
                dax_path = argument;
            else
                throw std::invalid_argument("usage: mvvm_cxl_fork_stream_test [DAX_FILE] "
                                            "[--numa-node N] [--memory-bytes BYTES] [--no-prefault]");
        }
        if (!dax_path.empty() && numa_node)
            throw std::invalid_argument("DAX_FILE and --numa-node are mutually exclusive");
        if (linear_memory_size < 256 * 1024 || linear_memory_size > std::numeric_limits<uint32>::max())
            throw std::invalid_argument("linear-memory size must be between 256 KiB and UINT32_MAX");

        const std::size_t payload_capacity = linear_memory_size + 64 * 1024;
        constexpr std::size_t kHeapOffset = 64 * 1024;
        constexpr std::size_t kHeapSize = 128 * 1024;
        constexpr std::size_t kMutationOffset = 777;

        std::shared_ptr<mvvm::cxl::SharedRegion> region;
        if (!dax_path.empty()) {
            region = mvvm::cxl::SharedRegion::createDaxFile(dax_path, payload_capacity);
            std::cout << "backend=daxfs path=" << dax_path << '\n';
        } else if (numa_node) {
            region = mvvm::cxl::SharedRegion::createNumaForkRegion(payload_capacity, *numa_node, prepopulate);
            std::cout << "backend=system-ram-numa node=" << *numa_node << " prefault=" << (prepopulate ? "yes" : "no")
                      << '\n';
        } else {
            region = mvvm::cxl::SharedRegion::createForkTestRegion(payload_capacity);
            std::cout << "backend=memfd (functional MAP_SHARED test; not CXL hardware)\n";
        }
        std::cout << std::flush;

        std::vector<uint8_t> source_bytes(linear_memory_size);
        for (std::size_t index = 0; index < source_bytes.size(); ++index)
            source_bytes[index] = static_cast<uint8_t>((index * 17U + 3U) & 0xffU);
        const uint8_t expected_mutation = source_bytes[kMutationOffset] ^ 0xffU;

        WASMMemoryInstance source{};
        source.module_type = 1;
        source.ref_count = 0;
        source.is_shared_memory = 0;
        source.num_bytes_per_page = 64 * 1024;
        source.cur_page_count =
            static_cast<uint32>((linear_memory_size + source.num_bytes_per_page - 1) / source.num_bytes_per_page);
        source.max_page_count = source.cur_page_count;
        source.memory_data = source_bytes.data();
        source.memory_data_size = static_cast<uint32>(source_bytes.size());
        source.memory_data_end = source.memory_data + source.memory_data_size;
        source.heap_data = source.memory_data + kHeapOffset;
        source.heap_data_end = source.heap_data + kHeapSize;

        WAMRMemoryInstance checkpoint{};
        checkpoint.dump_impl(&source);

        int child_status_pipe[2];
        if (::pipe(child_status_pipe) != 0)
            throw std::runtime_error("pipe failed");

        const pid_t child = ::fork();
        if (child < 0)
            throw std::runtime_error("fork failed");

        if (child == 0) {
            ::close(child_status_pipe[0]);
            mvvm::cxl::ReadStream reader(region);
            if (!reader.waitUntilReady(std::chrono::minutes(5)))
                childFail("timed out waiting for committed checkpoint");
            const auto restore_start = std::chrono::steady_clock::now();
            auto result = struct_pack::deserialize<WAMRMemoryInstance>(reader);
            if (!result)
                childFail("struct_pack failed to deserialize checkpoint");
            WAMRMemoryInstance restored = std::move(result.value());
            if (!region->contains(restored.memory_data.data(), restored.memory_data.size()))
                childFail("linear memory was copied outside the shared mapping");

            WASMMemoryInstance destination{};
            restored.restore_impl(&destination);
            if (destination.memory_data != restored.memory_data.data())
                childFail("restore copied linear memory");
            if (!destination.is_shared_memory || destination.ref_count < 2)
                childFail("external CXL mapping was not pinned against WAMR deallocation");
            if (destination.heap_data != destination.memory_data + kHeapOffset ||
                destination.heap_data_end != destination.heap_data + kHeapSize)
                childFail("restore did not preserve the in-memory heap alias");

            const auto restore_end = std::chrono::steady_clock::now();
            destination.memory_data[kMutationOffset] ^= 0xffU;
            const std::size_t probe_offset = linear_memory_size / 2;
            volatile uint8_t resident_probe = destination.memory_data[probe_offset];
            (void)resident_probe;
            ChildResult child_result{
                1,
                static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(restore_end - restore_start).count()),
                numa_node ? region->pageNumaNode(destination.memory_data + probe_offset) : -1,
            };
            if (::write(child_status_pipe[1], &child_result, sizeof(child_result)) != sizeof(child_result))
                childFail("failed to notify parent");
            _exit(0);
        }

        ::close(child_status_pipe[1]);
        mvvm::cxl::WriteStream writer(region);
        const auto serialize_start = std::chrono::steady_clock::now();
        struct_pack::serialize_to(writer, checkpoint);
        const auto serialize_end = std::chrono::steady_clock::now();
        if (!writer.commit())
            throw std::runtime_error("failed to commit CXL checkpoint");
        const auto commit_end = std::chrono::steady_clock::now();

        mvvm::cxl::ReadStream parent_reader(region);
        if (!parent_reader.waitUntilReady(std::chrono::seconds(1)))
            throw std::runtime_error("parent could not observe committed checkpoint");
        auto parent_result = struct_pack::deserialize<WAMRMemoryInstance>(parent_reader);
        if (!parent_result)
            throw std::runtime_error("parent could not deserialize checkpoint view");
        WAMRMemoryInstance parent_view = std::move(parent_result.value());
        if (!region->contains(parent_view.memory_data.data(), parent_view.memory_data.size()))
            throw std::runtime_error("parent linear-memory view is not in the shared mapping");

        ChildResult child_result{};
        if (::read(child_status_pipe[0], &child_result, sizeof(child_result)) != sizeof(child_result) ||
            child_result.success != 1)
            throw std::runtime_error("child restore did not complete");
        int wait_status = 0;
        if (::waitpid(child, &wait_status, 0) != child || !WIFEXITED(wait_status) || WEXITSTATUS(wait_status) != 0)
            throw std::runtime_error("child restore process failed");
        if (parent_view.memory_data[kMutationOffset] != expected_mutation)
            throw std::runtime_error("child mutation was not visible in the parent's CXL mapping");
        if (numa_node && child_result.memory_node != *numa_node)
            throw std::runtime_error("checkpoint page is not resident on the requested NUMA node");

        const double serialize_seconds = std::chrono::duration<double>(serialize_end - serialize_start).count();
        const double commit_seconds = std::chrono::duration<double>(commit_end - serialize_end).count();
        const double restore_seconds = static_cast<double>(child_result.restore_nanoseconds) / 1e9;
        const double gib = static_cast<double>(linear_memory_size) / static_cast<double>(1ULL << 30);
        std::cout << "PASS: checkpoint bytes=" << region->committedSize()
                  << ", linear memory and heap restored as one shared mapping\n"
                  << "serialize_ms=" << serialize_seconds * 1000.0 << " bandwidth_gib_s=" << gib / serialize_seconds
                  << " commit_ms=" << commit_seconds * 1000.0 << " restore_ms=" << restore_seconds * 1000.0;
        if (numa_node)
            std::cout << " verified_numa_node=" << child_result.memory_node;
        std::cout << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
