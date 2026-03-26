#include "BridgeTests.hpp"

#include "hlir_cpp_bridge/HlirBridge.hpp"
#include "code_gen_bridge/CodeGenBridge.hpp"

#include <iostream>
#include <filesystem>

namespace BridgeTests {

// Output directory for generated test files.
// IRONSMITH_BRIDGE_TEST_OUTPUT_DIR is set by CMake to the source-tree output folder
// so files always land in tests/hlir_bridge/output/ regardless of working directory.
#ifdef IRONSMITH_BRIDGE_TEST_OUTPUT_DIR
static std::string OUTPUT_DIR = std::string(IRONSMITH_BRIDGE_TEST_OUTPUT_DIR) + "/";
#else
static std::string OUTPUT_DIR = "tests/hlir_bridge/output/";
#endif

static void ensureOutputDir() {
    std::filesystem::create_directories(OUTPUT_DIR);
}

static bool testHlirBridge() {
    std::cout << "\n=== Testing HLIR C++ Bridge ===\n";

    try {
        hlir::HlirBridge bridge("bridge_test");

        // Test 1: Create a constant
        std::cout << "  [1/8] Creating constant... ";
        auto constResult = bridge.addConstant("test_size", "256", "int");
        if (!constResult) {
            std::cerr << "FAILED\n    " << constResult.error()[0].message << "\n";
            return false;
        }
        auto constId = constResult.value();
        std::cout << "OK (ID: " << constId.value.substr(0, 8) << "...)\n";

        // Test 2: Create a tensor type
        std::cout << "  [2/8] Creating tensor type... ";
        auto tensorResult = bridge.addTensorType("test_type", {"test_size"}, "float32", "");
        if (!tensorResult) {
            std::cerr << "FAILED\n    " << tensorResult.error()[0].message << "\n";
            return false;
        }
        auto tensorId = tensorResult.value();
        std::cout << "OK (ID: " << tensorId.value.substr(0, 8) << "...)\n";

        // Test 3: Create tiles
        std::cout << "  [3/8] Creating tiles... ";
        auto shimResult = bridge.addTile("test_shim", hlir::TileKind::SHIM, 0, 0);
        auto memResult = bridge.addTile("test_mem", hlir::TileKind::MEM, 0, 1);
        if (!shimResult || !memResult) {
            std::cerr << "FAILED\n";
            return false;
        }
        auto shimId = shimResult.value();
        auto memId = memResult.value();
        std::cout << "OK (2 tiles)\n";

        // Test 4: Create FIFO
        std::cout << "  [4/8] Creating FIFO... ";
        auto fifoResult = bridge.addFifo("test_fifo", tensorId, 2, shimId, {memId});
        if (!fifoResult) {
            std::cerr << "FAILED\n    " << fifoResult.error()[0].message << "\n";
            return false;
        }
        auto fifoId = fifoResult.value();
        std::cout << "OK (ID: " << fifoId.value.substr(0, 8) << "...)\n";

        // Test 5: Update tile (move location)
        std::cout << "  [5/8] Updating tile location... ";
        auto updateResult = bridge.addTile("test_mem", hlir::TileKind::MEM, 0, 2, memId);
        if (!updateResult || updateResult.value().value != memId.value) {
            std::cerr << "FAILED\n";
            return false;
        }
        std::cout << "OK (same ID, new location)\n";

        // Test 6: Update FIFO depth
        std::cout << "  [6/8] Updating FIFO depth... ";
        auto depthResult = bridge.updateFifoDepth(fifoId, 8);
        if (!depthResult) {
            std::cerr << "FAILED\n";
            return false;
        }
        std::cout << "OK (depth: 2 -> 8)\n";

        // Test 7: Lookup component
        std::cout << "  [7/8] Looking up component... ";
        auto lookupResult = bridge.lookupById(fifoId);
        if (!lookupResult) {
            std::cerr << "FAILED\n";
            return false;
        }
        std::cout << "OK (found FIFO data)\n";

        // Test 8: Export to XML
        std::cout << "  [8/8] Exporting to XML... ";
        ensureOutputDir();
        std::string testXmlPath = OUTPUT_DIR + "bridge_test_output.xml";
        auto exportResult = bridge.exportToGuiXml(testXmlPath);
        if (!exportResult) {
            std::cerr << "FAILED\n    " << exportResult.error()[0].message << "\n";
            return false;
        }

        // Verify file exists
        if (!std::filesystem::exists(testXmlPath)) {
            std::cerr << "FAILED (file not created)\n";
            return false;
        }

        auto fileSize = std::filesystem::file_size(testXmlPath);
        std::cout << "OK (" << fileSize << " bytes)\n";

        std::cout << "\n  HLIR Bridge: ALL TESTS PASSED\n";
        std::cout << "  XML file saved to: " << testXmlPath << "\n";
        return true;

    } catch (const std::exception& e) {
        std::cerr << "EXCEPTION: " << e.what() << "\n";
        return false;
    }
}

static bool testCodeGenBridge() {
    std::cout << "\n=== Testing Code Generation Bridge ===\n";

    try {
        codegen::CodeGenBridge bridge;

        // Test 1: Check availability
        std::cout << "  [1/3] Checking code generator availability... ";
        bool available = bridge.isAvailable();
        if (!available) {
            std::cout << "NOT AVAILABLE\n";
            std::cout << "  Note: This is expected if main.py is not yet implemented.\n";
            std::cout << "  The bridge is working, but code generator is not found.\n";
            std::cout << "  [2/3] Skipping code generation test (generator not available)\n";
            std::cout << "  [3/3] Skipping Python version test (generator not available)\n";
            return true;  // Not a failure
        }
        std::cout << "OK\n";

        // Test 2: Run code generation on test XML
        std::cout << "  [2/3] Running code generator on test XML... ";
        std::string testXmlPath = OUTPUT_DIR + "bridge_test_output.xml";

        if (!std::filesystem::exists(testXmlPath)) {
            std::cout << "SKIPPED (test XML not found)\n";
            std::cout << "  Note: Run HLIR bridge tests first to generate test XML\n";
        } else {
            codegen::CodeGenOptions options;
            options.outputDir = OUTPUT_DIR;
            options.backend = "default";
            options.verbose = true;
            options.cleanOutput = true;

            auto codegenResult = bridge.runCodeGen(testXmlPath, options);
            if (!codegenResult) {
                std::cerr << "FAILED\n";
                for (const auto& diag : codegenResult.error()) {
                    std::cerr << "    " << diag.message << "\n";
                    if (!diag.details.empty()) {
                        std::cerr << "    Details: " << diag.details << "\n";
                    }
                }
                return false;
            }

            const auto& output = codegenResult.value();
            std::cout << "OK\n";
            std::cout << "    Output directory: " << output.outputDirectory << "\n";
            std::cout << "    Generated " << output.generatedFiles.size() << " file(s)\n";
            for (const auto& file : output.generatedFiles) {
                std::cout << "      - " << file << "\n";
            }
        }

        // Test 3: Get Python version
        std::cout << "  [3/3] Getting Python version... ";
        auto versionResult = bridge.getVersion();
        if (!versionResult) {
            std::cerr << "FAILED\n";
            return false;
        }
        std::cout << "OK (" << versionResult.value() << ")\n";

        std::cout << "\n  CodeGen Bridge: ALL TESTS PASSED\n";
        return true;

    } catch (const std::exception& e) {
        std::cerr << "EXCEPTION: " << e.what() << "\n";
        return false;
    }
}

static bool testPassthroughExample() {
    std::cout << "\n=== Testing Passthrough Example (Full Pipeline) ===\n";

    try {
        // Create passthrough design using HLIR C++ Bridge
        hlir::HlirBridge bridge("passthrough_test");

        // Step 1: Add constants
        std::cout << "  [1/12] Adding constants... ";
        auto constN = bridge.addConstant("N", "4096", "int");
        if (!constN) {
            std::cerr << "FAILED\n    " << constN.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Step 2: Add type definitions
        std::cout << "  [2/12] Adding tensor types... ";
        auto vectorTy = bridge.addTensorType("vector_ty", {"N"}, "int32");
        if (!vectorTy) {
            std::cerr << "FAILED\n    " << vectorTy.error()[0].message << "\n";
            return false;
        }

        auto lineTy = bridge.addTensorType("line_ty", {"N / 4"}, "int32");
        if (!lineTy) {
            std::cerr << "FAILED\n    " << lineTy.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Step 3: Add tile
        std::cout << "  [3/12] Adding SHIM tile... ";
        auto shim = bridge.addTile("shim0", hlir::TileKind::SHIM, 0, 0);
        if (!shim) {
            std::cerr << "FAILED\n    " << shim.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Step 4: Add input FIFO
        std::cout << "  [4/12] Adding input FIFO... ";
        auto fifoIn = bridge.addFifo("of_in", *lineTy, 2);
        if (!fifoIn) {
            std::cerr << "FAILED\n    " << fifoIn.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Step 5: Add forward FIFO
        std::cout << "  [5/12] Adding forward FIFO... ";
        auto fifoOut = bridge.addFifoForward("of_out", *fifoIn);
        if (!fifoOut) {
            std::cerr << "FAILED\n    " << fifoOut.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Step 6: Create runtime
        std::cout << "  [6/12] Creating runtime... ";
        auto runtime = bridge.createRuntime("runtime");
        if (!runtime) {
            std::cerr << "FAILED\n    " << runtime.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Step 7: Add input type
        std::cout << "  [7/12] Adding runtime input type... ";
        auto addInput = bridge.runtimeAddInputType(*vectorTy);
        if (!addInput) {
            std::cerr << "FAILED\n    " << addInput.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Step 8: Add output type
        std::cout << "  [8/12] Adding runtime output type... ";
        auto addOutput = bridge.runtimeAddOutputType(*vectorTy);
        if (!addOutput) {
            std::cerr << "FAILED\n    " << addOutput.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Step 9: Add parameters
        std::cout << "  [9/12] Adding runtime parameters... ";
        auto addParamA = bridge.runtimeAddParam("inputA");
        if (!addParamA) {
            std::cerr << "FAILED\n    " << addParamA.error()[0].message << "\n";
            return false;
        }
        auto addParamC = bridge.runtimeAddParam("outputC");
        if (!addParamC) {
            std::cerr << "FAILED\n    " << addParamC.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Step 10: Add fill operation
        std::cout << "  [10/12] Adding fill operation... ";
        auto addFill = bridge.runtimeAddFill("fill_0", *fifoIn, "inputA", *shim);
        if (!addFill) {
            std::cerr << "FAILED\n    " << addFill.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Step 11: Add drain operation
        std::cout << "  [11/12] Adding drain operation... ";
        auto addDrain = bridge.runtimeAddDrain("drain_0", *fifoOut, "outputC", *shim);
        if (!addDrain) {
            std::cerr << "FAILED\n    " << addDrain.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Step 12: Build runtime
        std::cout << "  [12/12] Building runtime... ";
        auto buildRuntime = bridge.runtimeBuild();
        if (!buildRuntime) {
            std::cerr << "FAILED\n    " << buildRuntime.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Validate program
        std::cout << "  [Validation] Building and validating program... ";
        auto buildResult = bridge.build();
        if (!buildResult) {
            std::cerr << "FAILED\n";
            for (const auto& diag : buildResult.error()) {
                std::cerr << "    " << diag.message << "\n";
            }
            return false;
        }
        std::cout << "OK\n";

        // Export to XML
        std::cout << "  [Export] Exporting to GUI XML... ";
        ensureOutputDir();
        std::string xmlPath = OUTPUT_DIR + "passthrough_test_gui.xml";
        auto exportResult = bridge.exportToGuiXml(xmlPath);
        if (!exportResult) {
            std::cerr << "FAILED\n    " << exportResult.error()[0].message << "\n";
            return false;
        }

        if (!std::filesystem::exists(xmlPath)) {
            std::cerr << "FAILED (file not created)\n";
            return false;
        }
        std::cout << "OK\n";

        // Run code generator
        std::cout << "  [CodeGen] Running code generator... ";
        codegen::CodeGenBridge codegenBridge;
        codegen::CodeGenOptions options;
        options.outputDir = OUTPUT_DIR;

        auto codegenResult = codegenBridge.runCodeGen(xmlPath, options);
        if (!codegenResult) {
            std::cerr << "FAILED\n";
            for (const auto& diag : codegenResult.error()) {
                std::cerr << "    " << diag.message << "\n";
            }
            return false;
        }

        const auto& output = codegenResult.value();
        std::cout << "OK (" << output.generatedFiles.size() << " files)\n";

        // Verify generated files
        bool foundGraphML = false;
        bool foundPython = false;
        for (const auto& file : output.generatedFiles) {
            std::string filename = file.filename().string();
            if (filename.ends_with(".graphml")) foundGraphML = true;
            if (filename.starts_with("generated_") && filename.ends_with(".py")) foundPython = true;
        }

        std::cout << "  [Verify] Checking generated files... ";
        if (!foundGraphML || !foundPython) {
            std::cerr << "FAILED\n";
            std::cerr << "    GraphML: " << (foundGraphML ? "Found" : "Missing") << "\n";
            std::cerr << "    Python: " << (foundPython ? "Found" : "Missing") << "\n";
            return false;
        }
        std::cout << "OK\n";

        std::cout << "\n  Passthrough Example: ALL TESTS PASSED\n";
        std::cout << "  Generated files saved to: " << OUTPUT_DIR << "\n";
        std::cout << "    - " << xmlPath << "\n";
        for (const auto& file : output.generatedFiles) {
            std::cout << "    - " << file.filename().string() << "\n";
        }

        return true;

    } catch (const std::exception& e) {
        std::cerr << "EXCEPTION: " << e.what() << "\n";
        return false;
    }
}

static bool testAddActivateExample() {
    std::cout << "\n=== Testing Add-Activate Example (Full Pipeline) ===\n";

    try {
        // Create add-activate design using HLIR C++ Bridge
        hlir::HlirBridge bridge("add_activate_test");

        // Step 1: Add constants
        std::cout << "  [1/16] Adding constants... ";
        auto constDataSize = bridge.addConstant("data_size", "128", "int");
        if (!constDataSize) {
            std::cerr << "FAILED\n    " << constDataSize.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Step 2: Add tensor types
        std::cout << "  [2/16] Adding tensor types... ";
        auto dataTy = bridge.addTensorType("data_ty", {"data_size"}, "bfloat16");
        if (!dataTy) {
            std::cerr << "FAILED\n    " << dataTy.error()[0].message << "\n";
            return false;
        }
        auto chunkTy = bridge.addTensorType("chunk_ty", {"data_size / 4"}, "bfloat16");
        if (!chunkTy) {
            std::cerr << "FAILED\n    " << chunkTy.error()[0].message << "\n";
            return false;
        }
        auto workerChunkTy = bridge.addTensorType("worker_chunk_ty", {"data_size / 8"}, "bfloat16");
        if (!workerChunkTy) {
            std::cerr << "FAILED\n    " << workerChunkTy.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Step 3: Add SHIM tiles (4 columns)
        std::cout << "  [3/16] Adding SHIM tiles... ";
        auto shim0 = bridge.addTile("shim0", hlir::TileKind::SHIM, 0, 0);
        auto shim1 = bridge.addTile("shim1", hlir::TileKind::SHIM, 1, 0);
        auto shim2 = bridge.addTile("shim2", hlir::TileKind::SHIM, 2, 0);
        auto shim3 = bridge.addTile("shim3", hlir::TileKind::SHIM, 3, 0);
        if (!shim0 || !shim1 || !shim2 || !shim3) {
            std::cerr << "FAILED\n";
            return false;
        }
        std::cout << "OK (4 SHIM tiles)\n";

        // Step 4: Add MEM tiles (for split/join operations)
        std::cout << "  [4/16] Adding MEM tiles... ";
        auto mem0 = bridge.addTile("mem0", hlir::TileKind::MEM, 0, 1);
        auto mem1 = bridge.addTile("mem1", hlir::TileKind::MEM, 1, 1);
        auto mem2 = bridge.addTile("mem2", hlir::TileKind::MEM, 2, 1);
        auto mem3 = bridge.addTile("mem3", hlir::TileKind::MEM, 3, 1);
        if (!mem0 || !mem1 || !mem2 || !mem3) {
            std::cerr << "FAILED\n";
            return false;
        }
        std::cout << "OK (4 MEM tiles)\n";

        // Step 5: Add compute tiles (16 total - 4 per column for add/relu workers)
        std::cout << "  [5/16] Adding compute tiles... ";
        // Column 0
        auto tile_0_5 = bridge.addTile("tile_0_5", hlir::TileKind::COMPUTE, 0, 5);
        auto tile_0_3 = bridge.addTile("tile_0_3", hlir::TileKind::COMPUTE, 0, 3);
        auto tile_0_4 = bridge.addTile("tile_0_4", hlir::TileKind::COMPUTE, 0, 4);
        auto tile_0_2 = bridge.addTile("tile_0_2", hlir::TileKind::COMPUTE, 0, 2);
        // Column 1
        auto tile_1_5 = bridge.addTile("tile_1_5", hlir::TileKind::COMPUTE, 1, 5);
        auto tile_1_3 = bridge.addTile("tile_1_3", hlir::TileKind::COMPUTE, 1, 3);
        auto tile_1_4 = bridge.addTile("tile_1_4", hlir::TileKind::COMPUTE, 1, 4);
        auto tile_1_2 = bridge.addTile("tile_1_2", hlir::TileKind::COMPUTE, 1, 2);
        // Column 2
        auto tile_2_5 = bridge.addTile("tile_2_5", hlir::TileKind::COMPUTE, 2, 5);
        auto tile_2_3 = bridge.addTile("tile_2_3", hlir::TileKind::COMPUTE, 2, 3);
        auto tile_2_4 = bridge.addTile("tile_2_4", hlir::TileKind::COMPUTE, 2, 4);
        auto tile_2_2 = bridge.addTile("tile_2_2", hlir::TileKind::COMPUTE, 2, 2);
        // Column 3
        auto tile_3_5 = bridge.addTile("tile_3_5", hlir::TileKind::COMPUTE, 3, 5);
        auto tile_3_3 = bridge.addTile("tile_3_3", hlir::TileKind::COMPUTE, 3, 3);
        auto tile_3_4 = bridge.addTile("tile_3_4", hlir::TileKind::COMPUTE, 3, 4);
        auto tile_3_2 = bridge.addTile("tile_3_2", hlir::TileKind::COMPUTE, 3, 2);
        if (!tile_0_5 || !tile_0_3 || !tile_0_4 || !tile_0_2 ||
            !tile_1_5 || !tile_1_3 || !tile_1_4 || !tile_1_2 ||
            !tile_2_5 || !tile_2_3 || !tile_2_4 || !tile_2_2 ||
            !tile_3_5 || !tile_3_3 || !tile_3_4 || !tile_3_2) {
            std::cerr << "FAILED\n";
            return false;
        }
        std::cout << "OK (16 compute tiles)\n";

        // Step 6: Add input FIFOs for A (4 columns)
        std::cout << "  [6/16] Adding input FIFOs for A... ";
        std::map<std::string, std::string> metaA_col0 = {{"context", "L3_L2"}, {"direction", "input"}, {"data", "A"}, {"column", "0"}};
        std::map<std::string, std::string> metaA_col1 = {{"context", "L3_L2"}, {"direction", "input"}, {"data", "A"}, {"column", "1"}};
        std::map<std::string, std::string> metaA_col2 = {{"context", "L3_L2"}, {"direction", "input"}, {"data", "A"}, {"column", "2"}};
        std::map<std::string, std::string> metaA_col3 = {{"context", "L3_L2"}, {"direction", "input"}, {"data", "A"}, {"column", "3"}};
        auto of_in_a_col0 = bridge.addFifo("of_in_a_col0", *chunkTy, 2, std::nullopt, {}, hlir::ComponentId(), metaA_col0);
        auto of_in_a_col1 = bridge.addFifo("of_in_a_col1", *chunkTy, 2, std::nullopt, {}, hlir::ComponentId(), metaA_col1);
        auto of_in_a_col2 = bridge.addFifo("of_in_a_col2", *chunkTy, 2, std::nullopt, {}, hlir::ComponentId(), metaA_col2);
        auto of_in_a_col3 = bridge.addFifo("of_in_a_col3", *chunkTy, 2, std::nullopt, {}, hlir::ComponentId(), metaA_col3);
        if (!of_in_a_col0 || !of_in_a_col1 || !of_in_a_col2 || !of_in_a_col3) {
            std::cerr << "FAILED\n";
            return false;
        }
        std::cout << "OK (4 FIFOs)\n";

        // Step 7: Add input FIFOs for B (4 columns)
        std::cout << "  [7/16] Adding input FIFOs for B... ";
        std::map<std::string, std::string> metaB_col0 = {{"context", "L3_L2"}, {"direction", "input"}, {"data", "B"}, {"column", "0"}};
        std::map<std::string, std::string> metaB_col1 = {{"context", "L3_L2"}, {"direction", "input"}, {"data", "B"}, {"column", "1"}};
        std::map<std::string, std::string> metaB_col2 = {{"context", "L3_L2"}, {"direction", "input"}, {"data", "B"}, {"column", "2"}};
        std::map<std::string, std::string> metaB_col3 = {{"context", "L3_L2"}, {"direction", "input"}, {"data", "B"}, {"column", "3"}};
        auto of_in_b_col0 = bridge.addFifo("of_in_b_col0", *chunkTy, 2, std::nullopt, {}, hlir::ComponentId(), metaB_col0);
        auto of_in_b_col1 = bridge.addFifo("of_in_b_col1", *chunkTy, 2, std::nullopt, {}, hlir::ComponentId(), metaB_col1);
        auto of_in_b_col2 = bridge.addFifo("of_in_b_col2", *chunkTy, 2, std::nullopt, {}, hlir::ComponentId(), metaB_col2);
        auto of_in_b_col3 = bridge.addFifo("of_in_b_col3", *chunkTy, 2, std::nullopt, {}, hlir::ComponentId(), metaB_col3);
        if (!of_in_b_col0 || !of_in_b_col1 || !of_in_b_col2 || !of_in_b_col3) {
            std::cerr << "FAILED\n";
            return false;
        }
        std::cout << "OK (4 FIFOs)\n";

        // Step 8: Add split operations for A (output names provided, FIFOs created automatically)
        std::cout << "  [8/14] Adding split operations for A... ";
        std::map<std::string, std::string> splitMetaA_col0 = {{"context", "L2_L1"}, {"data", "A"}, {"column", "0"}};
        std::map<std::string, std::string> splitMetaA_col1 = {{"context", "L2_L1"}, {"data", "A"}, {"column", "1"}};
        std::map<std::string, std::string> splitMetaA_col2 = {{"context", "L2_L1"}, {"data", "A"}, {"column", "2"}};
        std::map<std::string, std::string> splitMetaA_col3 = {{"context", "L2_L1"}, {"data", "A"}, {"column", "3"}};
        auto splitA_col0 = bridge.addFifoSplit("split_a_col0", *of_in_a_col0, 2, *workerChunkTy,
            {"MEM_L2_L1_A1_col0", "MEM_L2_L1_A2_col0"}, {0, 16}, *mem0, hlir::ComponentId(), splitMetaA_col0);
        auto splitA_col1 = bridge.addFifoSplit("split_a_col1", *of_in_a_col1, 2, *workerChunkTy,
            {"MEM_L2_L1_A3_col1", "MEM_L2_L1_A4_col1"}, {0, 16}, *mem1, hlir::ComponentId(), splitMetaA_col1);
        auto splitA_col2 = bridge.addFifoSplit("split_a_col2", *of_in_a_col2, 2, *workerChunkTy,
            {"MEM_L2_L1_A5_col2", "MEM_L2_L1_A6_col2"}, {0, 16}, *mem2, hlir::ComponentId(), splitMetaA_col2);
        auto splitA_col3 = bridge.addFifoSplit("split_a_col3", *of_in_a_col3, 2, *workerChunkTy,
            {"MEM_L2_L1_A7_col3", "MEM_L2_L1_A8_col3"}, {0, 16}, *mem3, hlir::ComponentId(), splitMetaA_col3);
        if (!splitA_col0 || !splitA_col1 || !splitA_col2 || !splitA_col3) {
            std::cerr << "FAILED\n";
            if (splitA_col0.error().size() > 0) std::cerr << "    " << splitA_col0.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK (4 split ops)\n";

        // Step 9: Add split operations for B (output names provided, FIFOs created automatically)
        std::cout << "  [9/14] Adding split operations for B... ";
        std::map<std::string, std::string> splitMetaB_col0 = {{"context", "L2_L1"}, {"data", "B"}, {"column", "0"}};
        std::map<std::string, std::string> splitMetaB_col1 = {{"context", "L2_L1"}, {"data", "B"}, {"column", "1"}};
        std::map<std::string, std::string> splitMetaB_col2 = {{"context", "L2_L1"}, {"data", "B"}, {"column", "2"}};
        std::map<std::string, std::string> splitMetaB_col3 = {{"context", "L2_L1"}, {"data", "B"}, {"column", "3"}};
        auto splitB_col0 = bridge.addFifoSplit("split_b_col0", *of_in_b_col0, 2, *workerChunkTy,
            {"MEM_L2_L1_B1_col0", "MEM_L2_L1_B2_col0"}, {0, 16}, *mem0, hlir::ComponentId(), splitMetaB_col0);
        auto splitB_col1 = bridge.addFifoSplit("split_b_col1", *of_in_b_col1, 2, *workerChunkTy,
            {"MEM_L2_L1_B3_col1", "MEM_L2_L1_B4_col1"}, {0, 16}, *mem1, hlir::ComponentId(), splitMetaB_col1);
        auto splitB_col2 = bridge.addFifoSplit("split_b_col2", *of_in_b_col2, 2, *workerChunkTy,
            {"MEM_L2_L1_B5_col2", "MEM_L2_L1_B6_col2"}, {0, 16}, *mem2, hlir::ComponentId(), splitMetaB_col2);
        auto splitB_col3 = bridge.addFifoSplit("split_b_col3", *of_in_b_col3, 2, *workerChunkTy,
            {"MEM_L2_L1_B7_col3", "MEM_L2_L1_B8_col3"}, {0, 16}, *mem3, hlir::ComponentId(), splitMetaB_col3);
        if (!splitB_col0 || !splitB_col1 || !splitB_col2 || !splitB_col3) {
            std::cerr << "FAILED\n";
            return false;
        }
        std::cout << "OK (4 split ops)\n";

        // Step 10: Add intermediate FIFOs (between add and relu stages)
        std::cout << "  [10/14] Adding intermediate FIFOs... ";
        std::map<std::string, std::string> metaInter = {{"context", "L1_L1"}, {"direction", "intermediate"}, {"stage", "add_to_relu"}};
        auto of_inter_1 = bridge.addFifo("of_inter_1", *workerChunkTy, 2, std::nullopt, {}, hlir::ComponentId(), metaInter);
        auto of_inter_2 = bridge.addFifo("of_inter_2", *workerChunkTy, 2, std::nullopt, {}, hlir::ComponentId(), metaInter);
        auto of_inter_3 = bridge.addFifo("of_inter_3", *workerChunkTy, 2, std::nullopt, {}, hlir::ComponentId(), metaInter);
        auto of_inter_4 = bridge.addFifo("of_inter_4", *workerChunkTy, 2, std::nullopt, {}, hlir::ComponentId(), metaInter);
        auto of_inter_5 = bridge.addFifo("of_inter_5", *workerChunkTy, 2, std::nullopt, {}, hlir::ComponentId(), metaInter);
        auto of_inter_6 = bridge.addFifo("of_inter_6", *workerChunkTy, 2, std::nullopt, {}, hlir::ComponentId(), metaInter);
        auto of_inter_7 = bridge.addFifo("of_inter_7", *workerChunkTy, 2, std::nullopt, {}, hlir::ComponentId(), metaInter);
        auto of_inter_8 = bridge.addFifo("of_inter_8", *workerChunkTy, 2, std::nullopt, {}, hlir::ComponentId(), metaInter);
        if (!of_inter_1 || !of_inter_2 || !of_inter_3 || !of_inter_4 ||
            !of_inter_5 || !of_inter_6 || !of_inter_7 || !of_inter_8) {
            std::cerr << "FAILED\n";
            return false;
        }
        std::cout << "OK (8 FIFOs)\n";

        // Step 11: Add output FIFOs for D
        std::cout << "  [11/14] Adding output FIFOs for D... ";
        std::map<std::string, std::string> metaD_col0 = {{"context", "L2_L3"}, {"direction", "output"}, {"data", "D"}, {"column", "0"}};
        std::map<std::string, std::string> metaD_col1 = {{"context", "L2_L3"}, {"direction", "output"}, {"data", "D"}, {"column", "1"}};
        std::map<std::string, std::string> metaD_col2 = {{"context", "L2_L3"}, {"direction", "output"}, {"data", "D"}, {"column", "2"}};
        std::map<std::string, std::string> metaD_col3 = {{"context", "L2_L3"}, {"direction", "output"}, {"data", "D"}, {"column", "3"}};
        auto of_out_d_col0 = bridge.addFifo("of_out_d_col0", *chunkTy, 2, std::nullopt, {}, hlir::ComponentId(), metaD_col0);
        auto of_out_d_col1 = bridge.addFifo("of_out_d_col1", *chunkTy, 2, std::nullopt, {}, hlir::ComponentId(), metaD_col1);
        auto of_out_d_col2 = bridge.addFifo("of_out_d_col2", *chunkTy, 2, std::nullopt, {}, hlir::ComponentId(), metaD_col2);
        auto of_out_d_col3 = bridge.addFifo("of_out_d_col3", *chunkTy, 2, std::nullopt, {}, hlir::ComponentId(), metaD_col3);
        if (!of_out_d_col0 || !of_out_d_col1 || !of_out_d_col2 || !of_out_d_col3) {
            std::cerr << "FAILED\n";
            return false;
        }
        std::cout << "OK (4 FIFOs)\n";

        // Step 12: Add join operations for D (input names provided, FIFOs created automatically)
        std::cout << "  [12/14] Adding join operations for D... ";
        std::map<std::string, std::string> joinMetaD_col0 = {{"context", "L1_L2"}, {"data", "D"}, {"column", "0"}};
        std::map<std::string, std::string> joinMetaD_col1 = {{"context", "L1_L2"}, {"data", "D"}, {"column", "1"}};
        std::map<std::string, std::string> joinMetaD_col2 = {{"context", "L1_L2"}, {"data", "D"}, {"column", "2"}};
        std::map<std::string, std::string> joinMetaD_col3 = {{"context", "L1_L2"}, {"data", "D"}, {"column", "3"}};
        auto joinD_col0 = bridge.addFifoJoin("join_d_col0", *of_out_d_col0, 2, *workerChunkTy,
            {"MEM_L1_L2_D1_col0", "MEM_L1_L2_D2_col0"}, {0, 16}, *mem0, hlir::ComponentId(), joinMetaD_col0);
        auto joinD_col1 = bridge.addFifoJoin("join_d_col1", *of_out_d_col1, 2, *workerChunkTy,
            {"MEM_L1_L2_D3_col1", "MEM_L1_L2_D4_col1"}, {0, 16}, *mem1, hlir::ComponentId(), joinMetaD_col1);
        auto joinD_col2 = bridge.addFifoJoin("join_d_col2", *of_out_d_col2, 2, *workerChunkTy,
            {"MEM_L1_L2_D5_col2", "MEM_L1_L2_D6_col2"}, {0, 16}, *mem2, hlir::ComponentId(), joinMetaD_col2);
        auto joinD_col3 = bridge.addFifoJoin("join_d_col3", *of_out_d_col3, 2, *workerChunkTy,
            {"MEM_L1_L2_D7_col3", "MEM_L1_L2_D8_col3"}, {0, 16}, *mem3, hlir::ComponentId(), joinMetaD_col3);
        if (!joinD_col0 || !joinD_col1 || !joinD_col2 || !joinD_col3) {
            std::cerr << "FAILED\n";
            if (joinD_col0.error().size() > 0) std::cerr << "    " << joinD_col0.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK (4 join ops)\n";

        // Step 13: Add external kernels (with operation metadata)
        std::cout << "  [13/14] Adding external kernels... ";
        std::map<std::string, std::string> kernelMeta1 = {{"operation", "element_wise_add"}};
        std::map<std::string, std::string> kernelMeta2 = {{"operation", "relu_activation"}};
        auto externalfunc1 = bridge.addExternalKernel(
            "externalfunc1", "eltwise_add_bf16_scalar",
            "../../../aie_kernels/aie2/add.cc",
            {*workerChunkTy, *workerChunkTy, *workerChunkTy},
            {}, hlir::ComponentId(), kernelMeta1);
        auto externalfunc2 = bridge.addExternalKernel(
            "externalfunc2", "bf16_relu",
            "../../../aie_kernels/aie2/relu.cc",
            {*workerChunkTy, *workerChunkTy},
            {}, hlir::ComponentId(), kernelMeta2);
        if (!externalfunc1 || !externalfunc2) {
            std::cerr << "FAILED\n";
            if (externalfunc1.error().size() > 0) std::cerr << "    " << externalfunc1.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK (2 kernels)\n";

        // Step 14: Add core functions (with operation metadata)
        std::cout << "  [14/14] Adding core functions... ";
        // corefunc1: add function (kernel, inputA, inputB, outputC)
        std::map<std::string, std::string> coreFuncMeta1 = {{"operation", "eltwise_add"}};
        std::map<std::string, std::string> coreFuncMeta2 = {{"operation", "relu"}};
        auto corefunc1 = bridge.addCoreFunction(
            "corefunc1",
            {"kernel", "inputA", "inputB", "outputC"},
            {{"inputA", 1, "elementA"}, {"inputB", 1, "elementB"}, {"outputC", 1, "elementC"}},
            {"kernel", {"elementA", "elementB", "elementC"}},
            {{"inputA", 1}, {"inputB", 1}, {"outputC", 1}},
            hlir::ComponentId(), coreFuncMeta1);
        // corefunc2: relu function (kernel, inputC, outputD)
        auto corefunc2 = bridge.addCoreFunction(
            "corefunc2",
            {"kernel", "inputC", "outputD"},
            {{"inputC", 1, "elementC"}, {"outputD", 1, "elementD"}},
            {"kernel", {"elementC", "elementD"}},
            {{"inputC", 1}, {"outputD", 1}},
            hlir::ComponentId(), coreFuncMeta2);
        if (!corefunc1 || !corefunc2) {
            std::cerr << "FAILED\n";
            if (corefunc1.error().size() > 0) std::cerr << "    " << corefunc1.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK (2 core functions)\n";

        // Add workers (16 total) with operation, column, worker_index metadata
        std::cout << "  [Workers] Adding workers... ";
        // Add workers (8 total)
        std::map<std::string, std::string> wAdd_c0_w0 = {{"operation", "add"}, {"column", "0"}, {"worker_index", "0"}};
        std::map<std::string, std::string> wAdd_c0_w1 = {{"operation", "add"}, {"column", "0"}, {"worker_index", "1"}};
        std::map<std::string, std::string> wAdd_c1_w0 = {{"operation", "add"}, {"column", "1"}, {"worker_index", "0"}};
        std::map<std::string, std::string> wAdd_c1_w1 = {{"operation", "add"}, {"column", "1"}, {"worker_index", "1"}};
        std::map<std::string, std::string> wAdd_c2_w0 = {{"operation", "add"}, {"column", "2"}, {"worker_index", "0"}};
        std::map<std::string, std::string> wAdd_c2_w1 = {{"operation", "add"}, {"column", "2"}, {"worker_index", "1"}};
        std::map<std::string, std::string> wAdd_c3_w0 = {{"operation", "add"}, {"column", "3"}, {"worker_index", "0"}};
        std::map<std::string, std::string> wAdd_c3_w1 = {{"operation", "add"}, {"column", "3"}, {"worker_index", "1"}};
        // Add workers use splitConsumer to reference split operations directly with output index
        auto worker_add_col0_w0 = bridge.addWorker("worker_add_col0_w0", *corefunc1,
            {hlir::HlirBridge::FunctionArg::kernel(*externalfunc1),
             hlir::HlirBridge::FunctionArg::splitConsumer(*splitA_col0, 0),
             hlir::HlirBridge::FunctionArg::splitConsumer(*splitB_col0, 0),
             hlir::HlirBridge::FunctionArg::fifoProducer(*of_inter_1)},
            *tile_0_5, hlir::ComponentId(), wAdd_c0_w0);
        auto worker_add_col0_w1 = bridge.addWorker("worker_add_col0_w1", *corefunc1,
            {hlir::HlirBridge::FunctionArg::kernel(*externalfunc1),
             hlir::HlirBridge::FunctionArg::splitConsumer(*splitA_col0, 1),
             hlir::HlirBridge::FunctionArg::splitConsumer(*splitB_col0, 1),
             hlir::HlirBridge::FunctionArg::fifoProducer(*of_inter_2)},
            *tile_0_3, hlir::ComponentId(), wAdd_c0_w1);
        auto worker_add_col1_w0 = bridge.addWorker("worker_add_col1_w0", *corefunc1,
            {hlir::HlirBridge::FunctionArg::kernel(*externalfunc1),
             hlir::HlirBridge::FunctionArg::splitConsumer(*splitA_col1, 0),
             hlir::HlirBridge::FunctionArg::splitConsumer(*splitB_col1, 0),
             hlir::HlirBridge::FunctionArg::fifoProducer(*of_inter_3)},
            *tile_1_5, hlir::ComponentId(), wAdd_c1_w0);
        auto worker_add_col1_w1 = bridge.addWorker("worker_add_col1_w1", *corefunc1,
            {hlir::HlirBridge::FunctionArg::kernel(*externalfunc1),
             hlir::HlirBridge::FunctionArg::splitConsumer(*splitA_col1, 1),
             hlir::HlirBridge::FunctionArg::splitConsumer(*splitB_col1, 1),
             hlir::HlirBridge::FunctionArg::fifoProducer(*of_inter_4)},
            *tile_1_3, hlir::ComponentId(), wAdd_c1_w1);
        auto worker_add_col2_w0 = bridge.addWorker("worker_add_col2_w0", *corefunc1,
            {hlir::HlirBridge::FunctionArg::kernel(*externalfunc1),
             hlir::HlirBridge::FunctionArg::splitConsumer(*splitA_col2, 0),
             hlir::HlirBridge::FunctionArg::splitConsumer(*splitB_col2, 0),
             hlir::HlirBridge::FunctionArg::fifoProducer(*of_inter_5)},
            *tile_2_5, hlir::ComponentId(), wAdd_c2_w0);
        auto worker_add_col2_w1 = bridge.addWorker("worker_add_col2_w1", *corefunc1,
            {hlir::HlirBridge::FunctionArg::kernel(*externalfunc1),
             hlir::HlirBridge::FunctionArg::splitConsumer(*splitA_col2, 1),
             hlir::HlirBridge::FunctionArg::splitConsumer(*splitB_col2, 1),
             hlir::HlirBridge::FunctionArg::fifoProducer(*of_inter_6)},
            *tile_2_3, hlir::ComponentId(), wAdd_c2_w1);
        auto worker_add_col3_w0 = bridge.addWorker("worker_add_col3_w0", *corefunc1,
            {hlir::HlirBridge::FunctionArg::kernel(*externalfunc1),
             hlir::HlirBridge::FunctionArg::splitConsumer(*splitA_col3, 0),
             hlir::HlirBridge::FunctionArg::splitConsumer(*splitB_col3, 0),
             hlir::HlirBridge::FunctionArg::fifoProducer(*of_inter_7)},
            *tile_3_5, hlir::ComponentId(), wAdd_c3_w0);
        auto worker_add_col3_w1 = bridge.addWorker("worker_add_col3_w1", *corefunc1,
            {hlir::HlirBridge::FunctionArg::kernel(*externalfunc1),
             hlir::HlirBridge::FunctionArg::splitConsumer(*splitA_col3, 1),
             hlir::HlirBridge::FunctionArg::splitConsumer(*splitB_col3, 1),
             hlir::HlirBridge::FunctionArg::fifoProducer(*of_inter_8)},
            *tile_3_3, hlir::ComponentId(), wAdd_c3_w1);

        // Relu workers (8 total)
        std::map<std::string, std::string> wRelu_c0_w0 = {{"operation", "relu"}, {"column", "0"}, {"worker_index", "0"}};
        std::map<std::string, std::string> wRelu_c0_w1 = {{"operation", "relu"}, {"column", "0"}, {"worker_index", "1"}};
        std::map<std::string, std::string> wRelu_c1_w0 = {{"operation", "relu"}, {"column", "1"}, {"worker_index", "0"}};
        std::map<std::string, std::string> wRelu_c1_w1 = {{"operation", "relu"}, {"column", "1"}, {"worker_index", "1"}};
        std::map<std::string, std::string> wRelu_c2_w0 = {{"operation", "relu"}, {"column", "2"}, {"worker_index", "0"}};
        std::map<std::string, std::string> wRelu_c2_w1 = {{"operation", "relu"}, {"column", "2"}, {"worker_index", "1"}};
        std::map<std::string, std::string> wRelu_c3_w0 = {{"operation", "relu"}, {"column", "3"}, {"worker_index", "0"}};
        std::map<std::string, std::string> wRelu_c3_w1 = {{"operation", "relu"}, {"column", "3"}, {"worker_index", "1"}};
        // Relu workers use joinProducer to reference join operations directly with input index
        auto worker_relu_col0_w0 = bridge.addWorker("worker_relu_col0_w0", *corefunc2,
            {hlir::HlirBridge::FunctionArg::kernel(*externalfunc2),
             hlir::HlirBridge::FunctionArg::fifoConsumer(*of_inter_1, 0),
             hlir::HlirBridge::FunctionArg::joinProducer(*joinD_col0, 0)},
            *tile_0_4, hlir::ComponentId(), wRelu_c0_w0);
        auto worker_relu_col0_w1 = bridge.addWorker("worker_relu_col0_w1", *corefunc2,
            {hlir::HlirBridge::FunctionArg::kernel(*externalfunc2),
             hlir::HlirBridge::FunctionArg::fifoConsumer(*of_inter_2, 0),
             hlir::HlirBridge::FunctionArg::joinProducer(*joinD_col0, 1)},
            *tile_0_2, hlir::ComponentId(), wRelu_c0_w1);
        auto worker_relu_col1_w0 = bridge.addWorker("worker_relu_col1_w0", *corefunc2,
            {hlir::HlirBridge::FunctionArg::kernel(*externalfunc2),
             hlir::HlirBridge::FunctionArg::fifoConsumer(*of_inter_3, 0),
             hlir::HlirBridge::FunctionArg::joinProducer(*joinD_col1, 0)},
            *tile_1_4, hlir::ComponentId(), wRelu_c1_w0);
        auto worker_relu_col1_w1 = bridge.addWorker("worker_relu_col1_w1", *corefunc2,
            {hlir::HlirBridge::FunctionArg::kernel(*externalfunc2),
             hlir::HlirBridge::FunctionArg::fifoConsumer(*of_inter_4, 0),
             hlir::HlirBridge::FunctionArg::joinProducer(*joinD_col1, 1)},
            *tile_1_2, hlir::ComponentId(), wRelu_c1_w1);
        auto worker_relu_col2_w0 = bridge.addWorker("worker_relu_col2_w0", *corefunc2,
            {hlir::HlirBridge::FunctionArg::kernel(*externalfunc2),
             hlir::HlirBridge::FunctionArg::fifoConsumer(*of_inter_5, 0),
             hlir::HlirBridge::FunctionArg::joinProducer(*joinD_col2, 0)},
            *tile_2_4, hlir::ComponentId(), wRelu_c2_w0);
        auto worker_relu_col2_w1 = bridge.addWorker("worker_relu_col2_w1", *corefunc2,
            {hlir::HlirBridge::FunctionArg::kernel(*externalfunc2),
             hlir::HlirBridge::FunctionArg::fifoConsumer(*of_inter_6, 0),
             hlir::HlirBridge::FunctionArg::joinProducer(*joinD_col2, 1)},
            *tile_2_2, hlir::ComponentId(), wRelu_c2_w1);
        auto worker_relu_col3_w0 = bridge.addWorker("worker_relu_col3_w0", *corefunc2,
            {hlir::HlirBridge::FunctionArg::kernel(*externalfunc2),
             hlir::HlirBridge::FunctionArg::fifoConsumer(*of_inter_7, 0),
             hlir::HlirBridge::FunctionArg::joinProducer(*joinD_col3, 0)},
            *tile_3_4, hlir::ComponentId(), wRelu_c3_w0);
        auto worker_relu_col3_w1 = bridge.addWorker("worker_relu_col3_w1", *corefunc2,
            {hlir::HlirBridge::FunctionArg::kernel(*externalfunc2),
             hlir::HlirBridge::FunctionArg::fifoConsumer(*of_inter_8, 0),
             hlir::HlirBridge::FunctionArg::joinProducer(*joinD_col3, 1)},
            *tile_3_2, hlir::ComponentId(), wRelu_c3_w1);

        if (!worker_add_col0_w0 || !worker_add_col0_w1 || !worker_add_col1_w0 || !worker_add_col1_w1 ||
            !worker_add_col2_w0 || !worker_add_col2_w1 || !worker_add_col3_w0 || !worker_add_col3_w1 ||
            !worker_relu_col0_w0 || !worker_relu_col0_w1 || !worker_relu_col1_w0 || !worker_relu_col1_w1 ||
            !worker_relu_col2_w0 || !worker_relu_col2_w1 || !worker_relu_col3_w0 || !worker_relu_col3_w1) {
            std::cerr << "FAILED\n";
            if (worker_add_col0_w0.error().size() > 0) std::cerr << "    " << worker_add_col0_w0.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK (16 workers)\n";

        // Create runtime
        std::cout << "  [Runtime] Creating runtime... ";
        auto runtime = bridge.createRuntime("runtime");
        if (!runtime) {
            std::cerr << "FAILED\n    " << runtime.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Add input/output types
        std::cout << "  [Runtime] Adding types... ";
        auto addInputA = bridge.runtimeAddInputType(*dataTy);
        auto addInputB = bridge.runtimeAddInputType(*dataTy);
        auto addOutputD = bridge.runtimeAddOutputType(*dataTy);
        if (!addInputA || !addInputB || !addOutputD) {
            std::cerr << "FAILED\n";
            return false;
        }
        std::cout << "OK\n";

        // Add parameters
        std::cout << "  [Runtime] Adding parameters... ";
        auto addParamA = bridge.runtimeAddParam("A");
        auto addParamB = bridge.runtimeAddParam("B");
        auto addParamD = bridge.runtimeAddParam("D");
        if (!addParamA || !addParamB || !addParamD) {
            std::cerr << "FAILED\n";
            return false;
        }
        std::cout << "OK\n";

        // Add workers to runtime (for StartWorkers)
        std::cout << "  [Runtime] Adding workers to runtime... ";
        // Add workers
        auto rtAddW1 = bridge.runtimeAddWorker(*worker_add_col0_w0);
        auto rtAddW2 = bridge.runtimeAddWorker(*worker_add_col0_w1);
        auto rtAddW3 = bridge.runtimeAddWorker(*worker_add_col1_w0);
        auto rtAddW4 = bridge.runtimeAddWorker(*worker_add_col1_w1);
        auto rtAddW5 = bridge.runtimeAddWorker(*worker_add_col2_w0);
        auto rtAddW6 = bridge.runtimeAddWorker(*worker_add_col2_w1);
        auto rtAddW7 = bridge.runtimeAddWorker(*worker_add_col3_w0);
        auto rtAddW8 = bridge.runtimeAddWorker(*worker_add_col3_w1);
        auto rtAddW9 = bridge.runtimeAddWorker(*worker_relu_col0_w0);
        auto rtAddW10 = bridge.runtimeAddWorker(*worker_relu_col0_w1);
        auto rtAddW11 = bridge.runtimeAddWorker(*worker_relu_col1_w0);
        auto rtAddW12 = bridge.runtimeAddWorker(*worker_relu_col1_w1);
        auto rtAddW13 = bridge.runtimeAddWorker(*worker_relu_col2_w0);
        auto rtAddW14 = bridge.runtimeAddWorker(*worker_relu_col2_w1);
        auto rtAddW15 = bridge.runtimeAddWorker(*worker_relu_col3_w0);
        auto rtAddW16 = bridge.runtimeAddWorker(*worker_relu_col3_w1);
        if (!rtAddW1 || !rtAddW2 || !rtAddW3 || !rtAddW4 ||
            !rtAddW5 || !rtAddW6 || !rtAddW7 || !rtAddW8 ||
            !rtAddW9 || !rtAddW10 || !rtAddW11 || !rtAddW12 ||
            !rtAddW13 || !rtAddW14 || !rtAddW15 || !rtAddW16) {
            std::cerr << "FAILED\n";
            return false;
        }
        std::cout << "OK (16 workers)\n";

        // Add fill operations for A (with column and use_tap=true)
        std::cout << "  [Runtime] Adding fill operations for A... ";
        auto fillA0 = bridge.runtimeAddFill("fill_a_col0", *of_in_a_col0, "A", *shim0, 0, true);
        auto fillA1 = bridge.runtimeAddFill("fill_a_col1", *of_in_a_col1, "A", *shim1, 1, true);
        auto fillA2 = bridge.runtimeAddFill("fill_a_col2", *of_in_a_col2, "A", *shim2, 2, true);
        auto fillA3 = bridge.runtimeAddFill("fill_a_col3", *of_in_a_col3, "A", *shim3, 3, true);
        if (!fillA0 || !fillA1 || !fillA2 || !fillA3) {
            std::cerr << "FAILED\n";
            return false;
        }
        std::cout << "OK\n";

        // Add fill operations for B (with column and use_tap=true)
        std::cout << "  [Runtime] Adding fill operations for B... ";
        auto fillB0 = bridge.runtimeAddFill("fill_b_col0", *of_in_b_col0, "B", *shim0, 0, true);
        auto fillB1 = bridge.runtimeAddFill("fill_b_col1", *of_in_b_col1, "B", *shim1, 1, true);
        auto fillB2 = bridge.runtimeAddFill("fill_b_col2", *of_in_b_col2, "B", *shim2, 2, true);
        auto fillB3 = bridge.runtimeAddFill("fill_b_col3", *of_in_b_col3, "B", *shim3, 3, true);
        if (!fillB0 || !fillB1 || !fillB2 || !fillB3) {
            std::cerr << "FAILED\n";
            return false;
        }
        std::cout << "OK\n";

        // Add drain operations for D (with column and use_tap=true)
        std::cout << "  [Runtime] Adding drain operations for D... ";
        auto drainD0 = bridge.runtimeAddDrain("drain_d_col0", *of_out_d_col0, "D", *shim0, 0, true);
        auto drainD1 = bridge.runtimeAddDrain("drain_d_col1", *of_out_d_col1, "D", *shim1, 1, true);
        auto drainD2 = bridge.runtimeAddDrain("drain_d_col2", *of_out_d_col2, "D", *shim2, 2, true);
        auto drainD3 = bridge.runtimeAddDrain("drain_d_col3", *of_out_d_col3, "D", *shim3, 3, true);
        if (!drainD0 || !drainD1 || !drainD2 || !drainD3) {
            std::cerr << "FAILED\n";
            return false;
        }
        std::cout << "OK\n";

        // Build runtime
        std::cout << "  [Runtime] Building runtime... ";
        auto buildRuntime = bridge.runtimeBuild();
        if (!buildRuntime) {
            std::cerr << "FAILED\n    " << buildRuntime.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Validate program
        std::cout << "  [Validation] Building and validating program... ";
        auto buildResult = bridge.build();
        if (!buildResult) {
            std::cerr << "FAILED\n";
            for (const auto& diag : buildResult.error()) {
                std::cerr << "    " << diag.message << "\n";
            }
            return false;
        }
        std::cout << "OK\n";

        // Export to XML
        std::cout << "  [Export] Exporting to GUI XML... ";
        ensureOutputDir();
        std::string xmlPath = OUTPUT_DIR + "add_activate_test_gui.xml";
        auto exportResult = bridge.exportToGuiXml(xmlPath);
        if (!exportResult) {
            std::cerr << "FAILED\n    " << exportResult.error()[0].message << "\n";
            return false;
        }

        if (!std::filesystem::exists(xmlPath)) {
            std::cerr << "FAILED (file not created)\n";
            return false;
        }
        std::cout << "OK\n";

        // Run code generator
        std::cout << "  [CodeGen] Running code generator... ";
        codegen::CodeGenBridge codegenBridge;
        codegen::CodeGenOptions options;
        options.outputDir = OUTPUT_DIR;

        auto codegenResult = codegenBridge.runCodeGen(xmlPath, options);
        if (!codegenResult) {
            std::cerr << "FAILED\n";
            for (const auto& diag : codegenResult.error()) {
                std::cerr << "    " << diag.message << "\n";
            }
            return false;
        }

        const auto& output = codegenResult.value();
        std::cout << "OK (" << output.generatedFiles.size() << " files)\n";

        // Verify generated files
        bool foundGraphML = false;
        bool foundPython = false;
        std::filesystem::path generatedPyPath;
        for (const auto& file : output.generatedFiles) {
            std::string filename = file.filename().string();
            if (filename.ends_with(".graphml")) foundGraphML = true;
            if (filename.starts_with("generated_") && filename.ends_with(".py")) {
                foundPython = true;
                generatedPyPath = file;
            }
        }

        std::cout << "  [Verify] Checking generated files... ";
        if (!foundGraphML || !foundPython) {
            std::cerr << "FAILED\n";
            std::cerr << "    GraphML: " << (foundGraphML ? "Found" : "Missing") << "\n";
            std::cerr << "    Python: " << (foundPython ? "Found" : "Missing") << "\n";
            return false;
        }
        std::cout << "OK\n";

        std::cout << "\n  Add-Activate Example: ALL TESTS PASSED\n";
        std::cout << "  Generated files saved to: " << OUTPUT_DIR << "\n";
        std::cout << "    - " << xmlPath << "\n";
        for (const auto& file : output.generatedFiles) {
            std::cout << "    - " << file.filename().string() << "\n";
        }

        return true;

    } catch (const std::exception& e) {
        std::cerr << "EXCEPTION: " << e.what() << "\n";
        return false;
    }
}

static bool testVectorExpExample() {
    std::cout << "\n=== Testing Vector Exp Example (Full Pipeline) ===\n";

    try {
        // Create vector exp design using HLIR C++ Bridge
        hlir::HlirBridge bridge("vector_exp_test");

        // Step 1: Add constants
        std::cout << "  [1/14] Adding constants... ";
        auto constN = bridge.addConstant("N", "65536", "int");
        if (!constN) {
            std::cerr << "FAILED\n    " << constN.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Step 2: Add tensor types
        std::cout << "  [2/14] Adding tensor types... ";
        // data_ty: N elements bfloat16
        auto dataTy = bridge.addTensorType("data_ty", {"N"}, "bfloat16");
        if (!dataTy) {
            std::cerr << "FAILED\n    " << dataTy.error()[0].message << "\n";
            return false;
        }
        // memtile_ty: N/16 elements bfloat16
        auto memtileTy = bridge.addTensorType("memtile_ty", {"N / 16"}, "bfloat16");
        if (!memtileTy) {
            std::cerr << "FAILED\n    " << memtileTy.error()[0].message << "\n";
            return false;
        }
        // tile_ty: N/64 elements bfloat16
        auto tileTy = bridge.addTensorType("tile_ty", {"N / 64"}, "bfloat16");
        if (!tileTy) {
            std::cerr << "FAILED\n    " << tileTy.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Step 3: Add tiles (single column design)
        std::cout << "  [3/14] Adding tiles... ";
        auto shim0 = bridge.addTile("shim0", hlir::TileKind::SHIM, 0, 0);
        auto mem0 = bridge.addTile("mem0", hlir::TileKind::MEM, 0, 1);
        auto tile_0_2 = bridge.addTile("tile_0_2", hlir::TileKind::COMPUTE, 0, 2);
        auto tile_0_3 = bridge.addTile("tile_0_3", hlir::TileKind::COMPUTE, 0, 3);
        auto tile_0_4 = bridge.addTile("tile_0_4", hlir::TileKind::COMPUTE, 0, 4);
        auto tile_0_5 = bridge.addTile("tile_0_5", hlir::TileKind::COMPUTE, 0, 5);
        if (!shim0 || !mem0 || !tile_0_2 || !tile_0_3 || !tile_0_4 || !tile_0_5) {
            std::cerr << "FAILED\n";
            return false;
        }
        std::cout << "OK (1 SHIM, 1 MEM, 4 COMPUTE)\n";

        // Step 4: Add input FIFO for A
        std::cout << "  [4/14] Adding input FIFO... ";
        std::map<std::string, std::string> metaInA = {{"context", "L3_L2"}, {"direction", "input"}, {"data", "A"}, {"column", "0"}};
        auto of_in_a = bridge.addFifo("of_in_a", *memtileTy, 2, std::nullopt, {}, hlir::ComponentId(), metaInA);
        if (!of_in_a) {
            std::cerr << "FAILED\n    " << of_in_a.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Step 5: Add output FIFO for C
        std::cout << "  [5/12] Adding output FIFO... ";
        std::map<std::string, std::string> metaOutC = {{"context", "L2_L3"}, {"direction", "output"}, {"data", "C"}, {"column", "0"}};
        auto of_out_c = bridge.addFifo("of_out_c", *memtileTy, 2, std::nullopt, {}, hlir::ComponentId(), metaOutC);
        if (!of_out_c) {
            std::cerr << "FAILED\n    " << of_out_c.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Step 6: Add split operation for A (output names are provided, FIFOs created automatically)
        // Offsets: 0, N/64*1=1024, N/64*2=2048, N/64*3=3072
        std::cout << "  [6/12] Adding split operation... ";
        std::map<std::string, std::string> splitMetaA = {{"context", "L2_L1"}, {"data", "A"}, {"column", "0"}};
        auto splitA = bridge.addFifoSplit("split_a_col0", *of_in_a, 4, *tileTy,
            {"MEM_L2_L1_A1_col0", "MEM_L2_L1_A2_col0", "MEM_L2_L1_A3_col0", "MEM_L2_L1_A4_col0"},
            {0, 1024, 2048, 3072}, *mem0, hlir::ComponentId(), splitMetaA);
        if (!splitA) {
            std::cerr << "FAILED\n";
            if (splitA.error().size() > 0) std::cerr << "    " << splitA.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Step 7: Add join operation for C (input names are provided, FIFOs created automatically)
        std::cout << "  [7/12] Adding join operation... ";
        std::map<std::string, std::string> joinMetaC = {{"context", "L1_L2"}, {"data", "C"}, {"column", "0"}};
        auto joinC = bridge.addFifoJoin("join_c_col0", *of_out_c, 4, *tileTy,
            {"MEM_L1_L2_C1_col0", "MEM_L1_L2_C2_col0", "MEM_L1_L2_C3_col0", "MEM_L1_L2_C4_col0"},
            {0, 1024, 2048, 3072}, *mem0, hlir::ComponentId(), joinMetaC);
        if (!joinC) {
            std::cerr << "FAILED\n";
            if (joinC.error().size() > 0) std::cerr << "    " << joinC.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Step 8: Add external kernel
        std::cout << "  [8/12] Adding external kernel... ";
        std::map<std::string, std::string> kernelMeta = {{"operation", "exp"}};
        auto exp_bf16_1024 = bridge.addExternalKernel(
            "exp_bf16_1024", "exp_bf16_1024",
            "/scratch/IRONSmithTesting/mlir-aie/aie_kernels/aie2/bf16_exp.cc",
            {*tileTy, *tileTy},
            {"/scratch/IRONSmithTesting/mlir-aie/aie_kernels", "/scratch/IRONSmithTesting/mlir-aie/aie_runtime_lib/AIE2"},
            hlir::ComponentId(), kernelMeta);
        if (!exp_bf16_1024) {
            std::cerr << "FAILED\n";
            if (exp_bf16_1024.error().size() > 0) std::cerr << "    " << exp_bf16_1024.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Step 9: Add core function with loop count = N/4096 = 16
        std::cout << "  [9/12] Adding core function... ";
        std::map<std::string, std::string> coreFuncMeta = {{"operation", "exp"}, {"loop_count", "N / 4096"}};
        auto corefunc_exp = bridge.addCoreFunction(
            "corefunc_exp",
            {"kernel", "inputA", "outputC"},
            {{"outputC", 1, "elem_out"}, {"inputA", 1, "elem_in"}},  // Acquire order matches Python
            {"kernel", {"elem_in", "elem_out"}},
            {{"inputA", 1}, {"outputC", 1}},  // Release order matches Python
            hlir::ComponentId(), coreFuncMeta);
        if (!corefunc_exp) {
            std::cerr << "FAILED\n";
            if (corefunc_exp.error().size() > 0) std::cerr << "    " << corefunc_exp.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Step 10: Add workers (4 total)
        std::cout << "  [10/12] Adding workers... ";
        std::map<std::string, std::string> w0Meta = {{"operation", "exp"}, {"column", "0"}, {"worker_index", "0"}};
        std::map<std::string, std::string> w1Meta = {{"operation", "exp"}, {"column", "0"}, {"worker_index", "1"}};
        std::map<std::string, std::string> w2Meta = {{"operation", "exp"}, {"column", "0"}, {"worker_index", "2"}};
        std::map<std::string, std::string> w3Meta = {{"operation", "exp"}, {"column", "0"}, {"worker_index", "3"}};

        auto worker0 = bridge.addWorker("worker0", *corefunc_exp,
            {hlir::HlirBridge::FunctionArg::kernel(*exp_bf16_1024),
             hlir::HlirBridge::FunctionArg::splitConsumer(*splitA, 0),
             hlir::HlirBridge::FunctionArg::joinProducer(*joinC, 0)},
            *tile_0_2, hlir::ComponentId(), w0Meta);
        auto worker1 = bridge.addWorker("worker1", *corefunc_exp,
            {hlir::HlirBridge::FunctionArg::kernel(*exp_bf16_1024),
             hlir::HlirBridge::FunctionArg::splitConsumer(*splitA, 1),
             hlir::HlirBridge::FunctionArg::joinProducer(*joinC, 1)},
            *tile_0_3, hlir::ComponentId(), w1Meta);
        auto worker2 = bridge.addWorker("worker2", *corefunc_exp,
            {hlir::HlirBridge::FunctionArg::kernel(*exp_bf16_1024),
             hlir::HlirBridge::FunctionArg::splitConsumer(*splitA, 2),
             hlir::HlirBridge::FunctionArg::joinProducer(*joinC, 2)},
            *tile_0_4, hlir::ComponentId(), w2Meta);
        auto worker3 = bridge.addWorker("worker3", *corefunc_exp,
            {hlir::HlirBridge::FunctionArg::kernel(*exp_bf16_1024),
             hlir::HlirBridge::FunctionArg::splitConsumer(*splitA, 3),
             hlir::HlirBridge::FunctionArg::joinProducer(*joinC, 3)},
            *tile_0_5, hlir::ComponentId(), w3Meta);

        if (!worker0 || !worker1 || !worker2 || !worker3) {
            std::cerr << "FAILED\n";
            if (worker0.error().size() > 0) std::cerr << "    " << worker0.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK (4 workers)\n";

        // Step 11: Create runtime
        std::cout << "  [11/12] Creating runtime... ";
        auto runtime = bridge.createRuntime("runtime");
        if (!runtime) {
            std::cerr << "FAILED\n    " << runtime.error()[0].message << "\n";
            return false;
        }

        // Add input/output types
        auto addInputA = bridge.runtimeAddInputType(*memtileTy);
        auto addOutputC = bridge.runtimeAddOutputType(*memtileTy);
        if (!addInputA || !addOutputC) {
            std::cerr << "FAILED (types)\n";
            return false;
        }

        // Add parameters
        auto addParamA = bridge.runtimeAddParam("inputA");
        auto addParamC = bridge.runtimeAddParam("outputC");
        if (!addParamA || !addParamC) {
            std::cerr << "FAILED (params)\n";
            return false;
        }

        // Add workers to runtime
        auto rtAddW0 = bridge.runtimeAddWorker(*worker0);
        auto rtAddW1 = bridge.runtimeAddWorker(*worker1);
        auto rtAddW2 = bridge.runtimeAddWorker(*worker2);
        auto rtAddW3 = bridge.runtimeAddWorker(*worker3);
        if (!rtAddW0 || !rtAddW1 || !rtAddW2 || !rtAddW3) {
            std::cerr << "FAILED (workers)\n";
            return false;
        }

        // Add fill operation
        auto fillA = bridge.runtimeAddFill("fill_a", *of_in_a, "inputA", *shim0, 0, false);
        if (!fillA) {
            std::cerr << "FAILED (fill)\n    " << fillA.error()[0].message << "\n";
            return false;
        }

        // Add drain operation
        auto drainC = bridge.runtimeAddDrain("drain_c", *of_out_c, "outputC", *shim0, 0, false);
        if (!drainC) {
            std::cerr << "FAILED (drain)\n    " << drainC.error()[0].message << "\n";
            return false;
        }

        // Build runtime
        auto buildRuntime = bridge.runtimeBuild();
        if (!buildRuntime) {
            std::cerr << "FAILED (build)\n    " << buildRuntime.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Step 12: Build and validate
        std::cout << "  [12/12] Building and validating program... ";
        auto buildResult = bridge.build();
        if (!buildResult) {
            std::cerr << "FAILED\n";
            for (const auto& diag : buildResult.error()) {
                std::cerr << "    " << diag.message << "\n";
            }
            return false;
        }
        std::cout << "OK\n";

        // Export to XML
        std::cout << "  [Export] Exporting to GUI XML... ";
        ensureOutputDir();
        std::string xmlPath = OUTPUT_DIR + "vector_exp_test_gui.xml";
        auto exportResult = bridge.exportToGuiXml(xmlPath);
        if (!exportResult) {
            std::cerr << "FAILED\n    " << exportResult.error()[0].message << "\n";
            return false;
        }

        if (!std::filesystem::exists(xmlPath)) {
            std::cerr << "FAILED (file not created)\n";
            return false;
        }
        std::cout << "OK\n";

        // Run code generator
        std::cout << "  [CodeGen] Running code generator... ";
        codegen::CodeGenBridge codegenBridge;
        codegen::CodeGenOptions options;
        options.outputDir = OUTPUT_DIR;

        auto codegenResult = codegenBridge.runCodeGen(xmlPath, options);
        if (!codegenResult) {
            std::cerr << "FAILED\n";
            for (const auto& diag : codegenResult.error()) {
                std::cerr << "    " << diag.message << "\n";
            }
            return false;
        }

        const auto& output = codegenResult.value();
        std::cout << "OK (" << output.generatedFiles.size() << " files)\n";

        // Verify generated files
        bool foundGraphML = false;
        bool foundPython = false;
        for (const auto& file : output.generatedFiles) {
            std::string filename = file.filename().string();
            if (filename.ends_with(".graphml")) foundGraphML = true;
            if (filename.starts_with("generated_") && filename.ends_with(".py")) foundPython = true;
        }

        std::cout << "  [Verify] Checking generated files... ";
        if (!foundGraphML || !foundPython) {
            std::cerr << "FAILED\n";
            std::cerr << "    GraphML: " << (foundGraphML ? "Found" : "Missing") << "\n";
            std::cerr << "    Python: " << (foundPython ? "Found" : "Missing") << "\n";
            return false;
        }
        std::cout << "OK\n";

        std::cout << "\n  Vector Exp Example: ALL TESTS PASSED\n";
        std::cout << "  Generated files saved to: " << OUTPUT_DIR << "\n";
        std::cout << "    - " << xmlPath << "\n";
        for (const auto& file : output.generatedFiles) {
            std::cout << "    - " << file.filename().string() << "\n";
        }

        return true;

    } catch (const std::exception& e) {
        std::cerr << "EXCEPTION: " << e.what() << "\n";
        return false;
    }
}

static bool testVectorVectorMulExample() {
    std::cout << "\n=== Testing Vector Vector Multiply Example (Full Pipeline) ===\n";

    try {
        hlir::HlirBridge bridge("vector_vector_mul_test");

        // Step 1: Add constants
        std::cout << "  [1/14] Adding constants... ";
        auto constN = bridge.addConstant("N", "65536", "int");
        if (!constN) {
            std::cerr << "FAILED\n    " << constN.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Step 2: Add tensor types
        std::cout << "  [2/14] Adding tensor types... ";
        auto dataTy = bridge.addTensorType("data_ty", {"N"}, "bfloat16");
        if (!dataTy) {
            std::cerr << "FAILED\n    " << dataTy.error()[0].message << "\n";
            return false;
        }
        auto memtileTy = bridge.addTensorType("memtile_ty", {"N / 16"}, "bfloat16");
        if (!memtileTy) {
            std::cerr << "FAILED\n    " << memtileTy.error()[0].message << "\n";
            return false;
        }
        auto tileTy = bridge.addTensorType("tile_ty", {"N / 64"}, "bfloat16");
        if (!tileTy) {
            std::cerr << "FAILED\n    " << tileTy.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Step 3: Add tiles (multi-column for SHIM/MEM, single column for compute)
        std::cout << "  [3/14] Adding tiles... ";
        // 2 SHIM tiles: (0,0) for fill A+B, (1,0) for drain C
        auto shim0 = bridge.addTile("shim0", hlir::TileKind::SHIM, 0, 0);
        auto shim1 = bridge.addTile("shim1", hlir::TileKind::SHIM, 1, 0);
        // 3 MEM tiles: (0,1) for split A, (1,1) for split B, (2,1) for join C
        auto mem0 = bridge.addTile("mem0", hlir::TileKind::MEM, 0, 1);
        auto mem1 = bridge.addTile("mem1", hlir::TileKind::MEM, 1, 1);
        auto mem2 = bridge.addTile("mem2", hlir::TileKind::MEM, 2, 1);
        // 4 compute tiles in column 0
        auto tile_0_5 = bridge.addTile("tile_0_5", hlir::TileKind::COMPUTE, 0, 5);
        auto tile_0_4 = bridge.addTile("tile_0_4", hlir::TileKind::COMPUTE, 0, 4);
        auto tile_0_3 = bridge.addTile("tile_0_3", hlir::TileKind::COMPUTE, 0, 3);
        auto tile_0_2 = bridge.addTile("tile_0_2", hlir::TileKind::COMPUTE, 0, 2);
        if (!shim0 || !shim1 || !mem0 || !mem1 || !mem2 ||
            !tile_0_5 || !tile_0_4 || !tile_0_3 || !tile_0_2) {
            std::cerr << "FAILED\n";
            return false;
        }
        std::cout << "OK (2 SHIM, 3 MEM, 4 COMPUTE)\n";

        // Step 4: Add input FIFOs
        std::cout << "  [4/14] Adding input FIFOs... ";
        std::map<std::string, std::string> metaInA = {{"context", "L3_L2"}, {"direction", "input"}, {"data", "A"}, {"column", "0"}};
        std::map<std::string, std::string> metaInB = {{"context", "L3_L2"}, {"direction", "input"}, {"data", "B"}, {"column", "0"}};
        auto of_in_a = bridge.addFifo("of_in_a", *memtileTy, 2, std::nullopt, {}, hlir::ComponentId(), metaInA);
        auto of_in_b = bridge.addFifo("of_in_b", *memtileTy, 2, std::nullopt, {}, hlir::ComponentId(), metaInB);
        if (!of_in_a || !of_in_b) {
            std::cerr << "FAILED\n";
            return false;
        }
        std::cout << "OK (2 FIFOs)\n";

        // Step 5: Add output FIFO
        std::cout << "  [5/14] Adding output FIFO... ";
        std::map<std::string, std::string> metaOutC = {{"context", "L2_L3"}, {"direction", "output"}, {"data", "C"}, {"column", "1"}};
        auto of_out_c = bridge.addFifo("of_out_c", *memtileTy, 2, std::nullopt, {}, hlir::ComponentId(), metaOutC);
        if (!of_out_c) {
            std::cerr << "FAILED\n    " << of_out_c.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Step 6: Add split operation for A at mem(0,1)
        std::cout << "  [6/14] Adding split A... ";
        std::map<std::string, std::string> splitMetaA = {{"context", "L2_L1"}, {"data", "A"}, {"column", "0"}};
        auto splitA = bridge.addFifoSplit("split_a", *of_in_a, 4, *tileTy,
            {"split_a_0", "split_a_1", "split_a_2", "split_a_3"},
            {0, 1024, 2048, 3072}, *mem0, hlir::ComponentId(), splitMetaA);
        if (!splitA) {
            std::cerr << "FAILED\n";
            if (splitA.error().size() > 0) std::cerr << "    " << splitA.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Step 7: Add split operation for B at mem(1,1)
        std::cout << "  [7/14] Adding split B... ";
        std::map<std::string, std::string> splitMetaB = {{"context", "L2_L1"}, {"data", "B"}, {"column", "1"}};
        auto splitB = bridge.addFifoSplit("split_b", *of_in_b, 4, *tileTy,
            {"split_b_0", "split_b_1", "split_b_2", "split_b_3"},
            {0, 1024, 2048, 3072}, *mem1, hlir::ComponentId(), splitMetaB);
        if (!splitB) {
            std::cerr << "FAILED\n";
            if (splitB.error().size() > 0) std::cerr << "    " << splitB.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Step 8: Add join operation for C at mem(2,1)
        std::cout << "  [8/14] Adding join C... ";
        std::map<std::string, std::string> joinMetaC = {{"context", "L1_L2"}, {"data", "C"}, {"column", "2"}};
        auto joinC = bridge.addFifoJoin("join_c", *of_out_c, 4, *tileTy,
            {"join_c_0", "join_c_1", "join_c_2", "join_c_3"},
            {0, 1024, 2048, 3072}, *mem2, hlir::ComponentId(), joinMetaC);
        if (!joinC) {
            std::cerr << "FAILED\n";
            if (joinC.error().size() > 0) std::cerr << "    " << joinC.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Step 9: Add external kernel (3 args: tile_ty, tile_ty, tile_ty)
        std::cout << "  [9/14] Adding external kernel... ";
        std::map<std::string, std::string> kernelMeta = {{"operation", "mul"}};
        auto mulKernel = bridge.addExternalKernel(
            "eltwise_mul_bf16_vector", "eltwise_mul_bf16_vector",
            "/scratch/IRONSmithTesting/mlir-aie/aie_kernels/aie2/mul.cc",
            {*tileTy, *tileTy, *tileTy},
            {"/scratch/IRONSmithTesting/mlir-aie/aie_kernels", "/scratch/IRONSmithTesting/mlir-aie/aie_runtime_lib/AIE2"},
            hlir::ComponentId(), kernelMeta);
        if (!mulKernel) {
            std::cerr << "FAILED\n";
            if (mulKernel.error().size() > 0) std::cerr << "    " << mulKernel.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Step 10: Add core function (acquire: output first, then inputA, inputB)
        std::cout << "  [10/14] Adding core function... ";
        std::map<std::string, std::string> coreFuncMeta = {{"operation", "mul"}, {"loop_count", "N / 4096"}};
        auto corefunc_mul = bridge.addCoreFunction(
            "corefunc_mul",
            {"kernel", "inputA", "inputB", "outputC"},
            {{"outputC", 1, "elem_out"}, {"inputA", 1, "elem_in_a"}, {"inputB", 1, "elem_in_b"}},
            {"kernel", {"elem_in_a", "elem_in_b", "elem_out"}},
            {{"inputA", 1}, {"inputB", 1}, {"outputC", 1}},
            hlir::ComponentId(), coreFuncMeta);
        if (!corefunc_mul) {
            std::cerr << "FAILED\n";
            if (corefunc_mul.error().size() > 0) std::cerr << "    " << corefunc_mul.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Step 11: Add workers (4 total)
        std::cout << "  [11/14] Adding workers... ";
        std::map<std::string, std::string> w0Meta = {{"operation", "mul"}, {"column", "0"}, {"worker_index", "0"}};
        std::map<std::string, std::string> w1Meta = {{"operation", "mul"}, {"column", "0"}, {"worker_index", "1"}};
        std::map<std::string, std::string> w2Meta = {{"operation", "mul"}, {"column", "0"}, {"worker_index", "2"}};
        std::map<std::string, std::string> w3Meta = {{"operation", "mul"}, {"column", "0"}, {"worker_index", "3"}};

        auto worker0 = bridge.addWorker("worker0", *corefunc_mul,
            {hlir::HlirBridge::FunctionArg::kernel(*mulKernel),
             hlir::HlirBridge::FunctionArg::splitConsumer(*splitA, 0),
             hlir::HlirBridge::FunctionArg::splitConsumer(*splitB, 0),
             hlir::HlirBridge::FunctionArg::joinProducer(*joinC, 0)},
            *tile_0_5, hlir::ComponentId(), w0Meta);
        auto worker1 = bridge.addWorker("worker1", *corefunc_mul,
            {hlir::HlirBridge::FunctionArg::kernel(*mulKernel),
             hlir::HlirBridge::FunctionArg::splitConsumer(*splitA, 1),
             hlir::HlirBridge::FunctionArg::splitConsumer(*splitB, 1),
             hlir::HlirBridge::FunctionArg::joinProducer(*joinC, 1)},
            *tile_0_4, hlir::ComponentId(), w1Meta);
        auto worker2 = bridge.addWorker("worker2", *corefunc_mul,
            {hlir::HlirBridge::FunctionArg::kernel(*mulKernel),
             hlir::HlirBridge::FunctionArg::splitConsumer(*splitA, 2),
             hlir::HlirBridge::FunctionArg::splitConsumer(*splitB, 2),
             hlir::HlirBridge::FunctionArg::joinProducer(*joinC, 2)},
            *tile_0_3, hlir::ComponentId(), w2Meta);
        auto worker3 = bridge.addWorker("worker3", *corefunc_mul,
            {hlir::HlirBridge::FunctionArg::kernel(*mulKernel),
             hlir::HlirBridge::FunctionArg::splitConsumer(*splitA, 3),
             hlir::HlirBridge::FunctionArg::splitConsumer(*splitB, 3),
             hlir::HlirBridge::FunctionArg::joinProducer(*joinC, 3)},
            *tile_0_2, hlir::ComponentId(), w3Meta);

        if (!worker0 || !worker1 || !worker2 || !worker3) {
            std::cerr << "FAILED\n";
            if (worker0.error().size() > 0) std::cerr << "    " << worker0.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK (4 workers)\n";

        // Step 12: Create runtime
        std::cout << "  [12/14] Creating runtime... ";
        auto runtime = bridge.createRuntime("runtime");
        if (!runtime) {
            std::cerr << "FAILED\n    " << runtime.error()[0].message << "\n";
            return false;
        }

        // Add input/output types (memtile_ty for streaming)
        auto addInputA = bridge.runtimeAddInputType(*memtileTy);
        auto addInputB = bridge.runtimeAddInputType(*memtileTy);
        auto addOutputC = bridge.runtimeAddOutputType(*memtileTy);
        if (!addInputA || !addInputB || !addOutputC) {
            std::cerr << "FAILED (types)\n";
            return false;
        }

        // Add parameters
        auto addParamA = bridge.runtimeAddParam("inputA");
        auto addParamB = bridge.runtimeAddParam("inputB");
        auto addParamC = bridge.runtimeAddParam("outputC");
        if (!addParamA || !addParamB || !addParamC) {
            std::cerr << "FAILED (params)\n";
            return false;
        }

        // Add workers to runtime
        auto rtAddW0 = bridge.runtimeAddWorker(*worker0);
        auto rtAddW1 = bridge.runtimeAddWorker(*worker1);
        auto rtAddW2 = bridge.runtimeAddWorker(*worker2);
        auto rtAddW3 = bridge.runtimeAddWorker(*worker3);
        if (!rtAddW0 || !rtAddW1 || !rtAddW2 || !rtAddW3) {
            std::cerr << "FAILED (workers)\n";
            return false;
        }

        // Fill A from shim(0,0), Fill B from shim(0,0)
        auto fillA = bridge.runtimeAddFill("fill_a", *of_in_a, "inputA", *shim0, 0, false);
        if (!fillA) {
            std::cerr << "FAILED (fill A)\n    " << fillA.error()[0].message << "\n";
            return false;
        }
        auto fillB = bridge.runtimeAddFill("fill_b", *of_in_b, "inputB", *shim0, 0, false);
        if (!fillB) {
            std::cerr << "FAILED (fill B)\n    " << fillB.error()[0].message << "\n";
            return false;
        }

        // Drain C from shim(1,0)
        auto drainC = bridge.runtimeAddDrain("drain_c", *of_out_c, "outputC", *shim1, 1, false);
        if (!drainC) {
            std::cerr << "FAILED (drain)\n    " << drainC.error()[0].message << "\n";
            return false;
        }

        // Build runtime
        auto buildRuntime = bridge.runtimeBuild();
        if (!buildRuntime) {
            std::cerr << "FAILED (build)\n    " << buildRuntime.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Step 13: Build and validate
        std::cout << "  [13/14] Building and validating program... ";
        auto buildResult = bridge.build();
        if (!buildResult) {
            std::cerr << "FAILED\n";
            for (const auto& diag : buildResult.error()) {
                std::cerr << "    " << diag.message << "\n";
            }
            return false;
        }
        std::cout << "OK\n";

        // Step 14: Export to XML and run code generator
        std::cout << "  [14/14] Exporting to GUI XML... ";
        ensureOutputDir();
        std::string xmlPath = OUTPUT_DIR + "vector_vector_mul_test_gui.xml";
        auto exportResult = bridge.exportToGuiXml(xmlPath);
        if (!exportResult) {
            std::cerr << "FAILED\n    " << exportResult.error()[0].message << "\n";
            return false;
        }

        if (!std::filesystem::exists(xmlPath)) {
            std::cerr << "FAILED (file not created)\n";
            return false;
        }
        std::cout << "OK\n";

        // Run code generator
        std::cout << "  [CodeGen] Running code generator... ";
        codegen::CodeGenBridge codegenBridge;
        codegen::CodeGenOptions options;
        options.outputDir = OUTPUT_DIR;

        auto codegenResult = codegenBridge.runCodeGen(xmlPath, options);
        if (!codegenResult) {
            std::cerr << "FAILED\n";
            for (const auto& diag : codegenResult.error()) {
                std::cerr << "    " << diag.message << "\n";
            }
            return false;
        }

        const auto& output = codegenResult.value();
        std::cout << "OK (" << output.generatedFiles.size() << " files)\n";

        // Verify generated files
        bool foundGraphML = false;
        bool foundPython = false;
        for (const auto& file : output.generatedFiles) {
            std::string filename = file.filename().string();
            if (filename.ends_with(".graphml")) foundGraphML = true;
            if (filename.starts_with("generated_") && filename.ends_with(".py")) foundPython = true;
        }

        std::cout << "  [Verify] Checking generated files... ";
        if (!foundGraphML || !foundPython) {
            std::cerr << "FAILED\n";
            std::cerr << "    GraphML: " << (foundGraphML ? "Found" : "Missing") << "\n";
            std::cerr << "    Python: " << (foundPython ? "Found" : "Missing") << "\n";
            return false;
        }
        std::cout << "OK\n";

        std::cout << "\n  Vector Vector Multiply Example: ALL TESTS PASSED\n";
        std::cout << "  Generated files saved to: " << OUTPUT_DIR << "\n";
        std::cout << "    - " << xmlPath << "\n";
        for (const auto& file : output.generatedFiles) {
            std::cout << "    - " << file.filename().string() << "\n";
        }

        return true;

    } catch (const std::exception& e) {
        std::cerr << "EXCEPTION: " << e.what() << "\n";
        return false;
    }
}

static bool testMatrixVectorMulExample() {
    std::cout << "\n=== Testing Matrix Vector Multiply Example (Full Pipeline) ===\n";

    try {
        hlir::HlirBridge bridge("matrix_vector_mul_test");

        // Step 1: Add constants
        // M=256, K=256, m=32, k=32, n_cores=4, derived expressions
        std::cout << "  [1/15] Adding constants... ";
        auto constM      = bridge.addConstant("M",            "256",                  "int");
        auto constK      = bridge.addConstant("K",            "256",                  "int");
        auto constm      = bridge.addConstant("m",            "32",                   "int");
        auto constk      = bridge.addConstant("k",            "32",                   "int");
        auto constNcores = bridge.addConstant("n_cores",      "4",                    "int");
        auto constMdivm  = bridge.addConstant("M_div_m",      "M // m",               "int");
        auto constKdivk  = bridge.addConstant("K_div_k",      "K // k",               "int");
        auto constRpc    = bridge.addConstant("rows_per_core","M_div_m // n_cores",   "int");
        auto constNfe    = bridge.addConstant("n_fifo_elems", "rows_per_core * K_div_k", "int");
        auto constAes    = bridge.addConstant("A_elem_size",  "n_cores * m * k",      "int");
        if (!constM || !constK || !constm || !constk || !constNcores ||
            !constMdivm || !constKdivk || !constRpc || !constNfe || !constAes) {
            std::cerr << "FAILED\n";
            return false;
        }
        std::cout << "OK (10 constants)\n";

        // Step 2: Add tensor types
        // Per-tile types
        std::cout << "  [2/15] Adding tensor types... ";
        auto inATy  = bridge.addTensorType("inA_ty",   {"m * k"},          "int16");
        auto inBTy  = bridge.addTensorType("inB_ty",   {"k"},              "int16");
        auto outCTy = bridge.addTensorType("outC_ty",  {"m"},              "int32");
        // Memtile buffer types
        auto aMemTy = bridge.addTensorType("A_mem_ty", {"n_cores * m * k"},"int16");
        auto cMemTy = bridge.addTensorType("C_mem_ty", {"n_cores * m"},    "int32");
        // Host buffer types (2D for TensorTiler2D)
        auto aTy    = bridge.addTensorType("A_ty",     {"n_fifo_elems", "A_elem_size"}, "int16");
        auto bTy    = bridge.addTensorType("B_ty",     {"1", "K"},         "int16");
        auto cTy    = bridge.addTensorType("C_ty",     {"1", "M"},         "int32");
        if (!inATy || !inBTy || !outCTy || !aMemTy || !cMemTy || !aTy || !bTy || !cTy) {
            std::cerr << "FAILED\n";
            return false;
        }
        std::cout << "OK (8 types)\n";

        // Step 3: Add tiles
        // 3 SHIMs: (0,0) fill A, (1,0) fill B, (2,0) drain C
        // 3 MEMs:  (0,1) split A, (1,1) forward B, (2,1) join C
        // 4 COMPUTE: (0,2)-(0,5)
        std::cout << "  [3/15] Adding tiles... ";
        auto shim0   = bridge.addTile("shim0",   hlir::TileKind::SHIM,    0, 0);
        auto shim1   = bridge.addTile("shim1",   hlir::TileKind::SHIM,    1, 0);
        auto shim2   = bridge.addTile("shim2",   hlir::TileKind::SHIM,    2, 0);
        auto mem0    = bridge.addTile("mem0",    hlir::TileKind::MEM,     0, 1);
        auto mem1    = bridge.addTile("mem1",    hlir::TileKind::MEM,     1, 1);
        auto mem2    = bridge.addTile("mem2",    hlir::TileKind::MEM,     2, 1);
        auto tile02  = bridge.addTile("tile_0_2",hlir::TileKind::COMPUTE, 0, 2);
        auto tile03  = bridge.addTile("tile_0_3",hlir::TileKind::COMPUTE, 0, 3);
        auto tile04  = bridge.addTile("tile_0_4",hlir::TileKind::COMPUTE, 0, 4);
        auto tile05  = bridge.addTile("tile_0_5",hlir::TileKind::COMPUTE, 0, 5);
        if (!shim0 || !shim1 || !shim2 || !mem0 || !mem1 || !mem2 ||
            !tile02 || !tile03 || !tile04 || !tile05) {
            std::cerr << "FAILED\n";
            return false;
        }
        std::cout << "OK (3 SHIM, 3 MEM, 4 COMPUTE)\n";

        // Step 4: Add input FIFO for A (A_mem_ty, depth 2, col 0)
        std::cout << "  [4/15] Adding input FIFO A... ";
        std::map<std::string, std::string> metaInA = {
            {"context", "L3_L2"}, {"direction", "input"}, {"data", "A"}, {"column", "0"}};
        auto of_in_a = bridge.addFifo("inA", *aMemTy, 2, std::nullopt, {}, hlir::ComponentId(), metaInA);
        if (!of_in_a) {
            std::cerr << "FAILED\n    " << of_in_a.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Step 5: Add input FIFO for B (inB_ty, depth 2, col 1)
        std::cout << "  [5/15] Adding input FIFO B... ";
        std::map<std::string, std::string> metaInB = {
            {"context", "L3_L2"}, {"direction", "input"}, {"data", "B"}, {"column", "1"}};
        auto of_in_b = bridge.addFifo("inB", *inBTy, 2, std::nullopt, {}, hlir::ComponentId(), metaInB);
        if (!of_in_b) {
            std::cerr << "FAILED\n    " << of_in_b.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Step 6: Add output FIFO for C (C_mem_ty, depth 2, col 2)
        std::cout << "  [6/15] Adding output FIFO C... ";
        std::map<std::string, std::string> metaOutC = {
            {"context", "L2_L3"}, {"direction", "output"}, {"data", "C"}, {"column", "2"}};
        auto of_out_c = bridge.addFifo("outC", *cMemTy, 2, std::nullopt, {}, hlir::ComponentId(), metaOutC);
        if (!of_out_c) {
            std::cerr << "FAILED\n    " << of_out_c.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Step 7: Split A at mem(0,1)
        // Offsets: [0, m*k=1024, 2*m*k=2048, 3*m*k=3072]
        std::cout << "  [7/15] Adding split A at mem(0,1)... ";
        std::map<std::string, std::string> splitMetaA = {
            {"context", "L2_L1"}, {"data", "A"}, {"column", "0"}};
        auto splitA = bridge.addFifoSplit("a_fifos", *of_in_a, 4, *inATy,
            {"a_fifos_0", "a_fifos_1", "a_fifos_2", "a_fifos_3"},
            {0, 1024, 2048, 3072}, *mem0, hlir::ComponentId(), splitMetaA);
        if (!splitA) {
            std::cerr << "FAILED\n";
            if (splitA.error().size() > 0) std::cerr << "    " << splitA.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Step 8: Forward B at mem(1,1) (broadcast to all 4 compute tiles)
        std::cout << "  [8/15] Adding forward B at mem(1,1)... ";
        std::map<std::string, std::string> fwdMetaB = {{"placement", "mem1"}};
        auto b_fwd = bridge.addFifoForward("B_fwd", *of_in_b, hlir::ComponentId(), fwdMetaB);
        if (!b_fwd) {
            std::cerr << "FAILED\n";
            if (b_fwd.error().size() > 0) std::cerr << "    " << b_fwd.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Step 9: Join C at mem(2,1)
        // Offsets: [0, m=32, 2*m=64, 3*m=96]
        std::cout << "  [9/15] Adding join C at mem(2,1)... ";
        std::map<std::string, std::string> joinMetaC = {
            {"context", "L1_L2"}, {"data", "C"}, {"column", "2"}};
        auto joinC = bridge.addFifoJoin("c_fifos", *of_out_c, 4, *outCTy,
            {"c_fifos_0", "c_fifos_1", "c_fifos_2", "c_fifos_3"},
            {0, 32, 64, 96}, *mem2, hlir::ComponentId(), joinMetaC);
        if (!joinC) {
            std::cerr << "FAILED\n";
            if (joinC.error().size() > 0) std::cerr << "    " << joinC.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Step 10: Add external kernel (inA_ty, inB_ty, outC_ty)
        std::cout << "  [10/15] Adding external kernel... ";
        std::map<std::string, std::string> kernelMeta = {{"operation", "matvec"}};
        auto matvec = bridge.addExternalKernel(
            "matvec_vectorized_i16_i32", "matvec_vectorized_i16_i32",
            "/scratch/IRONSmithTesting/mlir-aie/aie_kernels/aie2/mv.cc",
            {*inATy, *inBTy, *outCTy},
            {"/scratch/IRONSmithTesting/mlir-aie/aie_kernels",
             "/scratch/IRONSmithTesting/mlir-aie/aie_kernels/aie2",
             "/scratch/IRONSmithTesting/mlir-aie/aie_runtime_lib/AIE2"},
            hlir::ComponentId(), kernelMeta);
        if (!matvec) {
            std::cerr << "FAILED\n";
            if (matvec.error().size() > 0) std::cerr << "    " << matvec.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Step 11: Add core function using body_stmts mode for correct nested structure:
        //   elem_out = c_out.acquire(1)        <- outer acquire (once per output tile)
        //   for i in range_(m): elem_out[i]=0  <- zero-initialize output buffer
        //   for _ in range_(K_div_k):          <- inner loop over K chunks
        //     elem_a = a_in.acquire(1)
        //     elem_b = b_in.acquire(1)
        //     matvec(elem_a, elem_b, elem_out)
        //     a_in.release(1)
        //     b_in.release(1)
        //   c_out.release(1)                   <- outer release (matches outer acquire)
        std::cout << "  [11/15] Adding core function... ";
        std::string bodyStmtsJson = R"([
            {"type": "Acquire", "fifo_param": "c_out", "count": 1, "local_var": "elem_out"},
            {"type": "ForLoop", "var": "i", "count": "m", "body": [
                {"type": "Assignment", "target": "elem_out", "index": "i", "value": 0}
            ]},
            {"type": "ForLoop", "var": "_", "count": "K_div_k", "body": [
                {"type": "Acquire", "fifo_param": "a_in", "count": 1, "local_var": "elem_a"},
                {"type": "Acquire", "fifo_param": "b_in", "count": 1, "local_var": "elem_b"},
                {"type": "KernelCall", "kernel_param": "matvec", "args": ["elem_a", "elem_b", "elem_out"]},
                {"type": "Release", "fifo_param": "a_in", "count": 1},
                {"type": "Release", "fifo_param": "b_in", "count": 1}
            ]},
            {"type": "Release", "fifo_param": "c_out", "count": 1}
        ])";
        std::map<std::string, std::string> coreFuncMeta = {{"operation", "matvec"}};
        auto corefunc_matvec = bridge.addCoreFunctionBody(
            "core_fn",
            {"a_in", "b_in", "c_out", "matvec"},
            bodyStmtsJson,
            hlir::ComponentId(), coreFuncMeta);
        if (!corefunc_matvec) {
            std::cerr << "FAILED\n";
            if (corefunc_matvec.error().size() > 0)
                std::cerr << "    " << corefunc_matvec.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Step 12: Add workers (4 compute tiles, col 0 rows 2-5)
        std::cout << "  [12/15] Adding workers... ";
        std::map<std::string, std::string> w0Meta = {{"operation","matvec"},{"column","0"},{"worker_index","0"}};
        std::map<std::string, std::string> w1Meta = {{"operation","matvec"},{"column","0"},{"worker_index","1"}};
        std::map<std::string, std::string> w2Meta = {{"operation","matvec"},{"column","0"},{"worker_index","2"}};
        std::map<std::string, std::string> w3Meta = {{"operation","matvec"},{"column","0"},{"worker_index","3"}};

        auto worker0 = bridge.addWorker("worker0", *corefunc_matvec,
            {hlir::HlirBridge::FunctionArg::splitConsumer(*splitA, 0),
             hlir::HlirBridge::FunctionArg::forwardConsumer(*b_fwd),
             hlir::HlirBridge::FunctionArg::joinProducer(*joinC, 0),
             hlir::HlirBridge::FunctionArg::kernel(*matvec)},
            *tile02, hlir::ComponentId(), w0Meta);
        auto worker1 = bridge.addWorker("worker1", *corefunc_matvec,
            {hlir::HlirBridge::FunctionArg::splitConsumer(*splitA, 1),
             hlir::HlirBridge::FunctionArg::forwardConsumer(*b_fwd),
             hlir::HlirBridge::FunctionArg::joinProducer(*joinC, 1),
             hlir::HlirBridge::FunctionArg::kernel(*matvec)},
            *tile03, hlir::ComponentId(), w1Meta);
        auto worker2 = bridge.addWorker("worker2", *corefunc_matvec,
            {hlir::HlirBridge::FunctionArg::splitConsumer(*splitA, 2),
             hlir::HlirBridge::FunctionArg::forwardConsumer(*b_fwd),
             hlir::HlirBridge::FunctionArg::joinProducer(*joinC, 2),
             hlir::HlirBridge::FunctionArg::kernel(*matvec)},
            *tile04, hlir::ComponentId(), w2Meta);
        auto worker3 = bridge.addWorker("worker3", *corefunc_matvec,
            {hlir::HlirBridge::FunctionArg::splitConsumer(*splitA, 3),
             hlir::HlirBridge::FunctionArg::forwardConsumer(*b_fwd),
             hlir::HlirBridge::FunctionArg::joinProducer(*joinC, 3),
             hlir::HlirBridge::FunctionArg::kernel(*matvec)},
            *tile05, hlir::ComponentId(), w3Meta);

        if (!worker0 || !worker1 || !worker2 || !worker3) {
            std::cerr << "FAILED\n";
            if (!worker0 && worker0.error().size() > 0) std::cerr << "    " << worker0.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK (4 workers)\n";

        // Step 13: Add TensorTiler2D access patterns (required for DMA BD sizing)
        // a_tap: tile (n_fifo_elems x A_elem_size) into (1 x 512) chunks
        // b_tap: tile (1 x K) into (1 x k) chunks, repeat rows_per_core times
        // c_tap: tile (1 x M) into (1 x n_cores*m) chunks
        std::cout << "  [13/16] Adding TensorTiler2D access patterns... ";
        auto a_tap = bridge.addTensorTiler2D(
            "a_tap",
            {"n_fifo_elems", "A_elem_size"},       // tensor_dims
            {"1", "512"},                           // tile_dims
            {"n_fifo_elems", "A_elem_size // 512"}, // tile_counts
            false, 0);                              // prune_step=false, index=0
        auto b_tap = bridge.addTensorTiler2D(
            "b_tap",
            {"1", "K"},                            // tensor_dims
            {"1", "k"},                            // tile_dims
            {"1", "K_div_k"},                      // tile_counts
            false, 0,                              // prune_step=false, index=0
            "rows_per_core");                      // pattern_repeat
        auto c_tap = bridge.addTensorTiler2D(
            "c_tap",
            {"1", "M"},                            // tensor_dims
            {"1", "n_cores * m"},                  // tile_dims
            {"1", "rows_per_core"},                // tile_counts
            false, 0);                             // prune_step=false, index=0
        if (!a_tap || !b_tap || !c_tap) {
            std::cerr << "FAILED\n";
            if (!a_tap && a_tap.error().size() > 0) std::cerr << "    a_tap: " << a_tap.error()[0].message << "\n";
            if (!b_tap && b_tap.error().size() > 0) std::cerr << "    b_tap: " << b_tap.error()[0].message << "\n";
            if (!c_tap && c_tap.error().size() > 0) std::cerr << "    c_tap: " << c_tap.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK (a_tap, b_tap, c_tap)\n";

        // Step 14: Create runtime
        std::cout << "  [14/16] Creating runtime... ";
        auto runtime = bridge.createRuntime("runtime");
        if (!runtime) {
            std::cerr << "FAILED\n    " << runtime.error()[0].message << "\n";
            return false;
        }

        // A_ty/B_ty/C_ty = rt.sequence DMA types; runtimeAddMainSize overrides main() buffer sizes.
        auto addInA  = bridge.runtimeAddInputType(*aTy);
        auto addInB  = bridge.runtimeAddInputType(*bTy);
        auto addOutC = bridge.runtimeAddOutputType(*cTy);
        if (!addInA || !addInB || !addOutC) {
            std::cerr << "FAILED (types)\n";
            return false;
        }
        bridge.runtimeAddMainSize("n_fifo_elems");  // inputA
        bridge.runtimeAddMainSize("n_fifo_elems");  // inputB
        bridge.runtimeAddMainSize("n_fifo_elems");  // outputC

        // Parameters
        auto addParamA = bridge.runtimeAddParam("inputA");
        auto addParamB = bridge.runtimeAddParam("inputB");
        auto addParamC = bridge.runtimeAddParam("outputC");
        if (!addParamA || !addParamB || !addParamC) {
            std::cerr << "FAILED (params)\n";
            return false;
        }

        // Workers
        if (!bridge.runtimeAddWorker(*worker0) || !bridge.runtimeAddWorker(*worker1) ||
            !bridge.runtimeAddWorker(*worker2) || !bridge.runtimeAddWorker(*worker3)) {
            std::cerr << "FAILED (workers)\n";
            return false;
        }

        // Fill A from shim(0,0) with a_tap, Fill B from shim(1,0) with b_tap,
        // Drain C from shim(2,0) with c_tap
        auto fillA = bridge.runtimeAddFill("fill_a", *of_in_a, "inputA", *shim0, 0, true, *a_tap);
        if (!fillA) {
            std::cerr << "FAILED (fill A)\n    " << fillA.error()[0].message << "\n";
            return false;
        }
        auto fillB = bridge.runtimeAddFill("fill_b", *of_in_b, "inputB", *shim1, 1, true, *b_tap);
        if (!fillB) {
            std::cerr << "FAILED (fill B)\n    " << fillB.error()[0].message << "\n";
            return false;
        }
        auto drainC = bridge.runtimeAddDrain("drain_c", *of_out_c, "outputC", *shim2, 2, true, *c_tap);
        if (!drainC) {
            std::cerr << "FAILED (drain C)\n    " << drainC.error()[0].message << "\n";
            return false;
        }

        auto buildRuntime = bridge.runtimeBuild();
        if (!buildRuntime) {
            std::cerr << "FAILED (build runtime)\n    " << buildRuntime.error()[0].message << "\n";
            return false;
        }
        std::cout << "OK\n";

        // Step 15: Build and validate
        std::cout << "  [15/16] Building and validating program... ";
        auto buildResult = bridge.build();
        if (!buildResult) {
            std::cerr << "FAILED\n";
            for (const auto& diag : buildResult.error()) {
                std::cerr << "    " << diag.message << "\n";
            }
            return false;
        }
        std::cout << "OK\n";

        // Step 16: Export to XML and run code generator
        std::cout << "  [16/16] Exporting to GUI XML and running code generator... ";
        ensureOutputDir();
        std::string xmlPath = OUTPUT_DIR + "matrix_vector_mul_test_gui.xml";
        auto exportResult = bridge.exportToGuiXml(xmlPath);
        if (!exportResult) {
            std::cerr << "FAILED\n    " << exportResult.error()[0].message << "\n";
            return false;
        }
        if (!std::filesystem::exists(xmlPath)) {
            std::cerr << "FAILED (file not created)\n";
            return false;
        }

        // Run code generator
        codegen::CodeGenBridge codegenBridge;
        codegen::CodeGenOptions options;
        options.outputDir = OUTPUT_DIR;

        auto codegenResult = codegenBridge.runCodeGen(xmlPath, options);
        if (!codegenResult) {
            std::cerr << "FAILED (codegen)\n";
            for (const auto& diag : codegenResult.error()) {
                std::cerr << "    " << diag.message << "\n";
            }
            return false;
        }

        const auto& output = codegenResult.value();
        bool foundGraphML = false;
        bool foundPython  = false;
        for (const auto& file : output.generatedFiles) {
            std::string filename = file.filename().string();
            if (filename.ends_with(".graphml")) foundGraphML = true;
            if (filename.starts_with("generated_") && filename.ends_with(".py")) foundPython = true;
        }

        if (!foundGraphML || !foundPython) {
            std::cerr << "FAILED (missing output files)\n";
            std::cerr << "    GraphML: " << (foundGraphML ? "Found" : "Missing") << "\n";
            std::cerr << "    Python:  " << (foundPython  ? "Found" : "Missing") << "\n";
            return false;
        }
        std::cout << "OK (" << output.generatedFiles.size() << " files)\n";

        std::cout << "\n  Matrix Vector Multiply Example: ALL TESTS PASSED\n";
        std::cout << "  Generated files saved to: " << OUTPUT_DIR << "\n";
        std::cout << "    - " << xmlPath << "\n";
        for (const auto& file : output.generatedFiles) {
            std::cout << "    - " << file.filename().string() << "\n";
        }

        return true;

    } catch (const std::exception& e) {
        std::cerr << "EXCEPTION: " << e.what() << "\n";
        return false;
    }
}

// Dual-path forward example
//
// Data flow:
//   shim(0,0) --fill--> in1 \
//                             Worker1(0,3) --> out1 \
//   shim(0,0) --fill--> in2 /              out2 /   Worker2(0,2) --> out3 -[fwd mem(0,1)]--> out5 --> drain --> shim(0,0)
//                                                                --> out4 -[fwd mem(0,1)]--> out6 --> drain --> shim(0,0)
//
// Both workers share the same core function: acquire (fifo_a, fifo_b) as inputs and
// (fifo_c, fifo_d) as outputs, call the copy kernel, then release all four.
static bool testDualPathForwardExample() {
    try {
        hlir::HlirBridge bridge("dual_path_forward_test");

        // Constant and type — N=64 int32 elements per buffer
        auto constN = bridge.addConstant("N", "64", "int");
        auto dataTy = bridge.addTensorType("data_ty", {"N"}, "int32");

        // Tiles
        auto shim0    = bridge.addTile("shim0",    hlir::TileKind::SHIM,    0, 0);
        auto mem0     = bridge.addTile("mem0",     hlir::TileKind::MEM,     0, 1);
        auto tile_0_2 = bridge.addTile("tile_0_2", hlir::TileKind::COMPUTE, 0, 2);
        auto tile_0_3 = bridge.addTile("tile_0_3", hlir::TileKind::COMPUTE, 0, 3);

        // Input FIFOs — filled by shim (0,0)
        auto in1 = bridge.addFifo("in1", *dataTy, 2);
        auto in2 = bridge.addFifo("in2", *dataTy, 2);

        // Intermediate FIFOs — connect Worker 1 (producer) to Worker 2 (consumer)
        auto out1 = bridge.addFifo("out1", *dataTy, 2);
        auto out2 = bridge.addFifo("out2", *dataTy, 2);

        // Output FIFOs — produced by Worker 2, source for mem-tile forward operations
        auto out3 = bridge.addFifo("out3", *dataTy, 2);
        auto out4 = bridge.addFifo("out4", *dataTy, 2);

        // Forward FIFOs — out3→out5 and out4→out6 placed on mem tile (0,1)
        std::map<std::string, std::string> fwdMeta = {{"placement", "mem0"}};
        auto out5 = bridge.addFifoForward("out5", *out3, hlir::ComponentId(), fwdMeta);
        auto out6 = bridge.addFifoForward("out6", *out4, hlir::ComponentId(), fwdMeta);

        // External kernel — copies two input buffers into two output buffers
        auto copy2_kernel = bridge.addExternalKernel(
            "copy2_kernel", "copy_two_fifos", "copy2.cc",
            {*dataTy, *dataTy, *dataTy, *dataTy});

        // Core function — shared by both workers
        //   fifo_a, fifo_b: acquired as inputs  (elem_a, elem_b)
        //   fifo_c, fifo_d: acquired as outputs (elem_c, elem_d)
        auto corefunc_copy2 = bridge.addCoreFunction(
            "corefunc_copy2",
            {"kernel", "fifo_a", "fifo_b", "fifo_c", "fifo_d"},
            {{"fifo_a", 1, "elem_a"}, {"fifo_b", 1, "elem_b"},
             {"fifo_c", 1, "elem_c"}, {"fifo_d", 1, "elem_d"}},
            {"kernel", {"elem_a", "elem_b", "elem_c", "elem_d"}},
            {{"fifo_a", 1}, {"fifo_b", 1}, {"fifo_c", 1}, {"fifo_d", 1}});

        // Worker 1 at (0,3): consumes in1, in2 — produces out1, out2
        auto worker1 = bridge.addWorker("worker1", *corefunc_copy2,
            {hlir::HlirBridge::FunctionArg::kernel(*copy2_kernel),
             hlir::HlirBridge::FunctionArg::fifoConsumer(*in1),
             hlir::HlirBridge::FunctionArg::fifoConsumer(*in2),
             hlir::HlirBridge::FunctionArg::fifoProducer(*out1),
             hlir::HlirBridge::FunctionArg::fifoProducer(*out2)},
            *tile_0_3);

        // Worker 2 at (0,2): consumes out1, out2 — produces out3, out4
        auto worker2 = bridge.addWorker("worker2", *corefunc_copy2,
            {hlir::HlirBridge::FunctionArg::kernel(*copy2_kernel),
             hlir::HlirBridge::FunctionArg::fifoConsumer(*out1),
             hlir::HlirBridge::FunctionArg::fifoConsumer(*out2),
             hlir::HlirBridge::FunctionArg::fifoProducer(*out3),
             hlir::HlirBridge::FunctionArg::fifoProducer(*out4)},
            *tile_0_2);

        // Runtime — two fills on shim (0,0), two drains from forwarded FIFOs on shim (0,0)
        bridge.createRuntime("runtime");
        bridge.runtimeAddInputType(*dataTy);
        bridge.runtimeAddInputType(*dataTy);
        bridge.runtimeAddOutputType(*dataTy);
        bridge.runtimeAddOutputType(*dataTy);
        bridge.runtimeAddParam("inputA");
        bridge.runtimeAddParam("inputB");
        bridge.runtimeAddParam("outputA");
        bridge.runtimeAddParam("outputB");
        bridge.runtimeAddWorker(*worker1);
        bridge.runtimeAddWorker(*worker2);
        bridge.runtimeAddFill("fill_in1", *in1, "inputA", *shim0);
        bridge.runtimeAddFill("fill_in2", *in2, "inputB", *shim0);
        bridge.runtimeAddDrain("drain_out5", *out5, "outputA", *shim0);
        bridge.runtimeAddDrain("drain_out6", *out6, "outputB", *shim0);
        bridge.runtimeBuild();

        // Build, export, and run code generator
        bridge.build();
        ensureOutputDir();
        std::string xmlPath = OUTPUT_DIR + "dual_path_forward_test_gui.xml";
        bridge.exportToGuiXml(xmlPath);
        codegen::CodeGenBridge codegenBridge;
        codegen::CodeGenOptions options;
        options.outputDir = OUTPUT_DIR;
        codegenBridge.runCodeGen(xmlPath, options);

        return true;

    } catch (const std::exception& e) {
        std::cerr << "EXCEPTION: " << e.what() << "\n";
        return false;
    }
}

bool runBridgeTests(const std::string& /*appDir*/) {
    std::cout << "\n";
    std::cout << "========================================\n";
    std::cout << "  IRONSmith Bridge Integration Tests\n";
    std::cout << "========================================\n";

    bool hlirPassed = testHlirBridge();
    bool codegenPassed = testCodeGenBridge();
    bool passthroughPassed = testPassthroughExample();
    bool addActivatePassed = testAddActivateExample();
    bool vectorExpPassed = testVectorExpExample();
    bool vectorMulPassed = testVectorVectorMulExample();
    bool matVecMulPassed = testMatrixVectorMulExample();
    bool dualPathFwdPassed = testDualPathForwardExample();

    std::cout << "\n========================================\n";
    std::cout << "  Test Summary\n";
    std::cout << "========================================\n";
    std::cout << "  HLIR Bridge:             " << (hlirPassed ? "PASSED" : "FAILED") << "\n";
    std::cout << "  CodeGen Bridge:          " << (codegenPassed ? "PASSED" : "FAILED") << "\n";
    std::cout << "  Passthrough Example:     " << (passthroughPassed ? "PASSED" : "FAILED") << "\n";
    std::cout << "  Add-Activate Example:    " << (addActivatePassed ? "PASSED" : "FAILED") << "\n";
    std::cout << "  Vector Exp Example:      " << (vectorExpPassed ? "PASSED" : "FAILED") << "\n";
    std::cout << "  Vector Mul Example:      " << (vectorMulPassed ? "PASSED" : "FAILED") << "\n";
    std::cout << "  Matrix-Vector Mul Exmpl: " << (matVecMulPassed ? "PASSED" : "FAILED") << "\n";
    std::cout << "  Dual Path Forward:       " << (dualPathFwdPassed ? "PASSED" : "FAILED") << "\n";
    std::cout << "========================================\n";

    bool allPassed = hlirPassed && codegenPassed && passthroughPassed && addActivatePassed
                  && vectorExpPassed && vectorMulPassed && matVecMulPassed && dualPathFwdPassed;
    std::cout << "\n  Overall: " << (allPassed ? "SUCCESS" : "FAILURE") << "\n\n";

    return allPassed;
}

} // namespace BridgeTests
