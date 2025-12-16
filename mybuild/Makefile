.PHONY: build
.DEFAULT_GOAL := build

PARALLEL_COUNT=16
EXAMPLE_FLAGS=-O0 -g
# GEM5_TRACE_FLAG=--debug-flags=Exec
# GEM5_TRACE_FLAG=--debug-break=1355932000
# If compile failed, explicitly do this: export PATH=$PATH:/gem5/riscv-released/riscv/bin/
GEM5_TRACE_FLAG=
RUN_SIM=./build/ARM/gem5.opt --outdir=m5out_dmp $(GEM5_TRACE_FLAG) configs/deprecated/example/se.py --cpu-type=DerivO3CPU --caches --l2cache --dmp --l1d_size=64kB --l1i_size=32kB --l2_size=512kB --l3_size=4MB --l3_size=4MB --cacheline_size=64 -c
RUN_SIM_NO_DMP=./build/ARM/gem5.opt --outdir=m5out_none $(GEM5_TRACE_FLAG) configs/deprecated/example/se.py --cpu-type=DerivO3CPU --caches --l2cache --l1d_size=64kB --l1i_size=32kB --l2_size=512kB --l3_size=4MB --l3_size=4MB --cacheline_size=64 -c
DEBUG_SIM=./build/ARM/gem5.opt --outdir=m5out_dmp --debug-flags=HWPrefetch configs/deprecated/example/se.py --cpu-type=DerivO3CPU --caches --l2cache --dmp --l1d_size=64kB --l1i_size=32kB --l2_size=512kB --l3_size=4MB --l3_size=4MB --cacheline_size=64 -c
DEBUG_SIM_NO_DMP=./build/ARM/gem5.opt --outdir=m5out_none --debug-flags=HWPrefetch configs/deprecated/example/se.py --cpu-type=DerivO3CPU --caches --l2cache --l1d_size=64kB --l1i_size=32kB --l2_size=512kB --l3_size=4MB --l3_size=4MB --cacheline_size=64 -c

gem5:
		cd .. && scons build/ARM/gem5.opt -j$(PARALLEL_COUNT)

mybuildout/test-umov: ../tests/test-progs/hello/src/hello.c
		aarch64-linux-gnu-g++ $^ $(EXAMPLE_FLAGS) -static -o $@

mybuildout/test-ptrchase: ../tests/test-progs/dmp/pointer_chasing.cpp # radical_chase.cpp 
		aarch64-linux-gnu-g++ $^ $(EXAMPLE_FLAGS) -static -o $@

# mybuildout/test-hfi: ../tests/test-progs/hfi/test-hfi.cpp ../tests/test-progs/hfi/hfi.S
# 		riscv64-linux-gnu-g++ $^ $(EXAMPLE_FLAGS) -static -o $@

build-tests: mybuildout/test-umov mybuildout/test-ptrchase

# build: gem5 build-tests


test-umov: build-tests
		cd .. && $(RUN_SIM) mybuild/mybuildout/test-umov -o 5

debug-umov: build-tests
		cd .. && $(DEBUG_SIM) mybuild/mybuildout/test-umov -o 5

debug-ptrchase: build-tests
		cd .. && $(DEBUG_SIM) mybuild/mybuildout/test-ptrchase

test-ptrchase: build-tests
		cd .. && $(RUN_SIM) mybuild/mybuildout/test-ptrchase

test-no-dmp-ptrchase: build-tests
		cd .. && $(RUN_SIM_NO_DMP) mybuild/mybuildout/test-ptrchase

debug-no-dmp-ptrchase: build-tests
		cd .. && $(DEBUG_SIM_NO_DMP) mybuild/mybuildout/test-ptrchase

#         cd .. && $(RUN_SIM) mybuild/mybuildout/test-hfi
# debug-hfi: build-tests
#         cd .. && $(DEBUG_SIM) mybuild/mybuildout/test-hfi

# debug-wasm: build-tests
#         cd .. && $(DEBUG_SIM) tests/test-progs/wasm2c-test/hfi-test/riscv_test

# test-wasm: build-tests
#         cd .. && $(RUN_SIM) tests/test-progs/wasm2c-test/hfi-test/riscv_test

# debug-sep: build-tests
#         cd .. && $(DEBUG_SIM) tests/test-progs/sepstack-test/build/sepstack-riscv-test

# test-sep: build-tests
#         cd .. && $(RUN_SIM) tests/test-progs/sepstack-test/build/sepstack-riscv-test
