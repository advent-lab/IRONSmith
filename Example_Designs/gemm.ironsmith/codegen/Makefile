#
# This file is licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# (c) Copyright 2025 Advanced Micro Devices, Inc. or its affiliates

# ── Toolchain discovery ────────────────────────────────────────────────────────
MLIR_AIE_DIR      ?= $(shell python3 -c "from aie.utils.config import root_path; print(root_path())")
PEANO_INSTALL_DIR ?= $(shell python3 -c \
	"import site, os; \
	 hits = [os.path.join(d,'llvm-aie') for d in site.getsitepackages() \
	         if os.path.isdir(os.path.join(d,'llvm-aie'))]; \
	 print(hits[0] if hits else '')")

KERNEL_CC         := $(PEANO_INSTALL_DIR)/bin/clang++

# ── Compiler flags ─────────────────────────────────────────────────────────────
KERNEL_CFLAGS     := -O2 -std=c++20 --target=aie2-none-unknown-elf \
                     -Wno-parentheses -Wno-attributes -Wno-macro-redefined \
                     -Wno-empty-body -DNDEBUG \
                     -I $(MLIR_AIE_DIR)/include

# Adjust defines to match your design tile dimensions.
KERNEL_DEFINES    :=

# ── Build artifacts ────────────────────────────────────────────────────────────
BUILD_DIR         := build
MM_OBJ := $(BUILD_DIR)/mm.o
ZERO_OBJ := $(BUILD_DIR)/zero.o
MATMUL_SCALAR_I16_I32_ARCHIVE := $(BUILD_DIR)/matmul_scalar_i16_i32.a

# ── Top-level targets ──────────────────────────────────────────────────────────
.PHONY: all run_generated clean

all: $(MATMUL_SCALAR_I16_I32_ARCHIVE)

$(MATMUL_SCALAR_I16_I32_ARCHIVE): $(MM_OBJ) $(ZERO_OBJ)
	ar rcs $@ $^

$(MM_OBJ): kernels/mm.cc | $(BUILD_DIR)
	cd $(BUILD_DIR) && \
	$(KERNEL_CC) $(KERNEL_CFLAGS) $(KERNEL_DEFINES) -c ../kernels/mm.cc -o mm.o

$(ZERO_OBJ): kernels/zero.cc | $(BUILD_DIR)
	cd $(BUILD_DIR) && \
	$(KERNEL_CC) $(KERNEL_CFLAGS) $(KERNEL_DEFINES) -c ../kernels/zero.cc -o zero.o

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# ── Run ────────────────────────────────────────────────────────────────────────
run_generated: all
	python3 generated_gemm.py

# ── Clean ──────────────────────────────────────────────────────────────────────
clean:
	rm -rf $(BUILD_DIR)
