/*
 * CaDS Zero - console on USART3, which the Nucleo routes to the ST-Link
 * virtual COM port (PD8 = TX, PD9 = RX, AF7).
 *
 * TX is polled: it is used by the panic path and by the on-target test's TAP
 * stream, and a transport that depends on interrupts being alive is worthless
 * in exactly the situation where you need it most.
 *
 * RX is interrupt driven with a ring buffer, and that is not gold plating. The
 * STM32F4's USART has a ONE BYTE receive register and no FIFO. At 115200 baud
 * a byte lands every 87 us, so any polling loop slower than that silently
 * drops characters - the receiver simply overruns.
 *
 * This was found the hard way: the hardware explorer polled at 500 us, so
 * "b 90" arrived as "b", the argument parsed as zero, and the backlight was
 * dutifully set to 0% while the command still acknowledged success. The
 * display went black and looked for all the world like a display bug.
 */

#include "board.h"
#include "cads_hal.h"
#include "hal_gpio.h"

/* Power of two so the wrap is a mask. 256 bytes is four full command lines of
 * headroom at the speed a human or a script types. */
#define CADS_RX_SIZE 256u
#define CADS_RX_MASK (CADS_RX_SIZE - 1u)

static volatile uint8_t cads_rx_buffer[CADS_RX_SIZE];
static volatile uint32_t cads_rx_head; /* written by the ISR   */
static volatile uint32_t cads_rx_tail; /* written by the reader */
static volatile uint32_t cads_rx_dropped;
static volatile uint32_t cads_rx_overruns;

void cads_hal_console_init(uint32_t baud) {
    cads_gpio_init_alternate(CADS_PIN_UART_TX_PORT, CADS_PIN_UART_TX, CADS_UART_AF, CadsGpioPullUp);
    cads_gpio_init_alternate(CADS_PIN_UART_RX_PORT, CADS_PIN_UART_RX, CADS_UART_AF, CadsGpioPullUp);

    RCC->APB1ENR |= RCC_APB1ENR_USART3EN;
    (void)RCC->APB1ENR;

    CADS_CONSOLE_UART->CR1 = 0u; /* disable while reconfiguring */

    /* Oversampling by 16: BRR is simply PCLK/baud in 1/16th steps, which is
     * exactly the fixed point layout of the register. */
    CADS_CONSOLE_UART->BRR = (CADS_PCLK1_HZ + (baud / 2u)) / baud;

    CADS_CONSOLE_UART->CR2 = 0u; /* 1 stop bit      */
    CADS_CONSOLE_UART->CR3 = 0u; /* no flow control */

    cads_rx_head = 0u;
    cads_rx_tail = 0u;

    /* Priority below the display DMA: a dropped console byte is an
     * inconvenience, a disturbed pixel transfer is a visible artefact. */
    NVIC_SetPriority(USART3_IRQn, 8u);
    NVIC_EnableIRQ(USART3_IRQn);

    CADS_CONSOLE_UART->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_UE;
}

void USART3_IRQHandler(void) {
    uint32_t status = CADS_CONSOLE_UART->SR;

    /* Reading SR then DR is what clears ORE, FE, NE and PE on this part, so the
     * data register must be read even when the byte is being discarded -
     * otherwise the error latches and the receiver stops delivering. */
    if(status & (USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_PE)) {
        (void)CADS_CONSOLE_UART->DR;
        if(status & USART_SR_ORE) cads_rx_overruns++;
        return;
    }

    if(status & USART_SR_RXNE) {
        uint8_t byte = (uint8_t)(CADS_CONSOLE_UART->DR & 0xFFu);
        uint32_t next = (cads_rx_head + 1u) & CADS_RX_MASK;
        if(next == cads_rx_tail) {
            /* Reader is not keeping up. Drop the new byte rather than the
             * whole buffer, and count it so the loss is visible. */
            cads_rx_dropped++;
            return;
        }
        cads_rx_buffer[cads_rx_head] = byte;
        cads_rx_head = next;
    }
}

void cads_hal_console_write(const void* data, size_t length) {
    /* If USART3's clock is not enabled yet, every SR read returns 0, so the
     * TXE/TC busy-waits below would spin forever. The clock is only turned on
     * in cads_hal_console_init(); a fault that happens earlier (during
     * cads_hal_early_init/time_init/io_init) routes through the fault handler,
     * which calls this to dump registers. Without this guard that dump - meant
     * to preserve the fault frame at a breakpoint - would instead hang the CPU
     * here. Drop the write and return so the caller (e.g. the fault handler's
     * bkpt) can proceed. */
    if((RCC->APB1ENR & RCC_APB1ENR_USART3EN) == 0u) return;

    const uint8_t* bytes = (const uint8_t*)data;
    for(size_t i = 0; i < length; i++) {
        while(!(CADS_CONSOLE_UART->SR & USART_SR_TXE)) {
        }
        CADS_CONSOLE_UART->DR = bytes[i];
    }
    while(!(CADS_CONSOLE_UART->SR & USART_SR_TC)) {
    }
}

bool cads_hal_console_read(uint8_t* byte) {
    if(cads_rx_tail == cads_rx_head) return false;
    *byte = cads_rx_buffer[cads_rx_tail];
    cads_rx_tail = (cads_rx_tail + 1u) & CADS_RX_MASK;
    return true;
}

uint32_t cads_hal_console_dropped(void) {
    return cads_rx_dropped;
}

uint32_t cads_hal_console_overruns(void) {
    return cads_rx_overruns;
}
