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
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
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

static void tx_byte(uint8_t byte) {
    pio_sm_put_blocking(pio_pt, 2, byte);
    sleep_us(BYTE_TIME_US + 500);
}

static void tx_message(const uint8_t *msg, int len) {
    for (int i = 0; i < len; i++) {
        tx_byte(msg[i]);
    }
}

static int rx_byte_timeout(uint32_t timeout_us) {
    uint32_t start = time_us_32();
    while (pio_sm_is_rx_fifo_empty(pio_uart, 1)) {
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
    
    sleep_ms(2000);
    printf("BOOT v9\n");
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
            int len = rx_message(rx_buf, 500000);
            
            if (len > 0) {
                print_msg("I>", rx_buf, len);
                fflush(stdout);
            }
            
            sleep_ms(50);
            cycle++;
        } else {
            // Capture and print both directions
            while (!pio_sm_is_rx_fifo_empty(pio_uart, 0)) {
                static uint8_t disp_buf[MSG_LEN];
                static int disp_idx = 0;
                uint8_t raw = pio_sm_get(pio_uart, 0) >> 24;
                disp_buf[disp_idx++] = raw;
                if (disp_idx >= MSG_LEN) {
                    print_msg("D>", disp_buf, MSG_LEN);
                    fflush(stdout);
                    disp_idx = 0;
                }
            }
            
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
                        printf("Mode: %s\n", mode ? "EMU" : "PT");
                    } else if (c == 'X' || c == 'x') {
                        if (emulator_running) stop_emulator();
                    } else if (c == 'R' || c == 'r') {
                        if (mode == 1 && !emulator_running) start_emulator();
                    } else if (c == '?') {
                        printf("v9 Mode:%s Run:%d\n", mode ? "EMU" : "PT", emulator_running);
                        print_msg("A:", slot_a, MSG_LEN);
                        print_msg("B:", slot_b, MSG_LEN);
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
