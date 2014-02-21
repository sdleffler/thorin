#ifndef THORIN_BE_LLVM_RURNTIMES_OPENCL_H
#define THORIN_BE_LLVM_RURNTIMES_OPENCL_H

#include "thorin/be/llvm/runtimes/spir_runtime.h"

namespace thorin {

class OpenCLRuntime : public SpirRuntime {
public:
    OpenCLRuntime(llvm::LLVMContext& context, llvm::Module* target, llvm::IRBuilder<>& builder);

    virtual llvm::Value* load_kernel(llvm::Value* module, llvm::Value* data);

protected:
    virtual std::string get_module_name(Lambda*);
};

}

#endif

