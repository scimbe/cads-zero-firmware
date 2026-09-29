/*
 * CaDS Zero - WiFi co-processor link, USART6 (PC6=TX, PC7=RX, AF8), CADS_WIFI_BAUD (115200, see board.h).
 *
 * Structurally this is hal_console.c's driver again: RX is interrupt driven
 * into a ring buffer (the STM32F4 USART has a one-byte receive register and
 * no FIFO - a byte lands every ~87 us at 115200, so anything slower than
 * that polling drops characters), TX is a polled busy-wait. The difference
 * from the console is who reads the ring: cads_wifi_tick() (modules/wifi)
 * drains it and feeds pppos_input(), not a human at a terminal. pppos_input()
 * itself must not run in interrupt context under NO_SYS=1 - it does real FSM
 * work and pbuf allocation - so the ISR only ever touches the ring, never
 * lwIP. Same "single owner, one task" discipline the rest of this firmware's
 * lwIP usage already follows (see cads_net_board.c's own header).
 */

#include "board.h"
#include "cads_hal.h"
#include "hal_gpio.h"

#define CADS_WIFI_RX_SIZE 512u
#define CADS_WIFI_RX_MASK (CADS_WIFI_RX_SIZE - 1u)

static volatile uint8_t cads_wifi_rx_buffer[CADS_WIFI_RX_SIZE];
static volatile uint32_t cads_wifi_rx_head; /* written by the ISR    */
static volatile uint32_t cads_wifi_rx_tail; /* written by the reader */
static volatile uint32_t cads_wifi_rx_dropped;
static volatile uint32_t cads_wifi_rx_overruns;

void cads_hal_wifi_uart_init(void) {
    cads_gpio_init_alternate(
        CADS_PIN_WIFI_TX_PORT, CADS_PIN_WIFI_TX, CADS_WIFI_UART_AF, CadsGpioPullUp);
    cads_gpio_init_alternate(
        CADS_PIN_WIFI_RX_PORT, CADS_PIN_WIFI_RX, CADS_WIFI_UART_AF, CadsGpioPullUp);

    RCC->APB2ENR |= RCC_APB2ENR_USART6EN; /* USART6 is on APB2, unlike USART3/APB1 */
    (void)RCC->APB2ENR;

    CADS_WIFI_UART->CR1 = 0u;

    /* USART6 is clocked from APB2 (PCLK2), not PCLK1 like the console's
     * USART3 - using the wrong clock here would silently mis-baud the link. */
    CADS_WIFI_UART->BRR = (CADS_PCLK2_HZ + (CADS_WIFI_BAUD / 2u)) / CADS_WIFI_BAUD;

    CADS_WIFI_UART->CR2 = 0u;
    CADS_WIFI_UART->CR3 = 0u;

    cads_wifi_rx_head = 0u;
    cads_wifi_rx_tail = 0u;

    /* Same priority band as the console: below display DMA, above nothing
     * that matters more than a dropped WiFi byte (PPP will retransmit at the
     * protocol level, same as it would over a lossy modem line). */
    NVIC_SetPriority(USART6_IRQn, 8u);
    NVIC_EnableIRQ(USART6_IRQn);

    CADS_WIFI_UART->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_UE;
}

void USART6_IRQHandler(void) {
    uint32_t status = CADS_WIFI_UART->SR;

    if(status & (USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_PE)) {
        (void)CADS_WIFI_UART->DR; /* reading DR clears the latched error, same as USART3 */
        if(status & USART_SR_ORE) cads_wifi_rx_overruns++;
        return;
    }

    if(status & USART_SR_RXNE) {
        uint8_t byte = (uint8_t)(CADS_WIFI_UART->DR & 0xFFu);
        uint32_t next = (cads_wifi_rx_head + 1u) & CADS_WIFI_RX_MASK;
        if(next == cads_wifi_rx_tail) {
            cads_wifi_rx_dropped++;
            return;
        }
        cads_wifi_rx_buffer[cads_wifi_rx_head] = byte;
        cads_wifi_rx_head = next;
    }
}

void cads_hal_wifi_uart_write(const void* data, size_t length) {
    if((RCC->APB2ENR & RCC_APB2ENR_USART6EN) == 0u) return; /* not initialised yet */

    const uint8_t* bytes = (const uint8_t*)data;
    for(size_t i = 0; i < length; i++) {
        while(!(CADS_WIFI_UART->SR & USART_SR_TXE)) {
        }
        CADS_WIFI_UART->DR = bytes[i];
    }
    while(!(CADS_WIFI_UART->SR & USART_SR_TC)) {
    }
}

bool cads_hal_wifi_uart_read(uint8_t* byte) {
    if(cads_wifi_rx_tail == cads_wifi_rx_head) return false;
    *byte = cads_wifi_rx_buffer[cads_wifi_rx_tail];
    cads_wifi_rx_tail = (cads_wifi_rx_tail + 1u) & CADS_WIFI_RX_MASK;
    return true;
}

uint32_t cads_hal_wifi_uart_dropped(void) {
    return cads_wifi_rx_dropped;
}

uint32_t cads_hal_wifi_uart_overruns(void) {
    return cads_wifi_rx_overruns;
}
