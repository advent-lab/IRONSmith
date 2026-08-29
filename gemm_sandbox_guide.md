# Workshop: 16-Tile Matrix Multiplication — Sandbox Fast Path

`C = A @ B` — 256×128 · 128×128 → 256×128, int16×int16→int32

## How the multiply is split across the grid

The 16 compute tiles form a 4×4 grid of output chunks — 4 grid columns × 4 grid rows of compute tiles (rows 2–5). Each compute tile owns one 64×32 chunk of `C` and nothing else.

- **All 4 tiles in a compute row** share the same 64-row chunk of `A` — that row's 64×128 horizontal strip is broadcast across the row.
- **All 4 tiles in a compute column** share the same 32-column chunk of `B` — that column's 128×32 vertical strip is broadcast down the column.
- Each tile has both its `A` row-chunk and `B` column-chunk, so it computes its own 64×32 chunk of `C` independently of every other tile — accumulating over `K=128` in two 64-wide passes.
- The 4 tiles in a compute column each finish a different 64-row band of the same 32-column output strip. Joining them stacks those 4 bands into one full 256×32 column of `C`, which drains straight to DDR.

![Interface layout: pink = Home/Output ribbon tabs, red = Top Ribbon, yellow = left sidebar (Project, Kernels), blue = right sidebar (AIE, Log, Properties, Code, Symbols). Every tile is labeled with its (col,row) address.](docs/workshop/labeled_locations.png)

---

## 0. Open the Design

1. Launch IRONSmith.
2. In the **Project Explorer** (left sidebar), expand `Example_Designs` and open **`gemm_sandbox.ironsmith`**.
3. Open the **AIE** panel (right sidebar) → Layout group → set **Horizontal spacing** to `70` and **Vertical spacing** to `65`

Already done: Symbol Table (6 constants, 7 types, 12 TAPs) in the **Symbols** panel (right sidebar), and every compute tile's core function body.

---

## 1. Connect Dataflow

See the reference image at the end of this section for what the fully wired result should look like.

### DDR Transfers
Top Ribbon → Home tab → **DDR Transfers**. Click the **DDR tile first**, then each shim tile in turn.

If the 12 wires converging on the DDR tile need more room, select the DDR tile and use the **AIE** panel (right sidebar) → nudge step down to move it further from the shim row.

1. **Distribute** — DDR `A` → shim (0,0), (1,0), (2,0), (3,0)
2. **Distribute** — DDR `B` → shim (0,0), (1,0), (2,0), (3,0)
3. **Collect** — DDR `C` → shim (0,0), (1,0), (2,0), (3,0)

### Shim → Mem (input side)
Top Ribbon → Home tab → **Linking** → **FIFO**. Two wires per column, both to the same mem tile:

| ObjectFifo | Shim | Mem |
|---|---|---|
| of_in_a_row0 | (0,0) | (0,1) |
| of_in_a_row1 | (1,0) | (1,1) |
| of_in_a_row2 | (2,0) | (2,1) |
| of_in_a_row3 | (3,0) | (3,1) |
| of_in_b_col0 | (0,0) | (0,1) |
| of_in_b_col1 | (1,0) | (1,1) |
| of_in_b_col2 | (2,0) | (2,1) |
| of_in_b_col3 | (3,0) | (3,1) |

### Mem → Shim (output side)
Top Ribbon → Home tab → **Linking** → **FIFO**

| ObjectFifo | Mem | Shim |
|---|---|---|
| of_out_c_col0 | (0,1) | (0,0) |
| of_out_c_col1 | (1,1) | (1,0) |
| of_out_c_col2 | (2,1) | (2,0) |
| of_out_c_col3 | (3,1) | (3,0) |

For each FIFO above, select it and open the **Properties** panel (right sidebar) to set:
- Name: matches the ObjectFifo (e.g. `of_in_a_row0`)
- Symbol: `a_tile_ty` / `b_tile_ty` (inputs) or `c_col_ty` (outputs)

Cosmetic only, to keep the wiring cleaner to look at: attach broadcasts to the left side (inputs) and joins to the right side (outputs) of each mem/compute tile.

### Broadcast — Mem → AIE
Top Ribbon → Home tab → **Movement Patterns** → **Broadcast**. Click the **Mem tile first**, then each AIE tile in turn.

**A rows:**

| Source | Mem | → AIE tiles |
|---|---|---|
| A row 0 | (0,1) | (0,2), (1,2), (2,2), (3,2) |
| A row 1 | (1,1) | (0,3), (1,3), (2,3), (3,3) |
| A row 2 | (2,1) | (0,4), (1,4), (2,4), (3,4) |
| A row 3 | (3,1) | (0,5), (1,5), (2,5), (3,5) |

**B columns:**

| Source | Mem | → AIE tiles |
|---|---|---|
| B col 0 | (0,1) | (0,2), (0,3), (0,4), (0,5) |
| B col 1 | (1,1) | (1,2), (1,3), (1,4), (1,5) |
| B col 2 | (2,1) | (2,2), (2,3), (2,4), (2,5) |
| B col 3 | (3,1) | (3,2), (3,3), (3,4), (3,5) |

### Join — AIE → Mem
Top Ribbon → Home tab → **Movement Patterns** → **Join**. Click the Mem tile first, then attach bottom to top, per column:

| Join | Sources | → Mem |
|---|---|---|
| C col 0 | (0,2), (0,3), (0,4), (0,5) | (0,1) |
| C col 1 | (1,2), (1,3), (1,4), (1,5) | (1,1) |
| C col 2 | (2,2), (2,3), (2,4), (2,5) | (2,1) |
| C col 3 | (3,2), (3,3), (3,4), (3,5) | (3,1) |

![Fully wired reference topology: DDR transfers, shim→mem FIFOs, mem→AIE broadcasts, and AIE→mem joins for all 4 columns](docs/workshop/gemm_full_dataflow.png)

---

## 2. Assign Kernels

Tile (0,5) already has its kernel assigned. For the other 15:

1. Open the **Kernels** panel (left sidebar), search `matmul`
2. Select **GEMM 64x64x32 (INT16 x INT16 -> INT32, scalar)**
3. Click tiles: (0,2) (1,2) (2,2) (3,2) (0,3) (1,3) (2,3) (3,3) (0,4) (1,4) (2,4) (3,4) (1,5) (2,5) (3,5)
4. Click **Clear Active Kernel** in the Kernels panel (left sidebar)

---

## 3. Adjust Properties
Select each item below on the canvas, then edit it in the **Properties** panel (right sidebar).

### DDR block → DDR Runtime
Select the DDR block at the bottom of the screen.

**Inputs table:**

| Name | Total Size |
|---|---|
| A | M * K |
| B | K * N |

**Outputs table:**

| Name | Total Size |
|---|---|
| C | M * N |

### Each Distribute/Collect branch wire (12 total) → DDR Transfer
Hover over the wire between the Distribute/Collect hub and the DDR block to see which output (A, B, or C) that branch belongs to. Set each branch's Target FIFO and Tensor Access Pattern:

| Branch | Shim | Target FIFO | Tensor Access Pattern |
|---|---|---|---|
| Distribute A | (0,0) | of_in_a_row0 | of_in_a_row0_tap |
| Distribute A | (1,0) | of_in_a_row1 | of_in_a_row1_tap |
| Distribute A | (2,0) | of_in_a_row2 | of_in_a_row2_tap |
| Distribute A | (3,0) | of_in_a_row3 | of_in_a_row3_tap |
| Distribute B | (0,0) | of_in_b_col0 | of_in_b_col0_tap |
| Distribute B | (1,0) | of_in_b_col1 | of_in_b_col1_tap |
| Distribute B | (2,0) | of_in_b_col2 | of_in_b_col2_tap |
| Distribute B | (3,0) | of_in_b_col3 | of_in_b_col3_tap |
| Collect C | (0,0) | of_out_c_col0 | of_out_c_col0_tap |
| Collect C | (1,0) | of_out_c_col1 | of_out_c_col1_tap |
| Collect C | (2,0) | of_out_c_col2 | of_out_c_col2_tap |
| Collect C | (3,0) | of_out_c_col3 | of_out_c_col3_tap |

![Hovering the DDR-side wire shows its annotation, here `FILL: of_in_a_row0`; the Properties panel for that selected arm shows the matching Target FIFO `of_in_a_row0` and its TAP](docs/workshop/close_up_matching_fill_to_fifo_and_properties_panel_with_target_and_tap.png)

### Each Broadcast (8 total) — click the hub
For each broadcast hub, set the name and source FIFO in the Properties panel (right sidebar).

| Hub at | Name | Source FIFO |
|---|---|---|
| (0,1) | a_row0_fwd | of_in_a_row0 |
| (1,1) | a_row1_fwd | of_in_a_row1 |
| (2,1) | a_row2_fwd | of_in_a_row2 |
| (3,1) | a_row3_fwd | of_in_a_row3 |
| (0,1) | b_col0_fwd | of_in_b_col0 |
| (1,1) | b_col1_fwd | of_in_b_col1 |
| (2,1) | b_col2_fwd | of_in_b_col2 |
| (3,1) | b_col3_fwd | of_in_b_col3 |

### Each Join hub (4 total) — click the hub → Join
For each join hub, set the name, offsets, branch type, and destination FIFO in the Properties panel (right sidebar).

| Hub at | Name | Offsets | Branch Type | Destination |
|---|---|---|---|---|
| (0,1) | c_col0_joins | 0, m * n, 2 * m * n, 3 * m * n | c_tile_ty | of_out_c_col0 |
| (1,1) | c_col1_joins | 0, m * n, 2 * m * n, 3 * m * n | c_tile_ty | of_out_c_col1 |
| (2,1) | c_col2_joins | 0, m * n, 2 * m * n, 3 * m * n | c_tile_ty | of_out_c_col2 |
| (3,1) | c_col3_joins | 0, m * n, 2 * m * n, 3 * m * n | c_tile_ty | of_out_c_col3 |

---

## 4. Wire Up the Core Function

### On the other 15 compute tiles
For each tile: (0,2) (1,2) (2,2) (3,2) (0,3) (1,3) (2,3) (3,3) (0,4) (1,4) (2,4) (3,4) (1,5) (2,5) (3,5):
1. Click the tile
2. In the Properties panel (right sidebar) → Core Function → Mode: **Shared Function**
3. Function dropdown → select **core_fn_matmul** explicitly, even if it already appears selected
4. Check the **fn_args** table; click **Auto-assign** if any reference is blank

![Properties panel for a compute tile: Kernel, In/Out FIFOs, Core Function Mode set to Shared Function, and the fn_args table with Auto-assign](docs/workshop/shared_function_panel.png)

### On tile (0,5)
1. Click the tile
2. In the Properties panel (right sidebar) → **fn_args** table
3. Click **Auto-assign**

---

## 5. Generate & Execute

1. Top Ribbon → Output tab → **Verify Design**
2. Top Ribbon → Output tab → **Generate Code**
3. Copy the generated code from the **Code** panel (right sidebar) into the workshop notebook.
4. Directly under the `gui_design_jit(...)` call, insert:

```python
A_mat = A.numpy().astype(np.int64).reshape(M, K)
B_mat = B.numpy().astype(np.int64).reshape(K, N)
expected = (A_mat @ B_mat).astype(np.int32)
actual = C.numpy().reshape(M, N)

if np.array_equal(actual, expected):
    print("PASS: C matches A @ B")
else:
    mismatches = int(np.count_nonzero(actual != expected))
    print(f"FAIL: C does not match A @ B ({mismatches} / {actual.size} elements differ)")
    print(f"actual[:4,:4]   = {actual[:4,:4]}")
    print(f"expected[:4,:4] = {expected[:4,:4]}")
```

5. Run the notebook cell.

Expected output:

```
A[:8] = [0 1 2 3 4 5 6 7]
B[:8] = [0 1 2 3 4 5 6 7]
C[:8] = [   0  448  896 1344 1792 2240 2688 3136]
PASS: C matches A @ B
```