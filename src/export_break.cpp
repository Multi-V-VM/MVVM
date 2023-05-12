//
// Created by victoryang00 on 5/11/23.
//
#include <llvm/DebugInfo/DIContext.h>
#include <llvm/DebugInfo/DWARF/DWARFContext.h>
#include <llvm/Object/ObjectFile.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/Signals.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetSelect.h>

using namespace llvm;
using namespace object;

const static std::array<std::string, 10> source_loc{};

cl::opt<std::string> InputFilename(cl::Positional, cl::desc("<input file>"), cl::Required);

int main(int argc, char **argv) {
    cl::ParseCommandLineOptions(argc, argv, "llvm line info dumper\n");

    SMDiagnostic Err;

    std::unique_ptr<MemoryBuffer> Buffer;

    auto BufferOrErr = MemoryBuffer::getFile(InputFilename, false, /*RequiresNullTerminator=*/false, true);
    if (auto Err = BufferOrErr.getError()) {
        errs() << "error: '" << InputFilename << "': " << Err.message() << "\n";
        return 1;
    }
    Buffer = std::move(*BufferOrErr);

    Expected<std::unique_ptr<Binary>> BinaryOrErr = createBinary(Buffer->getMemBufferRef());
    if (!BinaryOrErr) {
        std::string Buf;
        raw_string_ostream OS(Buf);
        logAllUnhandledErrors(BinaryOrErr.takeError(), OS);
        OS.flush();
        errs() << "error: '" << InputFilename << "': " << Buf;
        return 1;
    }

    Binary &Bin = *BinaryOrErr.get();
    if (auto *Obj = dyn_cast<ObjectFile>(&Bin)) {
        std::unique_ptr<DIContext> DICtx = DWARFContext::create(*Obj);

        outs() << "Dump of debug_line section:\n";
        DIDumpOptions DumpOptions; // Create a default DIDumpOptions object
        DICtx->dump(outs(), DumpOptions);
    } else {
        errs() << "error: '" << InputFilename << "' not an object file\n";
        return 1;
    }

    return 0;
}