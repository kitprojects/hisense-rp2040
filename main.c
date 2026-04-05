/**
 * Hisense AC DISP Emulator for RP2040 - v9
 * 
 * Commands:
 * - M : toggle mode (0=passthrough, 1=emulator). Persists.
 * - R : manual start emulator (if in emu mode)
 * - X : stop emulator (if running)
 * - A/B : set slots
 * - ? : status
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "passthrough.pio.h"
#include "uart_rx.pio.h"
#include "uart_rx_inv.pio.h"
#include "uart_tx.pio.h"

#define PIN_TX_DISP     0
#define PIN_RX_DISP     1
#define PIN_TX_INV      4
#define PIN_RX_INV      5

#define BAUD_RATE       1200
#define MSG_LEN         16
#define BYTE_TIME_US    8333

static PIO pio_pt;
static PIO pio_uart;

static uint8_t slot_a[MSG_LEN] = {0xA1, 0x21, 0x00, 0x00, 0x0C, 0x10, 0x00, 0x00, 
                                   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xDE};
static uint8_t slot_b[MSG_LEN] = {0xA1, 0x22, 0x00, 0x00, 0x64, 0x5D, 0x5E, 0x00,
                                   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE2};
static uint8_t tx_buf[MSG_LEN];
static uint8_t rx_buf[MSG_LEN];

static int mode = 0;  // 0=passthrough, 1=emulator
static bool emulator_running = false;

// Forward declaration
static void print_msg(const char *prefix, const uint8_t *msg, int len);

// DMA Ring Buffer for DISP UART RX
#define DISP_BUF_SIZE 256
#define DISP_BUF_BYTES (DISP_BUF_SIZE * 4)

static uint32_t disp_dma_buf[DISP_BUF_SIZE] __attribute__((aligned(DISP_BUF_BYTES)));
static int disp_dma_chan = -1;
static uint32_t disp_read_idx = 0;
static uint8_t disp_msg_buf[MSG_LEN];
static int disp_msg_idx = 0;

static void init_disp_dma(void) {
    disp_dma_chan = dma_claim_unused_channel(true);
    
    dma_channel_config c = dma_channel_get_default_config(disp_dma_chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_ring(&c, true, 10);  // wrap at 1024 bytes
    channel_config_set_dreq(&c, pio_get_dreq(pio_uart, 0, false));
    
    dma_channel_configure(
        disp_dma_chan,
        &c,
        disp_dma_buf,
        &pio_uart->rxf[0],
        UINT32_MAX,
        true
    );
}

static void process_disp_dma(void) {
    uint32_t write_addr = dma_channel_hw_addr(disp_dma_chan)->write_addr;
    uint32_t buf_base = (uint32_t)disp_dma_buf;
    
    if (write_addr < buf_base || write_addr >= buf_base + DISP_BUF_BYTES) {
        return;
    }
    
    uint32_t write_idx = (write_addr - buf_base) / 4;
    uint32_t available = (write_idx - disp_read_idx + DISP_BUF_SIZE) % DISP_BUF_SIZE;
    
    // Overflow: snap forward if DMA lapped us
    if (available > DISP_BUF_SIZE - 8) {
        disp_read_idx = (write_idx + 1) % DISP_BUF_SIZE;
        disp_msg_idx = 0;
        return;
    }
    
    while (disp_read_idx != write_idx) {
        uint32_t word = disp_dma_buf[disp_read_idx];
        uint8_t byte = word >> 24;
        disp_read_idx = (disp_read_idx + 1) % DISP_BUF_SIZE;
        
        if (byte == 0xA1) {
            disp_msg_idx = 0;
        }
        
        disp_msg_buf[disp_msg_idx++] = byte;
        if (disp_msg_idx >= MSG_LEN) {
            print_msg("D>", disp_msg_buf, MSG_LEN);
            disp_msg_idx = 0;
        }
    }
}

static void tx_byte(uint8_t byte) {
    pio_sm_put_blocking(pio_pt, 2, byte);
    uint32_t end = time_us_32() + BYTE_TIME_US + 500;
    while (time_us_32() < end) {
        process_disp_dma();
    }
}

static void tx_message(const uint8_t *msg, int len) {
    for (int i = 0; i < len; i++) {
        tx_byte(msg[i]);
    }
}

static int rx_byte_timeout(uint32_t timeout_us) {
    uint32_t start = time_us_32();
    while (pio_sm_is_rx_fifo_empty(pio_uart, 1)) {
        process_disp_dma();
        if (time_us_32() - start > timeout_us) return -1;
    }
    uint8_t byte = pio_sm_get(pio_uart, 1) >> 24;
    return ~byte & 0xFF;
}

static int rx_message(uint8_t *buf, uint32_t timeout_us) {
    int count = 0;
    int b = rx_byte_timeout(timeout_us);
    if (b < 0) return 0;
    buf[count++] = b;
    while (count < MSG_LEN) {
        b = rx_byte_timeout(BYTE_TIME_US * 3);
        if (b < 0) break;
        buf[count++] = b;
    }
    return count;
}

static void rx_drain(void) {
    while (!pio_sm_is_rx_fifo_empty(pio_uart, 1)) {
        pio_sm_get(pio_uart, 1);
    }
}

static void print_msg(const char *prefix, const uint8_t *msg, int len) {
    printf("%s", prefix);
    for (int i = 0; i < len; i++) printf("%02X ", msg[i]);
    printf("\n");
}

static int parse_hex(const char *str, uint8_t *buf, int max_len) {
    int count = 0;
    const char *p = str;
    while (*p && count < max_len) {
        while (*p == ' ') p++;
        if (!*p) break;
        if (p[0] && p[1]) {
            char hex[3] = {p[0], p[1], 0};
            buf[count++] = (uint8_t)strtol(hex, NULL, 16);
            p += 2;
        } else break;
    }
    return count;
}

static void start_emulator(void) {
    if (mode != 1) return;  // Only start if in emulator mode
    
    printf("EMU: starting\n");
    emulator_running = true;
    
    pio_sm_set_enabled(pio_pt, 0, false);
    pio_sm_set_enabled(pio_pt, 2, true);
    rx_drain();
    
    printf("EMU: zeros\n");
    for (int i = 0; i < 15; i++) {
        uint8_t zeros[16] = {0};
        tx_message(zeros, 16);
        sleep_ms(30);
        rx_drain();
    }
    
    printf("EMU: FE\n");
    tx_byte(0xFE);
    sleep_ms(100);
    
    for (int i = 0; i < 50; i++) {
        int b = rx_byte_timeout(20000);
        if (b == 0xFE) {
            printf("EMU: got FE\n");
            break;
        }
    }
    
    sleep_ms(200);
    rx_drain();
    printf("EMU: running\n");
    fflush(stdout);
}

static void stop_emulator(void) {
    emulator_running = false;
    pio_sm_set_enabled(pio_pt, 2, false);
    pio_sm_set_enabled(pio_pt, 0, true);
    printf("EMU: stopped\n");
    fflush(stdout);
}

int main() {
    stdio_init_all();
    
    gpio_init(PIN_TX_DISP); gpio_set_dir(PIN_TX_DISP, GPIO_OUT); gpio_put(PIN_TX_DISP, 1);
    gpio_init(PIN_RX_DISP); gpio_set_dir(PIN_RX_DISP, GPIO_IN);
    gpio_init(PIN_TX_INV); gpio_set_dir(PIN_TX_INV, GPIO_OUT); gpio_put(PIN_TX_INV, 1);
    gpio_init(PIN_RX_INV); gpio_set_dir(PIN_RX_INV, GPIO_IN); gpio_pull_up(PIN_RX_INV);
    
    pio_pt = pio0;
    
    uint pt_offset = pio_add_program(pio_pt, &passthrough_program);
    passthrough_program_init(pio_pt, 0, pt_offset, PIN_RX_DISP, PIN_TX_INV);
    passthrough_program_init(pio_pt, 1, pt_offset, PIN_RX_INV, PIN_TX_DISP);
    
    uint tx_offset = pio_add_program(pio_pt, &uart_tx_program);
    uart_tx_program_init(pio_pt, 2, tx_offset, PIN_TX_INV, BAUD_RATE);
    pio_sm_set_enabled(pio_pt, 2, false);
    
    pio_uart = pio1;
    
    uint uart_offset = pio_add_program(pio_uart, &uart_rx_program);
    uart_rx_program_init(pio_uart, 0, uart_offset, PIN_RX_DISP, BAUD_RATE);
    
    uint uart_inv_offset = pio_add_program(pio_uart, &uart_rx_inv_program);
    uart_rx_inv_program_init(pio_uart, 1, uart_inv_offset, PIN_RX_INV, BAUD_RATE);
    
    // Start DMA to continuously drain DISP RX into ring buffer
    init_disp_dma();
    
    sleep_ms(2000);
    printf("BOOT v10-dma\n");
    printf("Mode: %s\n", mode ? "EMU" : "PT");
    fflush(stdout);
    
    char cmd_buf[128];
    int cmd_idx = 0;
    int cycle = 0;
    uint32_t last_hb = 0;
    
    while (1) {
        uint32_t now = time_us_32();
        
        if (emulator_running) {
            memcpy(tx_buf, (cycle % 2 == 0) ? slot_a : slot_b, MSG_LEN);
            
            tx_message(tx_buf, MSG_LEN);
            process_disp_dma();  // drain while TX
            
            int len = rx_message(rx_buf, 500000);
            process_disp_dma();  // drain after RX
            
            if (len > 0) {
                print_msg("I>", rx_buf, len);
                fflush(stdout);
            }
            
            // Busy-wait with DMA drain instead of sleep
            uint32_t wait_start = time_us_32();
            while (time_us_32() - wait_start < 50000) {
                process_disp_dma();
            }
            cycle++;
        } else {
            // Capture and print both directions
            // DISP: use DMA buffer (DMA owns the FIFO)
            process_disp_dma();
            
            while (!pio_sm_is_rx_fifo_empty(pio_uart, 1)) {
                static uint8_t inv_buf[MSG_LEN];
                static int inv_idx = 0;
                uint8_t raw = pio_sm_get(pio_uart, 1) >> 24;
                uint8_t byte = ~raw & 0xFF;
                
                // Auto-start emulator if in emu mode and see zeros
                if (mode == 1 && !emulator_running && byte == 0x00) {
                    start_emulator();
                    inv_idx = 0;
                    break;
                }
                
                inv_buf[inv_idx++] = byte;
                if (inv_idx >= MSG_LEN) {
                    print_msg("I>", inv_buf, MSG_LEN);
                    fflush(stdout);
                    inv_idx = 0;
                }
            }
            
            // Heartbeat
            if (now - last_hb > 2000000) {
                printf("%s\n", mode ? "EMU-WAIT" : "PT");
                fflush(stdout);
                last_hb = now;
            }
        }
        
        // USB commands
        int ch = getchar_timeout_us(0);
        if (ch != PICO_ERROR_TIMEOUT) {
            if (ch == '\n' || ch == '\r') {
                cmd_buf[cmd_idx] = 0;
                if (cmd_idx > 0) {
                    char c = cmd_buf[0];
                    if (c == 'A' || c == 'a') {
                        int n = parse_hex(cmd_buf + 1, slot_a, MSG_LEN);
                        printf("A:%d\n", n);
                    } else if (c == 'B' || c == 'b') {
                        int n = parse_hex(cmd_buf + 1, slot_b, MSG_LEN);
                        printf("B:%d\n", n);
                    } else if (c == 'M' || c == 'm') {
                        if (emulator_running) stop_emulator();
                        mode = 1 - mode;
                        if (mode == 1) {
                            // Disable passthrough DISP→INV so INV sees zeros
                            pio_sm_set_enabled(pio_pt, 0, false);
                            printf("Mode: EMU (waiting for zeros)\n");
                            sleep_ms(2000);  // let system settle
                        } else {
                            // Re-enable passthrough
                            pio_sm_set_enabled(pio_pt, 0, true);
                            printf("Mode: PT\n");
                        }
                    } else if (c == 'X' || c == 'x') {
                        if (emulator_running) stop_emulator();
                    } else if (c == 'R' || c == 'r') {
                        if (mode == 1 && !emulator_running) start_emulator();
                    } else if (c == '?') {
                        printf("v10-dma Mode:%s Run:%d\n", mode ? "EMU" : "PT", emulator_running);
                        print_msg("A:", slot_a, MSG_LEN);
                        print_msg("B:", slot_b, MSG_LEN);
                    } else if (c == '!') {
                        reset_usb_boot(0, 0);
                    }
                    fflush(stdout);
                }
                cmd_idx = 0;
            } else if (cmd_idx < 127) {
                cmd_buf[cmd_idx++] = ch;
            }
        }
    }
}
