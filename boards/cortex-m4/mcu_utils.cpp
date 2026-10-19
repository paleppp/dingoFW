#include "mcu_utils.h"
#include "hal.h"

// Just below the bootloader magic at 0x2001FFF0, at the top of RAM where the
// startup code does not clear anything
#define WAKE_RECORD_ADDR 0x2001FFDC
#define WAKE_RECORD_MAGIC 0x57414B45 // "WAKE"

struct StoredWakeRecord
{
    uint32_t nMagic;
    WakeRecord stRecord;
};

static volatile StoredWakeRecord *const pStoredWake = (volatile StoredWakeRecord *)WAKE_RECORD_ADDR;

void EnterStopMode()
{
    PWR->CR &= ~PWR_CR_PDDS;	            // cleared PDDS means stop mode (not standby) 
	PWR->CR |= PWR_CR_CWUF | PWR_CR_CSBF;	// clear wakeup flag, clear standby flag
    PWR->CR |= PWR_CR_FPDS | PWR_CR_LPDS;	// turn off flash, regulator in low power mode
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;      // enable deep sleep mode

    __disable_irq();
    
    __WFI();

    // Resume here after wakeup. Interrupts are still masked, so whatever woke
    // the MCU is still pending - note it down for the next boot.
    pStoredWake->stRecord.nExtiPending = EXTI->PR;
    for (uint8_t i = 0; i < 3; i++)
        pStoredWake->stRecord.nIrqPending[i] = NVIC->ISPR[i];
    pStoredWake->nMagic = WAKE_RECORD_MAGIC;

    NVIC_SystemReset();
}

 void RequestBootloader()
{
    // Set the magic code
    *((volatile unsigned long *)0x2001FFF0) = 0xDEADBEEF; // End of RAM

    // Reset the microcontroller to start the bootloader on next boot
    // The OpenBLT reset handler (bootloader/dingopdm) jumps to the
    // ROM USB-DFU bootloader if the magic code is set
    NVIC_SystemReset();
    
    // No further code will execute after this point
}

bool TakeWakeRecord(WakeRecord *pRecord)
{
    if (pStoredWake->nMagic != WAKE_RECORD_MAGIC)
        return false;

    pRecord->nExtiPending = pStoredWake->stRecord.nExtiPending;
    for (uint8_t i = 0; i < 3; i++)
        pRecord->nIrqPending[i] = pStoredWake->stRecord.nIrqPending[i];

    // Once only, a reset for any other reason must not report it again
    pStoredWake->nMagic = 0;
    return true;
}

void RequestBootloaderCan(uint16_t nBaseId, uint8_t nCanSpeed)
{
    // OpenBLT can't read the FRAM config: hand it the CAN settings at the top of RAM,
    // magic last (bootloader/Source/ARMCM4_STM32F4/can.c, dingopdm/hooks.c)
    volatile uint32_t *pHandoff = (volatile uint32_t *)0x2001FFF0;
    pHandoff[1] = nBaseId;
    pHandoff[2] = nCanSpeed;
    pHandoff[0] = 0xB00710AD;
    __DSB();

    NVIC_SystemReset();
}
