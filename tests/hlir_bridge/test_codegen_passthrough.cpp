#include <iostream>
#include "src/libs/code_gen_bridge/CodeGenBridge.hpp"

int main() {
    codegen::CodeGenBridge bridge;
    
    codegen::CodeGenOptions options;
    options.outputDir = ".";
    options.verbose = false;
    
    auto result = bridge.runCodeGen("src/aiecad_compiler/examples/applications/passthrough2/passthrough_gui.xml", options);
    
    if (!result) {
        std::cerr << "Code generation failed:\n";
        for (const auto& diag : result.error()) {
            std::cerr << "  " << diag.message << "\n";
        }
        return 1;
    }
    
    std::cout << "Success! Generated " << result->generatedFiles.size() << " files\n";
    for (const auto& file : result->generatedFiles) {
        std::cout << "  - " << file << "\n";
    }
    
    return 0;
}
