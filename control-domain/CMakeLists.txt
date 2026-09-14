cmake_minimum_required(VERSION 3.20.0)

# ---------------------------------------------------------------------
# Selecao de target
#
#   west build -b blackpill_f411ce .      -> perfil completo (artigo)
#   west build -b nucleo_u575zi_q .       -> perfil minimo (SPI + PWM)
#
# O overlay e o fragmento de Kconfig sao escolhidos EXPLICITAMENTE pelo
# nome da board. Isso evita depender da ordem de descoberta automatica
# de app.overlay / boards/<board>.overlay, que ja mudou entre versoes do
# Zephyr.
# ---------------------------------------------------------------------

# O Zephyr descobre sozinho boards/<board>.overlay e boards/<board>.conf
# (cmake/modules/configuration_files.cmake, linhas 47 e 77) -- mas SO se
# DTC_OVERLAY_FILE nao estiver definida. Definir a mao desativava esse
# mecanismo e, no sysbuild, ainda lia ${BOARD} vazio, gerando
# "boards/.overlay". Por isso nao setamos nada aqui.

# O Zephyr le o arquivo 'VERSION' automaticamente AQUI
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(infusion_pump)

# Validacao depois do find_package: aqui BOARD esta garantidamente
# definido, inclusive sob sysbuild.
set(ARGUS_BOARD_OVERLAY "${CMAKE_CURRENT_SOURCE_DIR}/boards/${BOARD}.overlay")
if(NOT EXISTS ${ARGUS_BOARD_OVERLAY})
    message(FATAL_ERROR
        "Board '${BOARD}' nao suportada por este projeto.\n"
        "Falta: ${ARGUS_BOARD_OVERLAY}\n"
        "Targets disponiveis: blackpill_f411ce, nucleo_u575zi_q")
endif()

message(STATUS "Argus target        : ${BOARD}")
message(STATUS "Argus DT overlay    : ${ARGUS_BOARD_OVERLAY}")

target_include_directories(app PRIVATE
    include
    utl
)

# Todos os fontes compilam nos dois targets. Os drivers de hardware
# opcional (encoder, ADC) viram stubs sozinhos quando os nos
# correspondentes nao existem no Device Tree -- ver src/encoder.c e
# src/adc_driver.c.
target_sources(app PRIVATE
    src/main.c
    src/cmd.c
    src/hub.c
    src/adc_driver.c
    src/encoder.c
    src/motor_driver.c
    src/logic_engine.c
    src/ota_handler.c
    utl/utl_io.c
    utl/utl_crc16.c
)

set(APP_VERSION_STRING "${APP_VERSION_MAJOR}.${APP_VERSION_MINOR}.${APP_PATCHLEVEL}")
message(STATUS "Argus firmware ver  : ${APP_VERSION_STRING}")

# Injeta a versao na assinatura do MCUboot
set(CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION "${APP_VERSION_STRING}" CACHE STRING "" FORCE)

# Artefato nomeado por board: argus_<board>_v<versao>.bin
set(OUTPUT_BIN_NAME "argus_${BOARD}_v${APP_VERSION_STRING}.bin")

# Sem MCUboot nao existe zephyr.signed.bin; usa o binario cru.
if(CONFIG_BOOTLOADER_MCUBOOT)
    set(ARGUS_SRC_BIN "${CMAKE_BINARY_DIR}/zephyr/zephyr.signed.bin")
else()
    set(ARGUS_SRC_BIN "${CMAKE_BINARY_DIR}/zephyr/zephyr.bin")
endif()

add_custom_target(artifact_gen ALL
    COMMAND ${CMAKE_COMMAND} -E copy
    ${ARGUS_SRC_BIN}
    ${CMAKE_SOURCE_DIR}/${OUTPUT_BIN_NAME}

    COMMENT ">>> [SUCESSO] ${OUTPUT_BIN_NAME}"
)

add_dependencies(artifact_gen zephyr_final)
