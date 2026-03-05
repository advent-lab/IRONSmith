# Bridge Integration Tests

The IRONSmith executable includes built-in integration tests for the HLIR C++ Bridge and Code Generation Bridge.

## Running Tests

To run the bridge tests, use the `--test-bridges` or `-t` command-line flag:

```bash
# From the build directory
./ironsmith.exe --test-bridges

# Or short form
./ironsmith.exe -t
```

## What Gets Tested

### HLIR C++ Bridge (8 tests)
1. **Create Constant** - Add a constant symbol
2. **Create Tensor Type** - Add a tensor type definition
3. **Create Tiles** - Add hardware tiles (shim, mem)
4. **Create FIFO** - Add a data FIFO connecting tiles
5. **Update Tile** - Update tile location using its ID
6. **Update FIFO Depth** - Modify FIFO buffer depth
7. **Lookup Component** - Retrieve component data by ID
8. **Export to XML** - Generate GUI-compatible XML file

### Code Generation Bridge (2 tests)
1. **Check Availability** - Verify Python and code generator are accessible
2. **Get Python Version** - Query Python interpreter version

## Test Output

The tests will output:
- Individual test results (OK/FAILED)
- Detailed error messages if any test fails
- Summary showing which bridges passed/failed
- Overall test result (SUCCESS/FAILURE)

### Example Output

```
========================================
  IRONSmith Bridge Integration Tests
========================================

=== Testing HLIR C++ Bridge ===
  [1/8] Creating constant... OK (ID: 3f4a5b6c...)
  [2/8] Creating tensor type... OK (ID: 7d8e9f0a...)
  [3/8] Creating tiles... OK (2 tiles)
  [4/8] Creating FIFO... OK (ID: 1a2b3c4d...)
  [5/8] Updating tile location... OK (same ID, new location)
  [6/8] Updating FIFO depth... OK (depth: 2 -> 8)
  [7/8] Looking up component... OK (found FIFO data)
  [8/8] Exporting to XML... OK (1234 bytes)

  HLIR Bridge: ALL TESTS PASSED

=== Testing Code Generation Bridge ===
  [1/2] Checking code generator availability... OK
  [2/2] Getting Python version... OK (3.11.5)

  CodeGen Bridge: ALL TESTS PASSED

========================================
  Test Summary
========================================
  HLIR Bridge:    PASSED
  CodeGen Bridge: PASSED
========================================

  Overall: SUCCESS
```

## Exit Codes

- **0 (EXIT_SUCCESS)** - All tests passed
- **1 (EXIT_FAILURE)** - One or more tests failed

## Integration with CI/CD

These tests can be integrated into automated build pipelines:

```bash
# Build the project
cmake --build build --target ironsmith

# Run tests
./build/bin/ironsmith.exe --test-bridges
if [ $? -ne 0 ]; then
    echo "Bridge tests failed!"
    exit 1
fi
```

## Notes

- Tests create a temporary XML file (`bridge_test_output.xml`) which is automatically deleted after testing
- If the code generator is not available (main.py not found), the CodeGen Bridge test will report this but won't fail
- Tests are non-interactive and complete in under 1 second
- Tests do not require GUI or any user interaction
