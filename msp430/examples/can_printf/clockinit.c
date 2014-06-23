/* Initialize the UCS clock in the F5172
 */

#include <msp430.h>
#include <stdint.h>
#include "vcore.h"

uint16_t _dcorsel_compute_f5172(unsigned long);
uint16_t _flld_compute(unsigned long);

uint8_t ucs_clockinit(unsigned long freq, uint8_t use_xt1, uint8_t vlo_as_aclk)
{
	unsigned long attempts = 0; //, divided;
	uint16_t flld;
	static uint8_t did_vcoreup = 0;

	UCSCTL4 = SELM__DCOCLK | SELS__DCOCLK;
	if (vlo_as_aclk)
		UCSCTL4 = (UCSCTL4 & ~SELA_7) | SELA__VLOCLK;

	if (use_xt1) {

		#ifdef __MSP430F5172
		PJSEL |= BIT4|BIT5;
		#endif
		#ifdef __MSP430F5529
		P5SEL |= BIT4|BIT5;
		#endif

		UCSCTL6 &= ~XT1OFF;
		UCSCTL6 = (UCSCTL6 & ~(XCAP_3|XT1DRIVE_3)) | XCAP_0 | XT1DRIVE_3;
		if (!vlo_as_aclk)
			UCSCTL4 = (UCSCTL4 & ~SELA_7) | SELA__XT1CLK;
		// Wait for XT1 to stabilize
		do {
			UCSCTL7 &= ~XT1LFOFFG;
			attempts++;
		} while (UCSCTL7 & XT1LFOFFG && attempts < 1000000);
		if (attempts == 1000000)
			return 0;  // XT1 FAILED
		UCSCTL3 = SELREF__XT1CLK;
	} else {
		UCSCTL6 |= XT1OFF;
		#ifdef XT1HFOFFG
		UCSCTL7 &= ~(XT1LFOFFG | XT1HFOFFG);
		#else
		UCSCTL7 &= ~XT1LFOFFG;
		#endif
		UCSCTL3 = SELREF__REFOCLK;
	}

	// Using frequency, determine which VCore level we should achieve.
	// Set Vcore to maximum
	if (!did_vcoreup) {
		SetVCoreUp(1);
		SetVCoreUp(2);
		SetVCoreUp(3);
		did_vcoreup = 1;
	}

	// Initialize DCO
	__bis_SR_register(SCG0);  // Disable FLL control loop
	UCSCTL0 = 0x0000;

	// Determine which DCORSEL we should use
	UCSCTL1 = _dcorsel_compute_f5172(freq);

	// FLL reference is 32768Hz, determine multiplier
	flld = _flld_compute(freq);
	UCSCTL2 = ((flld/2) << 12) | (uint16_t)(freq / 32768UL / flld);

	__bic_SR_register(SCG0);  // Re-enable FLL control loop

	// Loop until XT1 & DCO fault flags have cleared
	do {
		#ifdef XT1HFOFFG
		UCSCTL7 &= ~(XT1LFOFFG | XT1HFOFFG | DCOFFG);
		#else
		UCSCTL7 &= ~(XT1LFOFFG | DCOFFG);
		#endif
		SFRIFG1 &= ~OFIFG;
	} while (SFRIFG1 & OFIFG);

	// DCOCLK stable
	return 1;
}

inline uint16_t _dcorsel_compute_f5172(unsigned long freq)
{
	if (freq <= 1000000)
		return DCORSEL_0;
	if (freq <= 2000000)
		return DCORSEL_1;
	if (freq <= 6000000)
		return DCORSEL_2;
	if (freq <= 10000000)
		return DCORSEL_3;
	if (freq <= 16000000)
		return DCORSEL_4;
	if (freq <= 20000000)
		return DCORSEL_5;
	if (freq <= 25000000)
		return DCORSEL_6;
	if (freq <= 130000000)  // This should never be reached ;)
		return DCORSEL_7;
	return 0;
}

inline uint16_t _flld_compute(unsigned long freq)
{
	if (freq <= 32000000)
		return 1;
	if (freq <= 64000000)
		return 2;
	if (freq <= 128000000)
		return 3;
	return 1;
}
