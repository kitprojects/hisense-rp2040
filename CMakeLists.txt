cmake_minimum_required(VERSION 3.13)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(hisense_sniffer C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(hisense_sniffer
    main.c
)

pico_generate_pio_header(hisense_sniffer ${CMAKE_CURRENT_LIST_DIR}/passthrough.pio)
pico_generate_pio_header(hisense_sniffer ${CMAKE_CURRENT_LIST_DIR}/uart_rx.pio)
pico_generate_pio_header(hisense_sniffer ${CMAKE_CURRENT_LIST_DIR}/uart_rx_inv.pio)
pico_generate_pio_header(hisense_sniffer ${CMAKE_CURRENT_LIST_DIR}/uart_tx.pio)

target_link_libraries(hisense_sniffer
    pico_stdlib
    hardware_gpio
    hardware_pio
    hardware_dma
)

pico_enable_stdio_usb(hisense_sniffer 1)
pico_enable_stdio_uart(hisense_sniffer 0)

pico_add_extra_outputs(hisense_sniffer)
