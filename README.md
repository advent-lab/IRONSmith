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

## Requirements

To configure and build IRONSmith, install the following:

- **CMake 3.27 or newer**
- **Ninja**
- **Python 3**
- **Qt 6**
- **QScintilla**
- A Unix-like C++ toolchain such as **GCC**, **Clang**, **Apple Clang**, or **WSL**
- **GoogleTest / GMock** if you are using presets with tests enabled

## Installing Dependencies

### macOS (Homebrew)

```bash
brew install cmake ninja python qt qscintilla2 googletest
```

### Ubuntu / WSL

```bash
sudo apt update
sudo apt install build-essential clang cmake ninja-build python3 python3-venv qt6-base-dev libqscintilla2-qt6-dev libgtest-dev libgmock-dev
```

## Python Virtual Environment

It is recommended to create a local Python virtual environment in the project root:

```bash
python3 -m venv venv
source venv/bin/activate
```

## Local CMake Configuration

If CMake fails to find **Qt 6** or **Python 3**, define `Qt6_DIR` and `Python3_EXECUTABLE` in `CMakeUserPresets.json`.

Example:

```json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "dev-debug-local",
      "inherits": "dev-debug",
      "cacheVariables": {
        "Python3_EXECUTABLE": "/absolute/path/to/venv/bin/python",
        "Qt6_DIR": "/absolute/path/to/Qt6/lib/cmake/Qt6"
      }
    }
  ]
}
```

Typical `Qt6_DIR` values include:

- `/opt/homebrew/opt/qt/lib/cmake/Qt6`
- `/usr/local/opt/qt/lib/cmake/Qt6`
- `/usr/lib/x86_64-linux-gnu/cmake/Qt6`

If **QScintilla** is installed but still not detected, verify that **Qt 6** is being found correctly first, then re-run CMake.

## Configure Presets

Available `cmake --preset` configure presets:

- `dev-debug`
- `dev-relWithDeb`
- `dev-release`
- `dev-asan`

A local machine-specific preset can also be defined, for example:

- `dev-debug-local`

## Build Presets

Available `cmake --build` presets:

- `build-dev-debug`
- `build-dev-relWithDeb`
- `build-dev-release`
- `build-dev-asan`

If you define a local configure preset, you can also define a matching local build preset, for example:

- `build-dev-debug-local`

## Test Presets

Available `ctest --preset` presets:

- `test-dev-debug`
- `test-dev-relWithDeb`
- `test-dev-asan`

A local machine-specific test preset can also be defined, for example:

- `test-dev-debug-local`

## Configure, Build, and Test

### Debug build using a local preset

```bash
cmake --preset dev-debug-local
cmake --build --preset build-dev-debug-local
ctest --preset test-dev-debug-local
```

### Debug build without a local preset

```bash
cmake --preset dev-debug \
  -DPython3_EXECUTABLE=/absolute/path/to/venv/bin/python \
  -DQt6_DIR=/absolute/path/to/Qt6/lib/cmake/Qt6

cmake --build --preset build-dev-debug
ctest --preset test-dev-debug
```

## Troubleshooting

Common configuration issues include:

- **Qt 6 not found**  
  Set `Qt6_DIR` to the directory containing `Qt6Config.cmake`.

- **Python 3 not found**  
  Set `Python3_EXECUTABLE` to the Python interpreter inside your virtual environment.

- **QScintilla not found**  
  Install the package for your platform, confirm that Qt 6 is detected correctly, and re-run configuration.

## Authors

- **Brock Sorenson**  
  `btsorens@asu.edu`  
  `brocksorenson10@gmail.com`

- **Samer Ali**  
  `swali6@asu.edu`

## License

IRONSmith is licensed under the **GNU General Public License v3.0**.

Copyright (C) 2026 **Samer Ali** and **Brock Sorenson**

A copy of the full license text should be included in the [`LICENSE`](LICENSE) file.

## Third-Party Dependencies

IRONSmith depends on third-party software, including:

- **Qt**
- **QScintilla**
- **nlohmann_json**
- **GoogleTest / GMock** (when tests are enabled)

Please review the licenses of these dependencies before redistribution.
