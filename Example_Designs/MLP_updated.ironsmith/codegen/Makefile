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
GEMM_BF16_OBJ := $(BUILD_DIR)/gemm_bf16.o
RELU_OBJ := $(BUILD_DIR)/relu.o
SOFTMAX_OBJ := $(BUILD_DIR)/softmax.o
ZERO_I16_GEMM_BF16_RELU_BF16_ARCHIVE := $(BUILD_DIR)/zero_i16_gemm_bf16_relu_bf16.a
ZERO_I16_GEMM_BF16_SOFTMAX_BF16_ARCHIVE := $(BUILD_DIR)/zero_i16_gemm_bf16_softmax_bf16.a

# ── Top-level targets ──────────────────────────────────────────────────────────
.PHONY: all run_generated clean

all: $(ZERO_I16_GEMM_BF16_RELU_BF16_ARCHIVE) $(ZERO_I16_GEMM_BF16_SOFTMAX_BF16_ARCHIVE)

$(ZERO_I16_GEMM_BF16_RELU_BF16_ARCHIVE): $(MM_OBJ) $(ZERO_OBJ) $(GEMM_BF16_OBJ) $(RELU_OBJ)
	ar rcs $@ $^

$(ZERO_I16_GEMM_BF16_SOFTMAX_BF16_ARCHIVE): $(MM_OBJ) $(ZERO_OBJ) $(GEMM_BF16_OBJ) $(SOFTMAX_OBJ)
	ar rcs $@ $^

$(MM_OBJ): kernels/mm.cc | $(BUILD_DIR)
	cd $(BUILD_DIR) && \
	$(KERNEL_CC) $(KERNEL_CFLAGS) $(KERNEL_DEFINES) -c ../kernels/mm.cc -o mm.o

$(RELU_OBJ): kernels/relu.cc | $(BUILD_DIR)
	cd $(BUILD_DIR) && \
	$(KERNEL_CC) $(KERNEL_CFLAGS) $(KERNEL_DEFINES) -c ../kernels/relu.cc -o relu.o

$(GEMM_BF16_OBJ): kernels/gemm_bf16.cpp | $(BUILD_DIR)
	cd $(BUILD_DIR) && \
	$(KERNEL_CC) $(KERNEL_CFLAGS) $(KERNEL_DEFINES) -c ../kernels/gemm_bf16.cpp -o gemm_bf16.o

$(ZERO_OBJ): kernels/zero.cc | $(BUILD_DIR)
	cd $(BUILD_DIR) && \
	$(KERNEL_CC) $(KERNEL_CFLAGS) $(KERNEL_DEFINES) -c ../kernels/zero.cc -o zero.o

$(SOFTMAX_OBJ): kernels/softmax.cc | $(BUILD_DIR)
	cd $(BUILD_DIR) && \
	$(KERNEL_CC) $(KERNEL_CFLAGS) $(KERNEL_DEFINES) -c ../kernels/softmax.cc -o softmax.o

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# ── Run ────────────────────────────────────────────────────────────────────────
run_generated: all
	python3 generated_MLP_updated.py

# ── Clean ──────────────────────────────────────────────────────────────────────
clean:
	rm -rf $(BUILD_DIR)
