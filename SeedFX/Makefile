ROOT := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
BUILD := $(ROOT)build
CXX ?= g++
CMSIS := $(ROOT)../Software/GuitarPedal/dependencies/libDaisy/Drivers/CMSIS-DSP
HOST_FLAGS := -D__GNUC_PYTHON__ -DARM_MATH_LOOPUNROLL -I$(CMSIS)/Include -std=c++17 -O2 -Wall -Wextra -Werror -DNAM_A2_HOT_DATA= -DNAM_A2_HOT_STATE_DATA= -DNAM_A2_STATE_DATA=
IR_SOURCE := $(ROOT)../Software/GuitarPedal/Effect-Modules/ImpulseResponse/ir_data.cpp
.PHONY: test audio clean schema
test: $(BUILD)/tests $(BUILD)/warm-test
	$(BUILD)/tests
	$(BUILD)/warm-test
	python3 tools/model_tool.py --verify-builtin
	node --test tests/web.test.mjs
	python3 -m unittest discover -s tests -p '*_test.py'
$(BUILD)/warm-test: tests/warm_test.cpp firmware/audio/warm_start.hpp $(wildcard ../Software/GuitarPedal/Effect-Modules/Nam/*.h)
	mkdir -p $(BUILD)
	$(CXX) $(HOST_FLAGS) tests/warm_test.cpp -o $@
$(BUILD)/arm_fir_f32.o: $(CMSIS)/Source/FilteringFunctions/arm_fir_f32.c
	mkdir -p $(BUILD)
	$(CC) -O2 -D__GNUC_PYTHON__ -DARM_MATH_LOOPUNROLL -I$(CMSIS)/Include -c $< -o $@
$(BUILD)/arm_fir_init_f32.o: $(CMSIS)/Source/FilteringFunctions/arm_fir_init_f32.c
	mkdir -p $(BUILD)
	$(CC) -O2 -D__GNUC_PYTHON__ -I$(CMSIS)/Include -c $< -o $@
$(BUILD)/tests: $(BUILD)/arm_fir_f32.o $(BUILD)/arm_fir_init_f32.o tests/core_test.cpp firmware/common/*.hpp firmware/audio/*.hpp $(IR_SOURCE) $(wildcard ../Software/GuitarPedal/Effect-Modules/Nam/*.h)
	mkdir -p $(BUILD)
	$(CXX) $(HOST_FLAGS) tests/core_test.cpp $(IR_SOURCE) $(BUILD)/arm_fir_f32.o $(BUILD)/arm_fir_init_f32.o -o $@
audio:
	$(MAKE) -C firmware/audio -j4
schema:
	python3 tools/generate_schema.py
clean:
	rm -rf $(BUILD)
