#include "board_config.h"
#include "gd32f4xx.h"
#include "gd32f4xx_gpio.h"
#include "gd32f4xx_rcu.h"
#include "gd32f4xx_usart.h"

#include <stdint.h>
#include <string.h>

static volatile uint32_t system_milliseconds;
static volatile uint32_t ram_test_buffer[BOARD_RAM_TEST_WORDS];

void SysTick_Handler(void)
{
    system_milliseconds++;
}

static void uart_write_char(char value)
{
    while (RESET == usart_flag_get(BOARD_DEBUG_USART, USART_FLAG_TBE)) {
    }
    usart_data_transmit(BOARD_DEBUG_USART, (uint16_t)(uint8_t)value);
}

static void uart_write_string(const char *text)
{
    while (*text != '\0') {
        if (*text == '\n') {
            uart_write_char('\r');
        }
        uart_write_char(*text++);
    }
}

static void uart_write_u32(uint32_t value)
{
    char digits[10];
    uint32_t count = 0U;

    do {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while ((value != 0U) && (count < sizeof(digits)));

    while (count > 0U) {
        uart_write_char(digits[--count]);
    }
}

static char uart_read_char(void)
{
    while (RESET == usart_flag_get(BOARD_DEBUG_USART, USART_FLAG_RBNE)) {
    }
    return (char)usart_data_receive(BOARD_DEBUG_USART);
}

static void uart_read_line(char *buffer, uint32_t buffer_size)
{
    uint32_t length = 0U;

    for (;;) {
        char value = uart_read_char();

        if ((value == '\r') || (value == '\n')) {
            if ((value == '\n') && (length == 0U)) {
                continue;
            }
            buffer[length] = '\0';
            uart_write_string("\n");
            return;
        }

        if (((value == '\b') || ((uint8_t)value == 0x7FU)) && (length > 0U)) {
            length--;
            uart_write_string("\b \b");
            continue;
        }

        if (((uint8_t)value >= 0x20U) && (length < (buffer_size - 1U))) {
            buffer[length++] = value;
            uart_write_char(value);
        }
    }
}

static void uart_init(void)
{
    rcu_periph_clock_enable(BOARD_DEBUG_GPIO_CLOCK);
    rcu_periph_clock_enable(BOARD_DEBUG_USART_CLOCK);

    gpio_af_set(BOARD_DEBUG_GPIO, BOARD_DEBUG_GPIO_AF,
                BOARD_DEBUG_TX_PIN | BOARD_DEBUG_RX_PIN);
    gpio_mode_set(BOARD_DEBUG_GPIO, GPIO_MODE_AF, GPIO_PUPD_PULLUP,
                  BOARD_DEBUG_TX_PIN | BOARD_DEBUG_RX_PIN);
    gpio_output_options_set(BOARD_DEBUG_GPIO, GPIO_OTYPE_PP,
                            GPIO_OSPEED_50MHZ, BOARD_DEBUG_TX_PIN);

    usart_deinit(BOARD_DEBUG_USART);
    usart_baudrate_set(BOARD_DEBUG_USART, BOARD_DEBUG_BAUDRATE);
    usart_word_length_set(BOARD_DEBUG_USART, USART_WL_8BIT);
    usart_stop_bit_set(BOARD_DEBUG_USART, USART_STB_1BIT);
    usart_parity_config(BOARD_DEBUG_USART, USART_PM_NONE);
    usart_receive_config(BOARD_DEBUG_USART, USART_RECEIVE_ENABLE);
    usart_transmit_config(BOARD_DEBUG_USART, USART_TRANSMIT_ENABLE);
    usart_enable(BOARD_DEBUG_USART);
}

static uint8_t internal_sram_test(void)
{
    static const uint32_t patterns[] = {
        0x00000000U,
        0xFFFFFFFFU,
        0xAAAAAAAAU,
        0x55555555U,
        0xA5A5A5A5U,
        0x5A5A5A5AU
    };
    uint32_t pattern_index;
    uint32_t word_index;

    for (pattern_index = 0U;
         pattern_index < (sizeof(patterns) / sizeof(patterns[0]));
         pattern_index++) {
        for (word_index = 0U; word_index < BOARD_RAM_TEST_WORDS; word_index++) {
            ram_test_buffer[word_index] = patterns[pattern_index] ^
                                          (word_index * 0x01010101U);
        }

        for (word_index = 0U; word_index < BOARD_RAM_TEST_WORDS; word_index++) {
            uint32_t expected = patterns[pattern_index] ^
                                (word_index * 0x01010101U);
            if (ram_test_buffer[word_index] != expected) {
                return 0U;
            }
        }
    }

    return 1U;
}

static void command_help(void)
{
    uart_write_string("Available commands:\n");
    uart_write_string("  help   Show available commands\n");
    uart_write_string("  info   Show MCU and firmware information\n");
    uart_write_string("  ram    Test a small buffer in internal SRAM\n");
    uart_write_string("  uptime Show time since firmware startup\n");
    uart_write_string("  reset  Reset the microcontroller\n");
}

static void command_info(void)
{
    uart_write_string("Device: GD32F450ZGT6\n");
    uart_write_string("Core clock reported by system: ");
    uart_write_u32(SystemCoreClock);
    uart_write_string(" Hz\n");
    uart_write_string("Debug UART: USART0, 115200 8-N-1\n");
    uart_write_string("External SDRAM, SPI flash, SDIO and USB drivers are not enabled in this baseline.\n");
}

static void command_ram(void)
{
    uart_write_string("Internal SRAM buffer test: ");
    if (internal_sram_test() != 0U) {
        uart_write_string("PASS\n");
    } else {
        uart_write_string("FAIL\n");
    }
}

static void command_uptime(void)
{
    uint32_t elapsed = system_milliseconds;

    uart_write_string("Uptime: ");
    uart_write_u32(elapsed / 1000U);
    uart_write_string(" s ");
    uart_write_u32(elapsed % 1000U);
    uart_write_string(" ms\n");
}

static void command_execute(const char *command)
{
    if (strcmp(command, "help") == 0) {
        command_help();
    } else if (strcmp(command, "info") == 0) {
        command_info();
    } else if (strcmp(command, "ram") == 0) {
        command_ram();
    } else if (strcmp(command, "uptime") == 0) {
        command_uptime();
    } else if (strcmp(command, "reset") == 0) {
        uart_write_string("Resetting...\n");
        NVIC_SystemReset();
    } else if (command[0] == '\0') {
    } else {
        uart_write_string("Unknown command. Type 'help'.\n");
    }
}

int main(void)
{
    char command[BOARD_COMMAND_BUFFER_SIZE];

    SystemCoreClockUpdate();
    if (SysTick_Config(SystemCoreClock / 1000U) != 0U) {
        for (;;) {
        }
    }

    uart_init();

    uart_write_string("\nGD32F450 Custom Development Board\n");
    uart_write_string("Bring-up firmware\n");
    uart_write_string("Type 'help' to list commands.\n\n");

    for (;;) {
        uart_write_string("> ");
        uart_read_line(command, sizeof(command));
        command_execute(command);
    }
}
