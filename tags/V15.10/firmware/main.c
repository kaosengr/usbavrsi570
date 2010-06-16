//************************************************************************
//**
//** Project......: Firmware USB AVR Si570 controler.
//**
//** Platform.....: ATtiny45 @ 16.5 MHz
//**
//** Licence......: This software is freely available for non-commercial 
//**                use - i.e. for research and experimentation only!
//**                Copyright: (c) 2006 by OBJECTIVE DEVELOPMENT Software GmbH
//**                Based on ObDev's AVR USB driver by Christian Starkjohann
//**
//** Programmer...: F.W. Krom, PE0FKO and
//**                thanks to Tom Baier DG8SAQ for the initial program.
//** 
//** Description..: Control the Si570 Freq. PLL chip over the USB port.
//**
//** History......: V15.1 02/12/2008: First release of PE0FKO.
//**                V15.2 19/12/2008: Change the Si570 code.
//**                V15.3 02/01/2009: Add Automatich smooth tune.
//**                V15.4 06/01/2009: Add Automatic Band Pass Filter Selection.
//**                V15.5 14/01/2009: Add the Smooth tune and band pass filter 
//**                                  to the "Set freq by Si570 registers" command.
//**                V15.6 17/01/2009: Bug fix, no connection on boot from PC.
//**                                  Used a FreqSmooth so the returned freq is
//**                                  the real freq and not the smooth center freq.
//**                V15.7 22/01/2009: Source change. Upgrade ObDev to 20081022.
//**                                  FreqSmoothTune variable removed from eeprom.
//**                                  Test errors in i2c code changed. 
//**                                  Add cmd 0x00, return firmware version number.
//**                                  Add cmd 0x20, Write Si570 register
//**                V15.8 10/02/2009: CalcFreqFromRegSi570() will use the fixed
//**                                  xtal freq of 114.285 MHz. Change static 
//**                                  variables to make some more free rom space.
//**                V15.9 17/02/2009: Disable I/O functions in case the ABPF is enabled.
//**               V15.10 18/03/2009: LO frequency subtract and multiply added.
//**                                  Add cmd 0x31, Write the frequency subtract multiply to the eeprom
//**                                  Add cmd 0x39, Return the frequency subtract multiply
//**                                  Check if the DCO freq is lower than the Si570 max.
//**                                  Include some .c files, it is smaller in size.
//**                                  Move some static variables to register, smaller code size.
//**                                  Add support for the CW Key_2 in command 0x50 & 0x51.
//**                                  CW Key always return open if ABPF is enabled (command 0x50 & 0x51)
//**
//**************************************************************************
//
//        ATtiny45
//        +--+-+--+
// !RESET |  |_|  | VCC
//    PB3 |       | PB2
//    PB4 |       | PB1
//    GND |       | PB0
//        +-------+
//
// Pin assignment:
// PB0 = USB +Data line
// PB2 = USB -Data line
// PB1 = I2C SDA to Si570
// PB3 = I2C SCL to Si570
// PB4 = user defined
// PB5 = user defined (RESET disabled by fuse RSTDISBL)
//
// Fuse bit information:
// Fuse high byte:
// 0xdd = 1 1 0 1   1 1 0 1     RSTDISBL disabled (SPI programming can be done)
// 0x5d = 0 1 0 1   1 1 0 1     RSTDISBL enabled (PB5 can be used as I/O pin)
//        ^ ^ ^ ^   ^ \-+-/ 
//        | | | |   |   +------ BODLEVEL 2..0 (brownout trigger level -> 2.7V)
//        | | | |   +---------- EESAVE (preserve EEPROM on Chip Erase -> not preserved)
//        | | | +-------------- WDTON (watchdog timer always on -> disable)
//        | | +---------------- SPIEN (enable serial programming -> enabled)
//        | +------------------ DWEN (debug wire enable)
//        +-------------------- RSTDISBL (disable external reset -> disabled)
//
// Fuse low byte:
// 0xe1 = 1 1 1 0   0 0 0 1
//        ^ ^ \+/   \--+--/
//        | |  |       +------- CKSEL 3..0 (clock selection -> HF PLL)
//        | |  +--------------- SUT 1..0 (BOD enabled, fast rising power)
//        | +------------------ CKOUT (clock output on CKOUT pin -> disabled)
//        +-------------------- CKDIV8 (divide clock by 8 -> don't divide) 
//
// Modifications by Fred Krom, PE0FKO at Nov 2008
// - Hang on no pull up of SCL line i2c to Si570 (or power down of Si590 in SR-V90)
// - Compiler (WinAVR-20071221) optimized the i2c delay loop a way!
// - Calculating the Si570 registers from a giffen frequence, returns a HIGH HS_DIV value
// - Source cleanup and split in deferend files.
// - Remove many debug USB command calls!
// - Version usbdrv-20081022
// - Add command 0x31, write only the Si570 registers (change freq max 3500ppm)
// - Change the Si570 register calculation and now use the full 38 bits of the chip!
//   Is is acurate, fast and small code! It cost only 350us (old 2ms) to calculate the new registers.
// - Add command 0x3d, Read the actual used xtal frequenty (4 bytes, 24 bits fraction, 8.24 bits)
// - Add the "automatich smooth tune" factionality.
// - Add the I/O function command 0x15
// - Add the commands 0x34, 0x35, 0x3A, 0x3B, 0x3C, 0x3D
// - Add the I/O function command 0x16
// - Add read / write Filter cross over points 0x17
// - Many code optimalization to make the small code.
// - Calculation of the freq from the Si570 registers and call 0x32, command 0x30

#include "main.h"

EEMEM	var_t		E;							// Variables in eeprom
		var_t		R							// Variables in ram
					=							// Variables in flash rom
					{	0xFF					// RC_OSCCAL
					,	DEVICE_XTAL				// FreqXtal
					,	0x03866666				// Freq at startup, 4.0 * 7.050 * _2(21)
#ifdef INCLUDE_FREQ_SM
					,	0.000 * _2(21)			// Freq subtract value is 0.0MHz (11.21bits)
					,	1.000 * _2(21)			// Freq multiply value os 1.0    (11.21bits)
#endif
#ifdef INCLUDE_SMOOTH
					,	3500					// SmoothTunePPM
#endif
#ifdef INCLUDE_ABPF
					, {	{  4.0 * 4.0 * _2(5) }	// Default filter cross over
					,	{  8.0 * 4.0 * _2(5) }	// frequnecy for softrock V9
					,	{ 16.0 * 4.0 * _2(5) }	// BPF. Four value array.
					,	{ true } }				// ABPF is default enabled
#endif
					,	DEVICE_I2C				// I2C address or ChipCrtlData
					};

				Si570_t		Si570_Data;			// Si570 register values
				sint16_t	replyBuf[4];		// USB Reply buffer

EMPTY_INTERRUPT( __vector_default );			// Redirect all unused interrupts to reti

#include "FreqFromSi570.c"						// Include code is small size

/* ------------------------------------------------------------------------- */
/* ------------------------ interface to USB driver ------------------------ */
/* ------------------------------------------------------------------------- */

uchar usbFunctionWrite(uchar *data, uchar len) //sends len bytes to SI570
{
	SWITCH_START(usbRequest)

	SWITCH_CASE(0x30)
		if (len == 6) {
			CalcFreqFromRegSi570(data);			// Calc the freq from the Si570 register value
			SetFreq(*(uint32_t*)data);			// and call the SetFreq(..) with the freq!
		}

#ifdef  INCLUDE_FREQ_SM
	SWITCH_CASE(0x31)							// Write the frequency subtract multiply to the eeprom
		if (len == 8) {
			memcpy(&R.FreqSub, data, 2*sizeof(uint32_t));
			eeprom_write_block(data, &E.FreqSub, 2*sizeof(uint32_t));
		}
#endif

	SWITCH_CASE(0x32)							// Set frequency by value and load Si570
		if (len == 4) {
			SetFreq(*(uint32_t*)data);
		}

	SWITCH_CASE(0x33)							// write new crystal frequency to EEPROM and use it.
		if (len == 4) {
			R.FreqXtal = *(uint32_t*)data;
			eeprom_write_block(data, &E.FreqXtal, sizeof(E.FreqXtal));
		}

	SWITCH_CASE(0x34)							// Write new startup frequency to eeprom
		if (len == 4) {
			eeprom_write_block(data, &E.Freq, sizeof(E.Freq));
		}

#ifdef  INCLUDE_SMOOTH
	SWITCH_CASE(0x35)							// Write new smooth tune to eeprom and use it.
		if (len == 2) {
			R.SmoothTunePPM = *(uint16_t*)data;
			eeprom_write_block(data, &E.SmoothTunePPM, sizeof(E.SmoothTunePPM));
		}
#endif

	SWITCH_END

	return 1;
}

usbMsgLen_t 
usbFunctionSetup(uchar data[8])
{
	usbRequest_t* rq = (usbRequest_t*)data;
	usbRequest = rq->bRequest;

    usbMsgPtr = (uchar*)replyBuf;
	replyBuf[0].b0 = 0xff;						// return value 0xff => command not supported 

	SWITCH_START(usbRequest)

	SWITCH_CASE(0x00)							// Return software version number
		replyBuf[0].w = (VERSION_MAJOR<<8)|(VERSION_MINOR);
		return sizeof(uint16_t);

//	SWITCH_CASE(0x00)							// ECHO value
//		replyBuf[0].w = rq->wValue.word;		// rq->bRequest identical data[1]!
//		return sizeof(uint16_t);

#ifdef  INCLUDE_NOT_USED
	SWITCH_CASE(0x01)							// set port directions
		IO_DDR = data[2] & 
		 ~((1 << USB_CFG_DMINUS_BIT) 
		 | (1 << USB_CFG_DPLUS_BIT));			// protect USB interface
		return 0;
#endif//INCLUDE_NOT_USED

	SWITCH_CASE(0x02)							// read ports (pe0fko changed)
		replyBuf[0].b0 = IO_PIN;
		return sizeof(uint8_t);

#ifdef  INCLUDE_NOT_USED
	SWITCH_CASE(0x03)							// read port states 
		replyBuf[0].b0 = IO_PORT;
		return sizeof(uint8_t);

	SWITCH_CASE(0x04)							// set ports 
#ifdef  INCLUDE_ABPF
		if (!FilterCrossOverOn)
#endif//INCLUDE_ABPF
		{
			IO_PORT = data[2] & 
			 ~((1 << USB_CFG_DMINUS_BIT) 
			 | (1 << USB_CFG_DPLUS_BIT));		// protect USB interface
		}
		return 0;
#endif//INCLUDE_NOT_USED

	SWITCH_CASE(0x0F)							// Watchdog reset
		while(true) ;

	SWITCH_CASE(0x15)							// Set IO port with mask and data bytes
#ifdef  INCLUDE_ABPF
		if (!FilterCrossOverOn)
#endif//INCLUDE_ABPF
		{	// SoftRock V9 only had 2 I/O pins from tiny45 available.
			uint8_t msk,dat;		
			msk = (rq->wValue.bytes[0] << IO_BIT_START) & (IO_BIT_MASK << IO_BIT_START);
			dat = (rq->wIndex.bytes[0] << IO_BIT_START) & (IO_BIT_MASK << IO_BIT_START);
			IO_DDR  = (IO_DDR & ~(IO_BIT_MASK << IO_BIT_START)) | msk;
			IO_PORT = (IO_PORT & ~msk) | dat;
		}
		// Return I/O pin's
		replyBuf[0].w = (IO_PIN>>IO_BIT_START) & IO_BIT_MASK;
        return sizeof(uint16_t);

	SWITCH_CASE(0x16)							// Read I/O bits
		replyBuf[0].w = (IO_PIN>>IO_BIT_START) & IO_BIT_MASK;
        return sizeof(uint16_t);

#ifdef  INCLUDE_ABPF
	SWITCH_CASE(0x17)							// Read and Write the Filter Cross over point's and use it.
		uint8_t index = rq->wIndex.bytes[0];

		if (rq->wIndex.bytes[1] == 0) {
			// RX Filter cross over point table.

			if (index < 4)
			{
				R.FilterCrossOver[index].w = rq->wValue.word;

				eeprom_write_block(&R.FilterCrossOver[index].w, 
						&E.FilterCrossOver[index].w, 
						sizeof(E.FilterCrossOver[0].w));
			}

			usbMsgPtr = (uint8_t*)&R.FilterCrossOver;
			return 4 * sizeof(uint16_t);
		}
		else {
			// TX Filter cross over point table.

			return 0;
		}
#endif//INCLUDE_ABPF

#ifdef  INCLUDE_SI570
	SWITCH_CASE(0x20)							// [DEBUG] Write byte to Si570 register
		Si570CmdReg(rq->wValue.bytes[1], rq->wIndex.bytes[0]);
#ifdef  INCLUDE_SMOOTH
		FreqSmoothTune = 0;						// Next SetFreq call no smoodtune
#endif//INCLUDE_SMOOTH
		replyBuf[0].b0 = I2CErrors;				// return I2C transmission error status
        return sizeof(uint8_t);
#endif//INCLUDE_SI570

	SWITCH_CASE6(0x30,0x31,0x32,0x33,0x34,0x35)
		//	0x30						      	// Set frequnecy by register and load Si570
		//	0x31								// Write the FREQ mul & add to the eeprom
		//	0x32								// Set frequency by value and load Si570
		//	0x33								// write new crystal frequency to EEPROM and use it.
		//	0x34								// Write new startup frequency to eeprom
		//	0x35								// Write new smooth tune to eeprom and use it.
		return USB_NO_MSG;						// use usbFunctionWrite to transfer data

#ifdef  INCLUDE_FREQ_SM
	SWITCH_CASE(0x39)							// Return the frequency subtract multiply
		usbMsgPtr = (uint8_t*)&R.FreqSub;
        return 2 * sizeof(uint32_t);
#endif//INCLUDE_FREQ_SM

	SWITCH_CASE(0x3A)							// Return running frequnecy
		usbMsgPtr = (uint8_t*)&R.Freq;
        return sizeof(uint32_t);

#ifdef  INCLUDE_SMOOTH
	SWITCH_CASE(0x3B)							// Return smooth tune ppm value
		usbMsgPtr = (uint8_t*)&R.SmoothTunePPM;
        return sizeof(uint16_t);
#endif//INCLUDE_SMOOTH

	SWITCH_CASE(0x3C)							// Return the startup frequency
		eeprom_read_block(replyBuf, &E.Freq, sizeof(E.Freq));
		return sizeof(uint32_t);

	SWITCH_CASE(0x3D)							// Return the XTal frequnecy
		usbMsgPtr = (uint8_t*)&R.FreqXtal;
        return sizeof(uint32_t);

//	SWITCH_CASE(0x3E)							// read out calculated frequency control registers
//		usbMsgPtr = (uint8_t*)&Si570_Data;
//		return sizeof(Si570_t);

	SWITCH_CASE(0x3F)							// read out chip frequency control registers
		return GetRegFromSi570();				// read all registers in one block to replyBuf[]

#ifdef  INCLUDE_I2C
	SWITCH_CASE(0x40)							// return I2C transmission error status
		replyBuf[0].b0 = I2CErrors;
		return sizeof(uint8_t);
#endif//INCLUDE_I2C

	SWITCH_CASE(0x41)							// Set the new i2c address or factory default (pe0fko: function changed)
		if (rq->wValue.bytes[0] != 0xFF)
			R.ChipCrtlData = rq->wValue.bytes[0];
		eeprom_write_byte(&E.ChipCrtlData, rq->wValue.bytes[0]);
        return 0;

	SWITCH_CASE2(0x50,0x51)						// set IO_P1 (cmd=0x50) and read CW key level (cmd=0x50 & 0x51)
		replyBuf[0].b0 = (_BV(IO_P2) | _BV(BIT_SDA));	// CW Key 1 (PB4) & 2 (PB1 + i2c SDA)
#ifdef  INCLUDE_ABPF
		if (!FilterCrossOverOn)
#endif//INCLUDE_ABPF
		{
			if (usbRequest == 0x50)
			{
			    if (rq->wValue.bytes[0] == 0)
					bit_0(IO_PORT, IO_P1);
				else
					bit_1(IO_PORT, IO_P1);
			}

			replyBuf[0].b0 &= IO_PIN;
		}
        return sizeof(uint8_t);

	SWITCH_END

    return 1;
}


// This function is neded, otherwise the USB device is not
// recognized after a reboot.
// The watchdog will need to be reset (<16ms). Fast (div 2K) prescaler after watchdog reset!
// MCUSR must be cleared (datasheet) it is not done within the wdt_disable().
void __attribute__((naked))
     __attribute__((section(".init3")))
dotInit3(void)
{
	MCUSR = 0;
	wdt_disable();

	IO_DDR = _BV(IO_P1);		// All port pins inputs except IO_P1 switching output
	IO_PORT = 0;				// Inp on startup, no pullups
}


/* ------------------------------------------------------------------------- */
/* --------------------------------- main ---------------------------------- */
/* ------------------------------------------------------------------------- */

int __attribute__((noreturn)) 
main(void)
{
	// Check if eeprom is initialized, use only the field ChipCrtlData.
	if (eeprom_read_byte(&E.ChipCrtlData) == 0xFF)
		eeprom_write_block(&R, &E, sizeof(E));	// Initialize eeprom to "factory defaults".
	else
		eeprom_read_block(&R, &E, sizeof(E));	// Load the persistend data from eeprom.

	if(R.RC_OSCCAL != 0xFF)
		OSCCAL = R.RC_OSCCAL;

	SI570_OffLine = true;						// Si570 is offline, not initialized

	DeviceInit();								// Initialize the Si570 device.

	usbDeviceDisconnect();						// Start USB enumeration
	_delay_ms(500);
	usbDeviceConnect();

	wdt_enable(WDTO_250MS);						// Watchdog 250ms

	usbInit();									// Init the USB used ports

	sei();										// Enable interupts

	while(true)
	{
	    wdt_reset();
	    usbPoll();

#ifdef  INCLUDE_SI570
		DeviceInit();
#endif//INCLUDE_SI570
	}
}

/*
Compile WinAVR error 20081205 version:
d:/winavr-20081205/bin/../lib/gcc/avr/4.3.2/../../../../avr/lib/avr25/crttn45.o:(.init9+0x2): 
relocation truncated to fit: R_AVR_13_PCREL against symbol `exit' defined in .fini9 section in 
d:/winavr-20081205/bin/../lib/gcc/avr/4.3.2/avr25\libgcc.a(_exit.o)
*/


/*
 * Compiler: WinAVR-20071221
 * V14		3866 bytes (94.4% Full)
 * V15.1	3856 bytes (94.1% Full)
 * V15.2	3482 bytes (85.0% Full)
 * V15.3	3892 bytes (95.0% Full)
 * V15.4	3918 bytes (95.7% Full)
 * V15.5	4044 bytes (98.7% Full)
 * V15.6	4072 bytes (99.4% Full)
 * V15.7	4090 bytes (99.9% Full)
 * V15.8	3984 bytes (97.3% Full)
 * V15.9	3984 bytes (97.3% Full)
 * V15.10	4018 bytes (98.1% Full)
 */