# IRONSmith

**IRONSmith** is an interactive **GUI-based design application** that allows developers, students, and performance engineers to visually construct and simulate **AMD AI Engine (AIE) / Ryzen AI NPU** programs using the [IRON](https://github.com/Xilinx/mlir-aie) framework.

![IRONSmith](misc/IRONSmith_Canvas.png)

---

### Code Editing Area
![IRONSmith](misc/IRONSmith_CodeEditor.png)

---

### Kernels Catalog With Preview
![IRONSmith](misc/IRONSmith_KernelsCatalog.png)

---

To compile and run, select a build profile from the CMake presets.

## Build presets

Available `cmake --build` presets:

- `build-dev-debug`
- `build-dev-release`
- `build-dev-asan`

Each build preset uses a matching configure preset:

- `build-dev-debug`  → `dev-debug`
- `build-dev-release` → `dev-release`
- `build-dev-asan`   → `dev-asan`

## Configure, build, and run

### Debug build

```bash
# From the project root
cmake --preset dev-debug
cmake --build --preset build-dev-debug

# Run (example)
./out/build/dev-debug/bin/ironsmith
```
