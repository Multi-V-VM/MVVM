#include "wamr_cxl_stream.h"
#include "wamr_memory_instance.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

[[noreturn]] void childFail(const char *message) {
    std::cerr << "child: " << message << '\n';
    _exit(2);
}

} // namespace

int main(int argc, char **argv) {
    try {
        constexpr std::size_t kPayloadCapacity = 4 * 1024 * 1024;
        constexpr std::size_t kLinearMemorySize = 1024 * 1024;
        constexpr std::size_t kHeapOffset = 64 * 1024;
        constexpr std::size_t kHeapSize = 128 * 1024;
        constexpr std::size_t kMutationOffset = 777;

        std::shared_ptr<mvvm::cxl::SharedRegion> region;
        if (argc == 2) {
            region = mvvm::cxl::SharedRegion::createDaxFile(argv[1], kPayloadCapacity);
            std::cout << "backend=daxfs path=" << argv[1] << '\n';
        } else {
            region = mvvm::cxl::SharedRegion::createForkTestRegion(kPayloadCapacity);
            std::cout << "backend=memfd (functional MAP_SHARED test; not CXL hardware)\n";
        }

        std::vector<uint8_t> source_bytes(kLinearMemorySize);
        for (std::size_t i = 0; i < source_bytes.size(); ++i)
            source_bytes[i] = static_cast<uint8_t>((i * 17U + 3U) & 0xffU);

        WASMMemoryInstance source{};
        source.module_type = 1;
        source.ref_count = 0;
        source.is_shared_memory = 0;
        source.num_bytes_per_page = 64 * 1024;
        source.cur_page_count = kLinearMemorySize / source.num_bytes_per_page;
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
            if (!reader.waitUntilReady(std::chrono::seconds(5)))
                childFail("timed out waiting for committed checkpoint");
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

            destination.memory_data[kMutationOffset] ^= 0xffU;
            const char success = 1;
            if (::write(child_status_pipe[1], &success, 1) != 1)
                childFail("failed to notify parent");
            _exit(0);
        }

        ::close(child_status_pipe[1]);
        mvvm::cxl::WriteStream writer(region);
        struct_pack::serialize_to(writer, checkpoint);
        if (!writer.commit())
            throw std::runtime_error("failed to commit CXL checkpoint");

        mvvm::cxl::ReadStream parent_reader(region);
        if (!parent_reader.waitUntilReady(std::chrono::seconds(1)))
            throw std::runtime_error("parent could not observe committed checkpoint");
        auto parent_result = struct_pack::deserialize<WAMRMemoryInstance>(parent_reader);
        if (!parent_result)
            throw std::runtime_error("parent could not deserialize checkpoint view");
        WAMRMemoryInstance parent_view = std::move(parent_result.value());
        if (!region->contains(parent_view.memory_data.data(), parent_view.memory_data.size()))
            throw std::runtime_error("parent linear-memory view is not in the shared mapping");
        const uint8_t before = parent_view.memory_data[kMutationOffset];

        char child_success = 0;
        if (::read(child_status_pipe[0], &child_success, 1) != 1 || child_success != 1)
            throw std::runtime_error("child restore did not complete");
        int wait_status = 0;
        if (::waitpid(child, &wait_status, 0) != child || !WIFEXITED(wait_status) || WEXITSTATUS(wait_status) != 0)
            throw std::runtime_error("child restore process failed");
        if (parent_view.memory_data[kMutationOffset] != static_cast<uint8_t>(before ^ 0xffU))
            throw std::runtime_error("child mutation was not visible in the parent's CXL mapping");

        std::cout << "PASS: checkpoint bytes=" << region->committedSize()
                  << ", linear memory and heap restored as one shared mapping\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
