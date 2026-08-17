/* GENERATED FILE - do not edit.
 * Regenerate with: scripts/gen_vectors.py lib/cmsis_device_f4/Include/stm32f429xx.h targets/itsboard/startup/vectors_stm32f429.c
 *
 * Cortex-M4 system exceptions plus the 90 device interrupts of the STM32F429,
 * derived from IRQn_Type in the CMSIS device header.
 */

#include <stdint.h>

extern uint32_t __cads_stack_top;

void Reset_Handler(void);

/*
 * GCC requires the target of an alias to be defined in the same translation
 * unit, so Default_Handler lives here rather than next to Reset_Handler.
 *
 * Halting rather than resetting is deliberate: an unexpected interrupt leaves
 * the machine intact for the attached ST-Link, and IPSR still names the vector
 * that fired.
 */
__attribute__((noreturn)) void Default_Handler(void) {
    __asm volatile("bkpt #0" ::: "memory");
    for(;;) {
    }
}

/* Every handler is a weak alias of Default_Handler; a driver that
 * defines the real symbol overrides it at link time. */
void NMI_Handler(void) __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void MemManage_Handler(void) __attribute__((weak, alias("Default_Handler")));
void BusFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void UsageFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void) __attribute__((weak, alias("Default_Handler")));
void DebugMon_Handler(void) __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void) __attribute__((weak, alias("Default_Handler")));

void WWDG_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void PVD_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TAMP_STAMP_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void RTC_WKUP_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void FLASH_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void RCC_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void EXTI0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void EXTI1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void EXTI2_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void EXTI3_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void EXTI4_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA1_Stream0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA1_Stream1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA1_Stream2_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA1_Stream3_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA1_Stream4_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA1_Stream5_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA1_Stream6_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void ADC_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void CAN1_TX_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void CAN1_RX0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void CAN1_RX1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void CAN1_SCE_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void EXTI9_5_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM1_BRK_TIM9_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM1_UP_TIM10_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM1_TRG_COM_TIM11_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM1_CC_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM2_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM3_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM4_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void I2C1_EV_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void I2C1_ER_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void I2C2_EV_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void I2C2_ER_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void SPI1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void SPI2_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void USART1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void USART2_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void USART3_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void EXTI15_10_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void RTC_Alarm_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void OTG_FS_WKUP_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM8_BRK_TIM12_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM8_UP_TIM13_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM8_TRG_COM_TIM14_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM8_CC_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA1_Stream7_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void FMC_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void SDIO_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM5_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void SPI3_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void UART4_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void UART5_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM6_DAC_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM7_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA2_Stream0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA2_Stream1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA2_Stream2_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA2_Stream3_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA2_Stream4_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void ETH_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void ETH_WKUP_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void CAN2_TX_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void CAN2_RX0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void CAN2_RX1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void CAN2_SCE_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void OTG_FS_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA2_Stream5_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA2_Stream6_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA2_Stream7_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void USART6_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void I2C3_EV_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void I2C3_ER_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void OTG_HS_EP1_OUT_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void OTG_HS_EP1_IN_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void OTG_HS_WKUP_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void OTG_HS_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DCMI_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void HASH_RNG_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void FPU_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void UART7_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void UART8_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void SPI4_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void SPI5_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void SPI6_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void SAI1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void LTDC_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void LTDC_ER_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA2D_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));

typedef void (*cads_vector_t)(void);

__attribute__((section(".isr_vector"), used))
const cads_vector_t cads_vector_table[] = {
    (cads_vector_t)(&__cads_stack_top),
    Reset_Handler,
    NMI_Handler,  /*   2 */
    HardFault_Handler,  /*   3 */
    MemManage_Handler,  /*   4 */
    BusFault_Handler,  /*   5 */
    UsageFault_Handler,  /*   6 */
    0,  /*   7 */
    0,  /*   8 */
    0,  /*   9 */
    0,  /*  10 */
    SVC_Handler,  /*  11 */
    DebugMon_Handler,  /*  12 */
    0,  /*  13 */
    PendSV_Handler,  /*  14 */
    SysTick_Handler,  /*  15 */
    WWDG_IRQHandler,  /*  16  IRQ  0  WWDG */
    PVD_IRQHandler,  /*  17  IRQ  1  PVD */
    TAMP_STAMP_IRQHandler,  /*  18  IRQ  2  TAMP_STAMP */
    RTC_WKUP_IRQHandler,  /*  19  IRQ  3  RTC_WKUP */
    FLASH_IRQHandler,  /*  20  IRQ  4  FLASH */
    RCC_IRQHandler,  /*  21  IRQ  5  RCC */
    EXTI0_IRQHandler,  /*  22  IRQ  6  EXTI0 */
    EXTI1_IRQHandler,  /*  23  IRQ  7  EXTI1 */
    EXTI2_IRQHandler,  /*  24  IRQ  8  EXTI2 */
    EXTI3_IRQHandler,  /*  25  IRQ  9  EXTI3 */
    EXTI4_IRQHandler,  /*  26  IRQ 10  EXTI4 */
    DMA1_Stream0_IRQHandler,  /*  27  IRQ 11  DMA1_Stream0 */
    DMA1_Stream1_IRQHandler,  /*  28  IRQ 12  DMA1_Stream1 */
    DMA1_Stream2_IRQHandler,  /*  29  IRQ 13  DMA1_Stream2 */
    DMA1_Stream3_IRQHandler,  /*  30  IRQ 14  DMA1_Stream3 */
    DMA1_Stream4_IRQHandler,  /*  31  IRQ 15  DMA1_Stream4 */
    DMA1_Stream5_IRQHandler,  /*  32  IRQ 16  DMA1_Stream5 */
    DMA1_Stream6_IRQHandler,  /*  33  IRQ 17  DMA1_Stream6 */
    ADC_IRQHandler,  /*  34  IRQ 18  ADC */
    CAN1_TX_IRQHandler,  /*  35  IRQ 19  CAN1_TX */
    CAN1_RX0_IRQHandler,  /*  36  IRQ 20  CAN1_RX0 */
    CAN1_RX1_IRQHandler,  /*  37  IRQ 21  CAN1_RX1 */
    CAN1_SCE_IRQHandler,  /*  38  IRQ 22  CAN1_SCE */
    EXTI9_5_IRQHandler,  /*  39  IRQ 23  EXTI9_5 */
    TIM1_BRK_TIM9_IRQHandler,  /*  40  IRQ 24  TIM1_BRK_TIM9 */
    TIM1_UP_TIM10_IRQHandler,  /*  41  IRQ 25  TIM1_UP_TIM10 */
    TIM1_TRG_COM_TIM11_IRQHandler,  /*  42  IRQ 26  TIM1_TRG_COM_TIM11 */
    TIM1_CC_IRQHandler,  /*  43  IRQ 27  TIM1_CC */
    TIM2_IRQHandler,  /*  44  IRQ 28  TIM2 */
    TIM3_IRQHandler,  /*  45  IRQ 29  TIM3 */
    TIM4_IRQHandler,  /*  46  IRQ 30  TIM4 */
    I2C1_EV_IRQHandler,  /*  47  IRQ 31  I2C1_EV */
    I2C1_ER_IRQHandler,  /*  48  IRQ 32  I2C1_ER */
    I2C2_EV_IRQHandler,  /*  49  IRQ 33  I2C2_EV */
    I2C2_ER_IRQHandler,  /*  50  IRQ 34  I2C2_ER */
    SPI1_IRQHandler,  /*  51  IRQ 35  SPI1 */
    SPI2_IRQHandler,  /*  52  IRQ 36  SPI2 */
    USART1_IRQHandler,  /*  53  IRQ 37  USART1 */
    USART2_IRQHandler,  /*  54  IRQ 38  USART2 */
    USART3_IRQHandler,  /*  55  IRQ 39  USART3 */
    EXTI15_10_IRQHandler,  /*  56  IRQ 40  EXTI15_10 */
    RTC_Alarm_IRQHandler,  /*  57  IRQ 41  RTC_Alarm */
    OTG_FS_WKUP_IRQHandler,  /*  58  IRQ 42  OTG_FS_WKUP */
    TIM8_BRK_TIM12_IRQHandler,  /*  59  IRQ 43  TIM8_BRK_TIM12 */
    TIM8_UP_TIM13_IRQHandler,  /*  60  IRQ 44  TIM8_UP_TIM13 */
    TIM8_TRG_COM_TIM14_IRQHandler,  /*  61  IRQ 45  TIM8_TRG_COM_TIM14 */
    TIM8_CC_IRQHandler,  /*  62  IRQ 46  TIM8_CC */
    DMA1_Stream7_IRQHandler,  /*  63  IRQ 47  DMA1_Stream7 */
    FMC_IRQHandler,  /*  64  IRQ 48  FMC */
    SDIO_IRQHandler,  /*  65  IRQ 49  SDIO */
    TIM5_IRQHandler,  /*  66  IRQ 50  TIM5 */
    SPI3_IRQHandler,  /*  67  IRQ 51  SPI3 */
    UART4_IRQHandler,  /*  68  IRQ 52  UART4 */
    UART5_IRQHandler,  /*  69  IRQ 53  UART5 */
    TIM6_DAC_IRQHandler,  /*  70  IRQ 54  TIM6_DAC */
    TIM7_IRQHandler,  /*  71  IRQ 55  TIM7 */
    DMA2_Stream0_IRQHandler,  /*  72  IRQ 56  DMA2_Stream0 */
    DMA2_Stream1_IRQHandler,  /*  73  IRQ 57  DMA2_Stream1 */
    DMA2_Stream2_IRQHandler,  /*  74  IRQ 58  DMA2_Stream2 */
    DMA2_Stream3_IRQHandler,  /*  75  IRQ 59  DMA2_Stream3 */
    DMA2_Stream4_IRQHandler,  /*  76  IRQ 60  DMA2_Stream4 */
    ETH_IRQHandler,  /*  77  IRQ 61  ETH */
    ETH_WKUP_IRQHandler,  /*  78  IRQ 62  ETH_WKUP */
    CAN2_TX_IRQHandler,  /*  79  IRQ 63  CAN2_TX */
    CAN2_RX0_IRQHandler,  /*  80  IRQ 64  CAN2_RX0 */
    CAN2_RX1_IRQHandler,  /*  81  IRQ 65  CAN2_RX1 */
    CAN2_SCE_IRQHandler,  /*  82  IRQ 66  CAN2_SCE */
    OTG_FS_IRQHandler,  /*  83  IRQ 67  OTG_FS */
    DMA2_Stream5_IRQHandler,  /*  84  IRQ 68  DMA2_Stream5 */
    DMA2_Stream6_IRQHandler,  /*  85  IRQ 69  DMA2_Stream6 */
    DMA2_Stream7_IRQHandler,  /*  86  IRQ 70  DMA2_Stream7 */
    USART6_IRQHandler,  /*  87  IRQ 71  USART6 */
    I2C3_EV_IRQHandler,  /*  88  IRQ 72  I2C3_EV */
    I2C3_ER_IRQHandler,  /*  89  IRQ 73  I2C3_ER */
    OTG_HS_EP1_OUT_IRQHandler,  /*  90  IRQ 74  OTG_HS_EP1_OUT */
    OTG_HS_EP1_IN_IRQHandler,  /*  91  IRQ 75  OTG_HS_EP1_IN */
    OTG_HS_WKUP_IRQHandler,  /*  92  IRQ 76  OTG_HS_WKUP */
    OTG_HS_IRQHandler,  /*  93  IRQ 77  OTG_HS */
    DCMI_IRQHandler,  /*  94  IRQ 78  DCMI */
    0,  /*  95  IRQ 79  reserved */
    HASH_RNG_IRQHandler,  /*  96  IRQ 80  HASH_RNG */
    FPU_IRQHandler,  /*  97  IRQ 81  FPU */
    UART7_IRQHandler,  /*  98  IRQ 82  UART7 */
    UART8_IRQHandler,  /*  99  IRQ 83  UART8 */
    SPI4_IRQHandler,  /* 100  IRQ 84  SPI4 */
    SPI5_IRQHandler,  /* 101  IRQ 85  SPI5 */
    SPI6_IRQHandler,  /* 102  IRQ 86  SPI6 */
    SAI1_IRQHandler,  /* 103  IRQ 87  SAI1 */
    LTDC_IRQHandler,  /* 104  IRQ 88  LTDC */
    LTDC_ER_IRQHandler,  /* 105  IRQ 89  LTDC_ER */
    DMA2D_IRQHandler,  /* 106  IRQ 90  DMA2D */
};
