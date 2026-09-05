set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../core)
idf_component_register(
    SRCS "${CORE_DIR}/polepos.c" "${CORE_DIR}/polepos_video.c" "${CORE_DIR}/polepos_sound.c" "${CORE_DIR}/z80/Z80.c" "${CORE_DIR}/z8000/z8002.c"
    INCLUDE_DIRS "${CORE_DIR}" "${CORE_DIR}/z80" "${CORE_DIR}/z8000"
    LDFRAGMENTS "linker.lf"
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE LSB_FIRST)
set_source_files_properties(${CORE_DIR}/z8000/z8002.c PROPERTIES COMPILE_OPTIONS "-O3;-DZ8K_HOT=IRAM_ATTR;-include;esp_attr.h")
target_compile_options(${COMPONENT_LIB} PRIVATE -O3 -fjump-tables -ftree-switch-conversion -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable -Wno-misleading-indentation -Wno-maybe-uninitialized -Wno-format)
