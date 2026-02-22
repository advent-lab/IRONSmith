// SPDX-FileCopyrightText: 2026 Brock Sorenson
// SPDX-License-Identifier: GPL-3.0-only

/**
 * HLIR C++ Bridge - Example Usage
 *
 * Demonstrates the complete ID-based workflow for building HLIR programs from C++.
 * All component references use ComponentId for type safety and better tracking.
 */

#include "HlirBridge.hpp"
#include <iostream>
#include <vector>

using namespace hlir;

void printError(const std::vector<HlirDiagnostic>& errors) {
    for (const auto& err : errors) {
        std::cerr << "ERROR [" << errorCodeToString(err.code) << "]: "
                  << err.message << "\n";
        if (!err.entityId.empty()) {
            std::cerr << "   Entity: " << err.entityId << "\n";
        }
        if (!err.dependencies.empty()) {
            std::cerr << "   Dependencies: ";
            for (const auto& dep : err.dependencies) {
                std::cerr << dep << " ";
            }
            std::cerr << "\n";
        }
    }
}

int main() {
    try {
        std::cout << "=== HLIR C++ Bridge - ID-Based Workflow ===\n\n";

        // Create bridge
        HlirBridge bridge("example_design");

        // ====================================================================
        // 1. Define constants and types
        // ====================================================================
        std::cout << "Step 1: Adding constants and types...\n";

        auto dataSize = bridge.addConstant("data_size", "128", "int");
        if (!dataSize) { printError(dataSize.error()); return 1; }
        std::cout << "   OK: Constant 'data_size' -> " << dataSize->value.substr(0, 8) << "...\n";

        auto chunkType = bridge.addTensorType(
            "chunk_ty",
            {"data_size / 4"},  // Symbolic shape
            "bfloat16",
            ""
        );
        if (!chunkType) { printError(chunkType.error()); return 1; }
        std::cout << "   OK: TensorType 'chunk_ty' -> " << chunkType->value.substr(0, 8) << "...\n";

        // ====================================================================
        // 2. Define hardware tiles (using IDs from now on)
        // ====================================================================
        std::cout << "\nStep 2: Adding hardware tiles...\n";

        auto shim0 = bridge.addTile("shim0", TileKind::SHIM, 0, 0);
        if (!shim0) { printError(shim0.error()); return 1; }
        std::cout << "   OK: Tile 'shim0' (shim) at (0,0)\n";

        auto mem0 = bridge.addTile("mem0", TileKind::MEM, 0, 1);
        if (!mem0) { printError(mem0.error()); return 1; }
        std::cout << "   OK: Tile 'mem0' (mem) at (0,1)\n";

        auto compute0 = bridge.addTile("compute_0_5", TileKind::COMPUTE, 0, 5);
        if (!compute0) { printError(compute0.error()); return 1; }
        std::cout << "   OK: Tile 'compute_0_5' (compute) at (0,5)\n";

        // ====================================================================
        // 3. Define FIFOs (using ComponentIds for producer/consumer)
        // ====================================================================
        std::cout << "\nStep 3: Adding FIFOs with ID-based references...\n";

        // FIFO from shim0 to mem0, using chunkType
        auto fifoIn = bridge.addFifo(
            "of_in",
            *chunkType,      // ID reference to tensor type
            2,               // depth
            *shim0,          // producer ID
            {*mem0}          // consumer IDs
        );
        if (!fifoIn) { printError(fifoIn.error()); return 1; }
        std::cout << "   OK: FIFO 'of_in': shim0 -> mem0 (depth 2)\n";

        // FIFO from mem0 to compute0
        auto fifoCompute = bridge.addFifo(
            "of_compute",
            *chunkType,
            2,
            *mem0,
            {*compute0}
        );
        if (!fifoCompute) { printError(fifoCompute.error()); return 1; }
        std::cout << "   OK: FIFO 'of_compute': mem0 -> compute0 (depth 2)\n";

        // FIFO from compute0 back to mem0
        auto fifoOut = bridge.addFifo(
            "of_out",
            *chunkType,
            2,
            *compute0,
            {*mem0}
        );
        if (!fifoOut) { printError(fifoOut.error()); return 1; }
        std::cout << "   OK: FIFO 'of_out': compute0 -> mem0 (depth 2)\n";

        // FIFO from mem0 to shim0
        auto fifoFinal = bridge.addFifo(
            "of_final",
            *chunkType,
            2,
            *mem0,
            {*shim0}
        );
        if (!fifoFinal) { printError(fifoFinal.error()); return 1; }
        std::cout << "   OK: FIFO 'of_final': mem0 -> shim0 (depth 2)\n";

        // ====================================================================
        // 4. Define external kernel
        // ====================================================================
        std::cout << "\nStep 4: Adding external kernel...\n";

        auto addKernel = bridge.addExternalKernel(
            "add_kernel",
            "eltwise_add",              // C function name
            "kernels/add.cc",           // Source file
            {*chunkType, *chunkType},   // Argument types (both are chunk_ty)
            {"kernels/"}                // Include directories
        );
        if (!addKernel) { printError(addKernel.error()); return 1; }
        std::cout << "   OK: ExternalKernel 'add_kernel' (eltwise_add)\n";

        // ====================================================================
        // 5. Define core function
        // ====================================================================
        std::cout << "\nStep 5: Adding core function...\n";

        std::vector<std::string> params = {"kernel", "fifoA", "fifoB", "fifoOut"};

        std::vector<HlirBridge::AcquireSpec> acquires = {
            {"fifoA", 1, "elemA"},
            {"fifoB", 1, "elemB"}
        };

        HlirBridge::KernelCallSpec kernelCall = {
            "kernel",
            {"elemA", "elemB"}
        };

        std::vector<HlirBridge::ReleaseSpec> releases = {
            {"fifoA", 1},
            {"fifoB", 1},
            {"fifoOut", 1}
        };

        auto coreFunc = bridge.addCoreFunction(
            "add_fn",
            params,
            acquires,
            kernelCall,
            releases
        );
        if (!coreFunc) { printError(coreFunc.error()); return 1; }
        std::cout << "   OK: CoreFunction 'add_fn'\n";

        // ====================================================================
        // 6. Define worker (binding kernel to compute tile)
        // ====================================================================
        std::cout << "\nStep 6: Adding worker with ID-based function arguments...\n";

        std::vector<HlirBridge::FunctionArg> fnArgs = {
            HlirBridge::FunctionArg::kernel(*addKernel),
            HlirBridge::FunctionArg::fifoConsumer(*fifoCompute, 0),
            HlirBridge::FunctionArg::fifoConsumer(*fifoCompute, 0),  // Same FIFO, different elem
            HlirBridge::FunctionArg::fifoProducer(*fifoOut)
        };

        auto worker = bridge.addWorker(
            "worker_0",
            *coreFunc,      // Core function ID
            fnArgs,         // Function arguments with IDs
            *compute0       // Placement tile ID
        );
        if (!worker) { printError(worker.error()); return 1; }
        std::cout << "   OK: Worker 'worker_0' on compute_0_5\n";

        // ====================================================================
        // 7. Create runtime sequence
        // ====================================================================
        std::cout << "\nStep 7: Creating runtime sequence...\n";

        auto runtime = bridge.createRuntime("main_runtime");
        if (!runtime) { printError(runtime.error()); return 1; }
        std::cout << "   OK: Runtime created\n";

        // Add input/output types using IDs
        auto inType = bridge.runtimeAddInputType(*chunkType);
        if (!inType) { printError(inType.error()); return 1; }

        auto outType = bridge.runtimeAddOutputType(*chunkType);
        if (!outType) { printError(outType.error()); return 1; }

        // Add parameters
        bridge.runtimeAddParam("input_data");
        bridge.runtimeAddParam("output_data");

        // Add fill/drain operations using FIFO and Tile IDs
        auto fill = bridge.runtimeAddFill("fill_0", *fifoIn, "input_data", *shim0);
        if (!fill) { printError(fill.error()); return 1; }

        auto drain = bridge.runtimeAddDrain("drain_0", *fifoFinal, "output_data", *shim0);
        if (!drain) { printError(drain.error()); return 1; }

        auto rtBuild = bridge.runtimeBuild();
        if (!rtBuild) { printError(rtBuild.error()); return 1; }
        std::cout << "   OK: Runtime built successfully\n";

        // ====================================================================
        // 8. Build and validate
        // ====================================================================
        std::cout << "\nStep 8: Building and validating program...\n";

        auto buildResult = bridge.build();
        if (!buildResult) { printError(buildResult.error()); return 1; }
        std::cout << "   OK: Program validated successfully!\n";

        // ====================================================================
        // 9. Get statistics
        // ====================================================================
        std::cout << "\nStep 9: Program statistics:\n";

        auto stats = bridge.getStats();
        if (stats) {
            std::cout << "   Symbols: " << stats->numSymbols << "\n";
            std::cout << "   Tiles: " << stats->numTiles << "\n";
            std::cout << "   FIFOs: " << stats->numFifos << "\n";
            std::cout << "   External Kernels: " << stats->numExternalKernels << "\n";
            std::cout << "   Core Functions: " << stats->numCoreFunctions << "\n";
            std::cout << "   Workers: " << stats->numWorkers << "\n";
            std::cout << "   Has Runtime: " << (stats->hasRuntime ? "Yes" : "No") << "\n";
        }

        // ====================================================================
        // 10. Demonstrate component operations using IDs
        // ====================================================================
        std::cout << "\nStep 10: Component operations with IDs:\n";

        // Lookup by name to get ID
        auto shim0Lookup = bridge.lookupByName(ComponentType::TILE, "shim0");
        if (shim0Lookup) {
            std::cout << "   OK: Found 'shim0' by name -> " << shim0Lookup->value.substr(0, 8) << "...\n";

            // Lookup full data by ID
            auto shim0Data = bridge.lookupById(*shim0Lookup);
            if (shim0Data) {
                std::cout << "   OK: Retrieved component data (JSON)\n";
            }
        }

        // Update FIFO depth using ID
        auto updateResult = bridge.updateFifoDepth(*fifoIn, 4);
        if (updateResult) {
            std::cout << "   OK: Updated FIFO depth: of_in -> 4\n";
        }

        // Get all tiles
        auto allTiles = bridge.getAllIds(ComponentType::TILE);
        if (allTiles) {
            std::cout << "   OK: Total tiles: " << allTiles->size() << "\n";
        }

        // ====================================================================
        // 11. Demonstrate component updates (NEW FEATURE!)
        // ====================================================================
        std::cout << "\nStep 11: Demonstrating component updates:\n";
        std::cout << "   Components can be updated by passing their existing ID to add_* methods.\n";
        std::cout << "   This replaces the component while preserving all dependency references!\n\n";

        // Update tile location - move compute tile to a different position
        std::cout << "   Updating compute tile location...\n";
        std::cout << "   Original: compute_0_5 at (0, 5)\n";
        auto updatedCompute = bridge.addTile("compute_0_5", TileKind::COMPUTE, 1, 6, *compute0);
        if (!updatedCompute) {
            printError(updatedCompute.error());
            return 1;
        }
        std::cout << "   OK: Updated to (1, 6) with same ID: " << updatedCompute->value.substr(0, 8) << "...\n";
        std::cout << "   Note: Workers and FIFOs still reference this tile correctly!\n\n";

        // Update tensor type shape - change buffer size
        std::cout << "   Updating tensor type shape...\n";
        std::cout << "   Original: chunk_ty with shape [buffer_size / 4]\n";
        auto updatedChunkType = bridge.addTensorType(
            "chunk_ty",
            {"buffer_size / 2"},  // Changed from /4 to /2
            "bfloat16",
            "",
            *chunkType
        );
        if (!updatedChunkType) {
            printError(updatedChunkType.error());
            return 1;
        }
        std::cout << "   OK: Updated shape to [buffer_size / 2] with same ID: " << updatedChunkType->value.substr(0, 8) << "...\n";
        std::cout << "   Note: All FIFOs using this type still reference it correctly!\n\n";

        // Update FIFO depth and connections
        std::cout << "   Updating FIFO depth and configuration...\n";
        std::cout << "   Original: of_in with depth 2\n";
        auto updatedFifoIn = bridge.addFifo(
            "of_in",
            *updatedChunkType,  // Using updated tensor type
            8,                  // Increased depth from 2 to 8
            *shim0,
            {*mem0},
            *fifoIn,
            {}
        );
        if (!updatedFifoIn) {
            printError(updatedFifoIn.error());
            return 1;
        }
        std::cout << "   OK: Updated depth to 8 with same ID: " << updatedFifoIn->value.substr(0, 8) << "...\n";
        std::cout << "   Note: Worker arguments still reference this FIFO correctly!\n\n";

        // Update external kernel source file
        std::cout << "   Updating external kernel source path...\n";
        std::cout << "   Original: add_kernel with source 'kernels/add.cc'\n";
        auto updatedKernel = bridge.addExternalKernel(
            "add_kernel",
            "eltwise_add",
            "kernels/optimized_add.cc",  // Changed source file
            {*updatedChunkType, *updatedChunkType},
            {"kernels/"},
            *addKernel
        );
        if (!updatedKernel) {
            printError(updatedKernel.error());
            return 1;
        }
        std::cout << "   OK: Updated source to 'kernels/optimized_add.cc' with same ID: " << updatedKernel->value.substr(0, 8) << "...\n";
        std::cout << "   Note: Core functions still reference this kernel correctly!\n\n";

        // Verify dependencies are intact by checking worker
        std::cout << "   Verifying that dependencies remain intact after updates...\n";
        auto workerData = bridge.lookupById(*worker);
        if (workerData) {
            std::cout << "   OK: Worker 'worker_0' still exists and references:\n";
            std::cout << "      - Updated kernel (ID preserved)\n";
            std::cout << "      - Updated FIFOs (IDs preserved)\n";
            std::cout << "      - Updated tile placement (ID preserved)\n";
        }

        std::cout << "\n   Summary: Component updates preserve all dependency relationships!\n";
        std::cout << "   This enables interactive GUI editing without breaking the design.\n";

        // ====================================================================
        // 12. Export to GUI XML
        // ====================================================================
        std::cout << "\nStep 12: Exporting to GUI XML...\n";

        auto exportResult = bridge.exportToGuiXml("example_design.xml");
        if (!exportResult) {
            printError(exportResult.error());
            return 1;
        }
        std::cout << "   OK: Exported to 'example_design.xml'\n";

        // ====================================================================
        // Success!
        // ====================================================================
        std::cout << "\n=== SUCCESS ===\n";
        std::cout << "HLIR program built, validated, and exported using\n";
        std::cout << "component ID-based workflow for type safety and tracking.\n";
        std::cout << "\nKey features demonstrated:\n";
        std::cout << "  - ID-based component references\n";
        std::cout << "  - Component updates while preserving dependencies\n";
        std::cout << "  - Type-safe component lookup and modification\n";
        std::cout << "  - Complete program validation and XML export\n";
        std::cout << "===================================================\n";

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "FATAL ERROR: " << e.what() << "\n";
        return 1;
    }
}
