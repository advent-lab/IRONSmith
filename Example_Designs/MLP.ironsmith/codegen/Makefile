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
RELU_I16_OBJ := $(BUILD_DIR)/relu_i16.o
SOFTMAX_I16_OBJ := $(BUILD_DIR)/softmax_i16.o
MATMUL_I16_I16_RELU_I16_ZERO_I16_ARCHIVE := $(BUILD_DIR)/matmul_i16_i16_relu_i16_zero_i16.a
MATMUL_I16_I16_ZERO_I16_SOFTMAX_I16_ARCHIVE := $(BUILD_DIR)/matmul_i16_i16_zero_i16_softmax_i16.a

# ── Top-level targets ──────────────────────────────────────────────────────────
.PHONY: all run_generated clean

all: $(MATMUL_I16_I16_RELU_I16_ZERO_I16_ARCHIVE) $(MATMUL_I16_I16_ZERO_I16_SOFTMAX_I16_ARCHIVE)

$(MATMUL_I16_I16_RELU_I16_ZERO_I16_ARCHIVE): $(MM_OBJ) $(ZERO_OBJ) $(RELU_I16_OBJ)
	ar rcs $@ $^

$(MATMUL_I16_I16_ZERO_I16_SOFTMAX_I16_ARCHIVE): $(MM_OBJ) $(ZERO_OBJ) $(SOFTMAX_I16_OBJ)
	ar rcs $@ $^

$(SOFTMAX_I16_OBJ): kernels/softmax_i16.cc | $(BUILD_DIR)
	cd $(BUILD_DIR) && \
	$(KERNEL_CC) $(KERNEL_CFLAGS) $(KERNEL_DEFINES) -c ../kernels/softmax_i16.cc -o softmax_i16.o

$(RELU_I16_OBJ): kernels/relu_i16.cc | $(BUILD_DIR)
	cd $(BUILD_DIR) && \
	$(KERNEL_CC) $(KERNEL_CFLAGS) $(KERNEL_DEFINES) -c ../kernels/relu_i16.cc -o relu_i16.o

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
	python3 generated_MLP.py

# ── Clean ──────────────────────────────────────────────────────────────────────
clean:
	rm -rf $(BUILD_DIR)
