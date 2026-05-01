#
# This file is licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# (c) Copyright 2025 Advanced Micro Devices, Inc. or its affiliates

# ── Toolchain discovery ────────────────────────────────────────────────────────
MLIR_AIE_DIR   ?= $(shell python3 -c "from aie.utils.config import root_path; print(root_path())")
PEANO_INSTALL_DIR ?= $(shell python3 -c \
	"import site, os; \
	 hits = [os.path.join(d,'llvm-aie') for d in site.getsitepackages() if os.path.isdir(os.path.join(d,'llvm-aie'))]; \
	 print(hits[0] if hits else '')")

KERNEL_CC      := $(PEANO_INSTALL_DIR)/bin/clang++

# ── Compiler flags ─────────────────────────────────────────────────────────────
KERNEL_CFLAGS  := -O2 -std=c++20 --target=aie2-none-unknown-elf \
                  -Wno-parentheses -Wno-attributes -Wno-macro-redefined \
                  -Wno-empty-body -DNDEBUG \
                  -I $(MLIR_AIE_DIR)/include

# MLP uses M=64, K=64, n_tile=16, dtype=int16
KERNEL_DEFINES := -Di16_i16_ONLY -DDIM_M=64 -DDIM_K=64 -DDIM_N=16 -DVECTORIZED_ONLY

# ── Build artifacts ────────────────────────────────────────────────────────────
BUILD_DIR      := build
MM_OBJ         := $(BUILD_DIR)/mm.o
RELU_OBJ       := $(BUILD_DIR)/relu.o
SOFTMAX_OBJ    := $(BUILD_DIR)/softmax_i16.o
ARCHIVE        := $(BUILD_DIR)/mm.a

# ── Top-level targets ──────────────────────────────────────────────────────────
.PHONY: all run run_generated clean

all: $(ARCHIVE)

# Link all object files into the archive
$(ARCHIVE): $(MM_OBJ) $(RELU_OBJ) $(SOFTMAX_OBJ)
	ar rcs $@ $^

$(MM_OBJ): kernels/mm.cc kernels/zero.cc aie_kernel_utils.h | $(BUILD_DIR)
	cd $(BUILD_DIR) && \
	$(KERNEL_CC) $(KERNEL_CFLAGS) $(KERNEL_DEFINES) -c ../kernels/mm.cc -o mm.o

$(RELU_OBJ): kernels/relu.cc aie_kernel_utils.h | $(BUILD_DIR)
	cd $(BUILD_DIR) && \
	$(KERNEL_CC) $(KERNEL_CFLAGS) $(KERNEL_DEFINES) -c ../kernels/relu.cc -o relu.o

$(SOFTMAX_OBJ): kernels/softmax_i16.cc aie_kernel_utils.h | $(BUILD_DIR)
	cd $(BUILD_DIR) && \
	$(KERNEL_CC) $(KERNEL_CFLAGS) $(KERNEL_DEFINES) -c ../kernels/softmax_i16.cc -o softmax_i16.o

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# ── Run ────────────────────────────────────────────────────────────────────────
run: all
	python3 mlp.py

run_generated: all
	python3 generated_mlp.py

# ── Clean ──────────────────────────────────────────────────────────────────────
clean:
	rm -rf $(BUILD_DIR)
