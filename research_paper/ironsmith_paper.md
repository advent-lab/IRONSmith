# IRONSmith Paper

Fast Machine Learning For Science Conference 2026 Abstract
IRONSmith: A Visual Dataflow Design Environment for AMD Ryzen AI NPUs

Brock Sorenson, Samer Ali, Curt Bansil, Aman Arora — Arizona State University

Machine learning inference increasingly relies on specialized hardware accelerators for throughput and power efficiency. Neural Processing Units (NPUs), such as the AMD Ryzen AI NPU, offer significant ML advantages over CPUs and GPUs, but programming them requires deep expertise in specialized frameworks. The AMD Ryzen AI NPU uses IRON Python and mlir-aie, a tile-level spatial programming model requiring specification of inter-tile dataflow buffers (ObjectFIFOs), runtime data transfers, and compute tile kernel operations. Even experienced ML developers must invest substantial time in hardware documentation and programming guides before mapping a simple operation to the hardware. 

We present IRONSmith, the first visual dataflow design environment for NPU programming. IRONSmith provides an interactive canvas displaying the AMD AIE tile grid as visually connected blocks, allowing users to design ML dataflow applications by connecting tiles with wires representing ObjectFIFO buffers, split/join patterns, broadcast connections, and DDR transfers without writing code. Compute kernels are assigned from a pre-built library and worker kernels are configured through structured property panels. Structural design verification checks designs for disconnected dataflow, DMA channel limit violations, and kernel argument mismatches before code generation, providing actionable feedback that would otherwise only surface as cryptic compiler or runtime failures. IRONSmith's backend pipeline automatically translates the visual design into executable IRON Python handling structural completion, import resolution, and dependency management automatically. Generated code executes directly on the AMD Ryzen AI NPU. We demonstrate IRONSmith across ML designs of increasing complexity, from a single-tile vector passthrough to multi-tile matrix operations to a complete Multi-Layer Perceptron, all designed visually and successfully executed on the AMD Ryzen AI NPU. 

IRONSmith serves educators, students, ML researchers, and engineers by bridging the gap between ML knowledge and NPU programming expertise, widening access to hardware that is rapidly becoming standard across consumer and enterprise devices. By bringing visual design principles to NPU programming, IRONSmith lowers the barrier to entry in much the same way that graphical development environments broadened access to software development.

Extended Outline — IRONSmith: A Visual Dataflow Design Environment for AMD Ryzen AI NPUs
Introduction
Motivation and Problem Statement

Paragraph 1 — NPUs as the hardware frontier for ML inference:
Neural Processing Units (NPUs) have emerged as a critical hardware platform for efficient ML inference at the edge, offering significant advantages in throughput per watt over CPUs and GPUs. The AMD Ryzen AI NPU in particular, built on the XDNA AI Engine (AIE) architecture, provides a spatially distributed tile array of programmable compute cores designed specifically for the data-intensive, parallel workloads characteristic of modern ML models. As NPUs become ubiquitous in consumer laptops and embedded devices, the ability to program and optimize ML workloads directly on this hardware is increasingly important for researchers and practitioners.

Paragraph 2 — The programming barrier:
Despite this potential, programming the AMD Ryzen AI NPU requires deep expertise in IRON Python and the mlir-aie compiler framework — a specialized, tile-level programming model in which the developer manually specifies inter-tile FIFO buffers, runtime sequences, split/join dataflow patterns, and compute tile worker functions. This programming model, while powerful, is fundamentally different from conventional ML frameworks and requires significant ramp-up time even for experienced engineers. Before IRONSmith, a developer working with the Ryzen AI NPU had no choice but to work through hardware documentation, AMD programming guides, and hundreds of lines of specialized code before being able to map even a simple ML operation to the hardware.

Paragraph 3 — The missing tool:
No visual design tool existed for NPU programming prior to this work. The closest prior resource, AMD's Riallto exploration framework, provided Jupyter notebook-based tutorials but was discontinued and never offered a visual canvas, automated design verification, or a code generation pipeline. This gap means that even developers with strong ML and dataflow intuition — who can conceptually understand how data should flow through an NPU — cannot easily translate that understanding into a working program without first mastering a highly specialized language.

Our Goal

Paragraph 1:
We aim to empower students, educators, researchers, and ML practitioners to design and generate NPU-executable ML applications through a visual, interactive canvas without requiring any IRON Python expertise. By translating the abstract tile-level programming model into an intuitive graphical interface, IRONSmith makes NPU programming accessible to anyone with general ML and dataflow knowledge, dramatically lowering the time from concept to executable hardware design. IRONSmith is, to our knowledge, the first visual design environment specifically targeting AMD Ryzen AI NPU programming — unifying visual canvas design, automated structural verification, and IRON Python code generation in a single accessible pipeline.

Overview / Highlights

Paragraph 1 — The tool (with Figure 1: Annotated IRONSmith GUI Overview):
IRONSmith provides an interactive canvas displaying the AMD AIE tile grid — shim, memory, and compute tiles — as draggable, connectable blocks. Users wire tiles together with dataflow connections representing ObjectFifo buffers, split/join patterns, broadcast connections, and DDR transfers, and configure each component through properties panels and a symbol table, all without writing a single line of code. 

INSERT ANNOTATED IRONSmith GUI OVERVIEW HERE.

Paragraph 2 — The workflow:
IRONSmith sits at the intersection of visualization, programming, and hardware acceleration: a visual wire drawn between two tiles on the canvas becomes a configured ObjectFifo in generated IRON Python, which is physically mapped through DMA channels on the NPU hardware. The complete user journey — from a conceptual DNN architecture to a visual IRONSmith canvas design to a generated IRON Python file to confirmed NPU execution — requires no hardware programming expertise at any step the user directly interacts with. IRONSmith's architecture comprises a visual frontend (the interactive canvas, properties panels, and symbol table), an intermediate representation layer (the HLIR graph), and a backend AIECAD Compiler that lowers the design through a series of XML and semantic graph stages to produce complete, executable IRON Python.

INSERT DESIGN WORKFLOW FLOWCHART HERE.

Applications

Short paragraph:
IRONSmith targets several practical use cases: reducing the onboarding time for developers beginning work with the Ryzen AI NPU; serving as a teaching tool in NPU/ML architecture courses where students can design and execute real hardware-accelerated workloads without writing IRON Python; providing a visual demonstration platform for AMD customer presentations and workshops; and enabling ML practitioners with dataflow knowledge to rapidly prototype NPU-accelerated applications without months of specialized training.

Related Work
IRON, mlir-aie, AMD AI Engine

Paragraph 1 — mlir-aie and IRON as the target backend:
The mlir-aie project (Xilinx/AMD) provides the open-source compiler infrastructure and IRON Python API that IRONSmith generates code for. While mlir-aie is powerful and expressive, it is fundamentally a close-to-metal programming interface — the programmer manually controls tile placement, ObjectFifo sizing, DMA descriptors, and worker core functions, with errors surfacing only at compile or runtime. The AMD IRON tutorial (ISCA 2025) documents this programming model authoritatively but does not lower the programming barrier; it teaches it.

Paragraph 2 — Riallto as prior accessibility attempt:
AMD's Riallto framework was the closest prior effort to making Ryzen AI NPU programming more accessible, providing Jupyter notebook tutorials and Python-level exploration of the spatial programming model. However, Riallto was discontinued, never offered a visual design canvas or automated code generation, and still required users to write Python code interacting with the NPU API. IRONSmith addresses exactly the gap Riallto left — a visual, code-free design environment with a complete generation pipeline.

Visual and Graphical Programming for Programmable Hardware

Paragraph 1 — FPGA graphical programming as precedent:
The use of graphical programming to lower entry barriers for reconfigurable hardware has well-established precedent in the FPGA domain. Balid and Abdulwahed demonstrated that LabVIEW-based graphical dataflow programming reduced FPGA development lifecycles by up to 5× compared to textual HDL (EDUCON 2013), while Kuon et al. (arXiv 2014) and Winzker and Schwandt (EDUCON 2019) showed measurable learning outcome improvements for students using visual hardware design tools. Matlab Simulink provides a widely-used block-diagram design environment in which users connect functional blocks graphically and generate RTL that can be synthesized into an FPGA bitstream — a proven commercial instantiation of the visual-to-hardware design paradigm. IRONSmith applies this established philosophy to the NPU domain for the first time.

Paragraph 2 — Gap in NPU-targeted visual tools:
Existing visual hardware programming tools such as VisualApplets (Basler AG) and AMD Vivado Block Design target FPGA-class devices and produce bitstream or RTL outputs. For GPU programming, visual tools such as NVIDIA Nsight provide profiling and execution visualization for CUDA kernels, but no visual design entry environment exists for authoring GPU programs. AMD's Vitis unified software platform for Versal AI Core devices provides visualizations of how code maps onto the AI Engine array — the same AI Engine architecture underlying the Ryzen AI NPU — but requires users to write AI Engine kernels in C++ and does not provide a design entry canvas. No existing tool provides a visual design canvas for AMD Ryzen AI NPU programming. Ragan-Kelley et al.'s Halide (PLDI 2013) established the important principle of separating algorithm description from its lowering and scheduling, which IRONSmith follows by letting users express the visual dataflow design while the AIECAD pipeline handles all scope, ordering, import, and dependency management automatically.

INSERT COMPARISON TABLE HERE.

Background
AMD Ryzen AIE NPU Architecture

Paragraph 1 — Tile structure:
The AMD Ryzen AI NPU is built on the XDNA architecture, consisting of a spatial array of three tile types: Shim tiles interface with DDR memory through DMA engines and serve as the entry/exit points for all external data; Memory tiles provide shared on-chip buffer storage between rows; and Compute tiles (AI Engines) contain programmable VLIW processors that execute kernel operations on locally acquired data. The AIE2 variant (used in AMD Phoenix devices) contains 32 compute tiles arranged in a 5×5 grid with 4 shim tiles, while AIE1 contains 16 compute tiles.

Paragraph 2 — Programming constraints:
Each tile interacts with adjacent tiles exclusively through statically configured DMA channels and ObjectFifo buffers — producer-consumer buffers that carry typed, statically sized data between tiles through DMA. DMA channel limits per tile and the static production/consumption rates of all ObjectFifos must be respected at design time, making structural correctness a prerequisite that IRONSmith's design verification layer directly enforces before code generation.

IRON Python and mlir-aie Programming

Paragraph 1 — ObjectFifo and runtime sequence:
IRON Python exposes the NPU dataflow model primarily through the ObjectFifo abstraction — a statically typed, statically sized producer-consumer buffer that moves data between tiles through DMA channels. The runtime sequence defines the host-side data movement: fill operations load input tensors from DDR into shim FIFOs, and drain operations collect output tensors back to DDR, with the sizes, types, and transfer patterns all manually specified. Writing a correct runtime sequence requires precise knowledge of how data is tiled and partitioned across the tile grid.

Paragraph 2 — Worker functions and complexity:
Compute tiles execute Worker functions that repeatedly acquire buffers from input FIFOs, call kernel functions on the acquired data, and release the output buffers — the acquire/call/release loop must be manually written and its argument types must exactly match the ObjectFifo element types. Even at this relatively high level of abstraction, a moderately complex design such as a matrix-multiply with broadcast weights requires managing dozens of interdependent ObjectFifos, split/join hubs, and worker configurations, all of which must be manually specified, scoped, and ordered in the generated Python file.

Dataflow Programming Difficulty

Paragraph 1:
The fundamental challenge is that IRON Python expresses an inherently visual, spatial programming model — a graph of tiles, FIFOs, and data movements — as a sequential text file, requiring the programmer to mentally track tile connectivity, buffer depths, and dataflow dependencies across hundreds of lines of code. Structural errors such as disconnected FIFOs, DMA channel oversubscription, or kernel argument mismatches produce cryptic compiler errors that are difficult to diagnose without a visual representation of the design. IRONSmith eliminates this mismatch by making the spatial programming model directly visual, and catches the most common structural errors before code generation.

Proposal
IRONSmith Architecture

Paragraph 1 (Figure — High-Level Architecture Diagram):
IRONSmith is built on a microkernel plugin architecture in which a Core System manages the application lifecycle and UI frame while modular plugins — Canvas, Project Explorer, Code Editor, and NPU Bridge — contribute functionality through a central ExtensionSystem service registry. This design makes IRONSmith stable and extensible: new visual features or backend integrations can be added as plugins without modifying the core, and the ExtensionSystem's GraphExtender and CodeGeneratorExtender interfaces allow new dataflow patterns to be supported by adding a single class to each. 

INSERT ARCHITECTURE DIAGRAM HERE.

Canvas and Visual Design Layer

Paragraph 1 — Tile grid and wiring:
The IRONSmith canvas presents the AMD AIE tile grid as an interactive visual layout in which users connect tiles with typed wires representing the full set of IRON dataflow patterns: ObjectFifo for standard tile-to-tile connections, Split and Join for sub-FIFO access patterns through memory tiles, Broadcast for sharing a single FIFO across multiple consumers, Forward for passing data through a tile without computation, and Distribute/Collect for streaming inputs and outputs across multiple shim tiles simultaneously. Wiring is performed by clicking and dragging between tile ports, translating the spatial programming model into a direct visual action that mirrors how an engineer would draw the design on a whiteboard.

Paragraph 2 — Properties Panel and Symbol Table:
Each visual component is configurable through a context-sensitive Properties Panel that exposes the underlying IRON parameters without requiring the user to write code: FIFO parameters (name, depth, data type, dimensions), DDR input/output sizes, runtime Tensor Access Pattern assignment, kernel assignments per compute tile, and Worker core body statements (acquire/kernel call/release sequences) are all set through structured form fields. The Symbol Table provides a design-wide registry for named constants, type abstractions, and tensor manipulation so that values defined once can be referenced throughout the design and consistently propagated into the generated code.

Paragraph 3 — Kernel Library and persistence:
IRONSmith includes a searchable pre-built kernel library sourced from the mlir-aie repository, allowing users to browse available ML kernels (GEMM, ReLU, CONV2D, Softmax, etc.), preview their source in the in-app code editor, and assign up to four kernels to a compute tile by selecting them from the library. Designs are automatically persisted to a .ironsmith bundle on every change, storing the complete visual layout in a document.json file, allowing designs to be closed and reopened across sessions without any data loss.

Design Verification

Paragraph 1 — Structural sanity checks:
Before generating code, IRONSmith runs a set of structural verification checks on the GUI design that catch the most common classes of errors encountered in direct IRON Python programming: invalid runtime I/O sequence configuration, disconnected dataflow (wires with no source or destination), DMA channel limit violations per tile, and kernel argument count or dimension incompatibilities. These checks provide the user with immediate, actionable error messages in the output log — feedback that would otherwise only appear as cryptic compiler or runtime failures after a full synthesis pass.

Note: IRONSmith verifies structural correctness of the GUI design; correctness of the generated IRON Python code is validated by the IRON compiler and runtime. Additional GUI-level semantic checks are identified as future work.

Code Generation and Design Lowering Pipeline

Paragraph 1 (Figure — Pipeline overview):
The AIECAD Compiler translates the persisted document.json into executable IRON Python through a series of intermediate representations that progressively complete, verify, and structure the design. The pipeline automatically resolves interdependencies between components, infers missing values, determines the correct import set, enforces IRON coding conventions, and produces a semantically directed graph from which the final code is generated — handling in software the expert knowledge that a human programmer would otherwise need to apply manually when writing IRON Python directly. 

INSERT DESIGN LOWERING PIPELINE FIGURE HERE.

Demonstration (Methodology and Results)
Evaluation Criteria

Paragraph 1:
We evaluate IRONSmith along three dimensions: correctness, expressiveness, and productivity. Correctness is demonstrated by executing the generated IRON Python for each benchmark directly on the AMD Ryzen AI NPU and confirming successful completion. Expressiveness is demonstrated through the progression of the benchmark suite — showing that IRONSmith covers the full range of IRON dataflow patterns from trivial single-tile pipelines to complex multi-tile matrix operations. Productivity is demonstrated through a code comparison of IRONSmith-generated IRON Python against equivalent hand-written reference code, showing that the pipeline automatically handles the structural boilerplate that would otherwise require manual expert effort.

Benchmark Suite

Paragraph 1 — Progressive benchmark rationale:
The benchmark suite consists of seven designs of increasing complexity, each introducing one or two new IRONSmith features so that the progression forms a natural coverage of the IRON programming model. Benchmarks range from Passthrough — the simplest possible ObjectFifo pipeline with no computation — to GEMM, which uses all major IRONSmith features including 16 AIE tiles, TensorAccessPatterns, custom core body functions, and accumulate-in-place patterns. All seven designs were created entirely through the IRONSmith canvas without writing any code, and their generated IRON Python executed successfully on the AMD Ryzen AI NPU.

(Benchmark descriptions as in outline — one sentence per design with feature tags)

INSERT BENCHMARK PROGRESSION TABLE HERE.

INSERT CODE COMPARISON TABLE HERE.

MLP Neural Network — Complex Use Case

Paragraph 1 — MLP as the capstone demonstration:
To demonstrate IRONSmith on a complete, meaningful ML application, we design a four-layer Multi-Layer Perceptron neural network entirely through the IRONSmith canvas — three hidden layers applying a 64×64 matrix-multiply with ReLU activation and a final output layer with matrix-multiply and Softmax, mapped across 16 AIE compute tiles. Weight matrices of 4,096 BFloat16 elements (64×64) are broadcast to each of the four tile columns, and intermediate activation tensors of the same shape are forwarded between adjacent layer columns. The MLP combines every feature demonstrated across the benchmark suite in a single cohesive design and additionally introduces tensor manipulation (dims_from_stream / dims_to_stream) for reshaping activation tensors between layers — required for correct data layout across the multi-column tile structure.

Paragraph 2 — Design process and user experience:
A user designing the MLP in IRONSmith begins from a standard MLP architecture diagram — a conceptual illustration of layers, weights, and activations — and maps it directly to the canvas by connecting tile columns, assigning matmul and activation kernels, configuring worker core bodies through the properties panel, and setting up the DDR runtime sequence. No IRON Python knowledge is required at any step; the design requires only an understanding of the MLP's dataflow and the compute operation at each layer. 

INSERT MLP CANVAS SCREENSHOT HERE.

The MLP canvas (Figure X) shows four tile columns arranged left to right, one per layer. Each column contains four AIE compute tiles stacked vertically, executing the layer's matmul and activation kernels in parallel across split activation sub-tensors. Weight matrices enter each column through broadcast connections — a single DDR input FIFO fanning out to all four tiles in the column simultaneously, represented on the canvas as a broadcast hub with four branch arms. Intermediate activations travel between adjacent layer columns via ForwardFifo connections, visible as horizontal wires linking the output ports of one column to the input ports of the next. Split hubs at the memory tile boundary partition the input activation tensor across the four tile rows; the final layer's results are collected back to DDR through a single output drain. The complete dataflow from DDR input to DDR output is visible as a connected wire diagram on the canvas.

Paragraph 3 — Verification and execution results:
The completed MLP design passes all IRONSmith structural verification checks — confirming correct runtime I/O sequence, connected dataflow, DMA channel compliance, and kernel argument compatibility — and produces a design summary reporting 16 AIE tiles, 30 FIFOs, 4 broadcasts, and 5 runtime fills. Code generation completes successfully through all AIECAD pipeline stages, and the generated IRON Python executes correctly on the AMD Ryzen AI NPU, validating the tool across all three evaluation dimensions: correctness (hardware execution passes), expressiveness (every IRON dataflow pattern appears in a single design), and productivity (the complete MLP was designed without writing any code).

INSERT VERIFICATION LOG SCREENSHOT HERE. 
INSERT CODE GENERATION LOG 
INSERT GENERATED CODE SNIPPET HERE.

Conclusion
Paragraph 1 — Summary:
IRONSmith is the first visual design environment for AMD Ryzen AI NPUs, providing a complete pipeline from graphical canvas dataflow design to automatically generated, directly executable IRON Python — combining visualization, programming, and hardware acceleration in a single tool that requires no specialized hardware programming knowledge from its users.

Paragraph 2 — Results summary:
We demonstrated IRONSmith across seven benchmark ML designs of increasing complexity, all successfully generated and executed on the AMD Ryzen AI NPU, and further validated the tool on a complete Multi-Layer Perceptron neural network inference application. Generated code was shown to be functionally equivalent to hand-written IRON Python while offloading all structural, scoping, and dependency management tasks from the user.

Paragraph 3 — Key insights:
The spatial, tile-based NPU programming model maps naturally to a graphical dataflow canvas — ObjectFifo connections, split/join hubs, and broadcast patterns translate directly to visual wires in a way that makes the programming model intuitive to practitioners familiar with ML dataflow graphs. Automated structural verification meaningfully lowers the risk of hardware misuse for users unfamiliar with DMA channel constraints and kernel interface requirements, providing the kind of immediate design-time feedback that previously only appeared as compile or runtime failures.

Paragraph 4 — Future work:
Future work includes golden reference validation (comparing NPU output against CPU reference for correctness verification), in-app performance feedback with execution profiling overlaid on the canvas, expanded support for complex pipeline designs including image processing and transformer attention blocks, and a quantitative user study measuring onboarding time and design correctness rates against direct IRON Python programming.

Paragraph 5 — Resources:
IRONSmith is open source under the GNU General Public License v3.0. The source code is available at https://github.com/advent-lab/IRONSmith. A user guide covering installation, canvas walkthrough, and all design patterns is included in the repository documentation. A live demo video is available at [INSERT DEMO VIDEO LINK].



