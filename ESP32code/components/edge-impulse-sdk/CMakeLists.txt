cmake_minimum_required(VERSION 3.16)

set(EI_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/edge-impulse-sdk")

# ── Collect SDK sources (excluding CMSIS, using ESP-NN + ESP-DSP instead) ──
file(GLOB_RECURSE EI_CPP_SRCS
    "${EI_SDK_DIR}/*.cpp"
    "${EI_SDK_DIR}/*.cc"
)
file(GLOB_RECURSE EI_C_SRCS "${EI_SDK_DIR}/*.c")

# Exclude all CMSIS sources
list(FILTER EI_CPP_SRCS EXCLUDE REGEX ".*/CMSIS/.*")
list(FILTER EI_C_SRCS   EXCLUDE REGEX ".*/CMSIS/.*")

# Re-add the single CMSIS file needed
set(EI_C_SRCS ${EI_C_SRCS}
    "${EI_SDK_DIR}/CMSIS/DSP/Source/StatisticsFunctions/arm_rms_f32.c"
)

# ESP-NN optimized assembly for ESP32-S3
file(GLOB_RECURSE EI_ESPNN_ASM
    "${EI_SDK_DIR}/porting/espressif/ESP-NN/*.s"
    "${EI_SDK_DIR}/porting/espressif/ESP-NN/*.S"
)

# ESP-DSP FFT optimized assembly
file(GLOB_RECURSE EI_FFT_ASM
    "${EI_SDK_DIR}/porting/espressif/esp-dsp/modules/fft/*.s"
    "${EI_SDK_DIR}/porting/espressif/esp-dsp/modules/fft/*.S"
)

# ESP-DSP C/C++ sources (ANSI fallbacks + bit-rev tables + common utils)
file(GLOB_RECURSE EI_ESPDSP_C_SRCS
    "${EI_SDK_DIR}/porting/espressif/esp-dsp/modules/*.c"
)
file(GLOB_RECURSE EI_ESPDSP_CPP_SRCS
    "${EI_SDK_DIR}/porting/espressif/esp-dsp/modules/*.cpp"
)

# TFLite compiled model
file(GLOB MODEL_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/tflite-model/*.cpp")

# ── Register as ESP-IDF component ──
idf_component_register(
    SRCS
        ${EI_CPP_SRCS}
        ${EI_C_SRCS}
        ${EI_ESPNN_ASM}
        ${EI_FFT_ASM}
        ${EI_ESPDSP_C_SRCS}
        ${EI_ESPDSP_CPP_SRCS}
        ${MODEL_SRCS}
    INCLUDE_DIRS
        "."
        "${EI_SDK_DIR}"
        "${EI_SDK_DIR}/porting"
        "${EI_SDK_DIR}/porting/espressif/esp-dsp/modules/common/include"
        "${EI_SDK_DIR}/porting/espressif/esp-dsp/modules/fft/include"
        "${EI_SDK_DIR}/porting/espressif/ESP-NN"
        "${EI_SDK_DIR}/porting/espressif/ESP-NN/include"
    REQUIRES
        esp_timer
        heap
        freertos
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC
    EI_PORTING_ESPRESSIF=1
    asm=__asm__
    portTICK_RATE_MS=portTICK_PERIOD_MS
)

# Suppress warnings in third-party Edge Impulse / TFLite code
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-maybe-uninitialized
    -Wno-deprecated-declarations
    -Wno-missing-field-initializers
    -Wno-unused-variable
    -Wno-unused-function
)
