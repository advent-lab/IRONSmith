#pragma once

namespace BridgeTests {

/**
 * Run integration tests for HLIR and CodeGen bridges.
 *
 * Tests the following:
 * 1. HLIR Bridge - Create components, update them, export to XML
 * 2. CodeGen Bridge - Check availability and version
 *
 * @return true if all tests pass, false otherwise
 */
bool runBridgeTests();

} // namespace BridgeTests
