/* Name: main.c
 * Project: atmega8-magnetometer-usb-mouse
 * Author: Denilson Figueiredo de Sa
 * Creation Date: 2011-08-29
 * Tabsize: 4
 * License: GNU GPL v2 or GNU GPL v3
 *
 * Includes third-party code:
 *
 * - V-USB from OBJECTIVE DEVELOPMENT Software GmbH
 *   http://www.obdev.at/products/vusb/index.html
 *
 * - USBaspLoader from OBJECTIVE DEVELOPMENT Software GmbH
 *   http://www.obdev.at/products/vusb/usbasploader.html
 *
 * - AVR315 TWI Master Implementation from Atmel
 *   http://www.atmel.com/dyn/products/documents.asp?category_id=163&family_id=607&subfamily_id=760
 */

// Headers from AVR-Libc
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>
#include <util/delay.h>

// V-USB driver from http://www.obdev.at/products/vusb/
#include "usbdrv.h"

// It's also possible to include "usbdrv.c" directly, if we also add
// this definition at the top of this file:
// #define USB_PUBLIC static
// However, this only saved 10 bytes.

// I'm not using serial-line debugging
//#include "oddebug.h"

// AVR315 Using the TWI module as I2C master
#include "avr315/TWI_Master.h"

// Non-blocking interrupt-based EEPROM writing.
#include "int_eeprom.h"

// Sensor communication over I2C (TWI)
#include "sensor.h"

// Button handling code
#include "buttons.h"


#if ENABLE_KEYBOARD

// Keyboard emulation code
#include "keyemu.h"
// Menu user interface for configuring the device
#include "menu.h"

#endif


#if ENABLE_MOUSE

// Mouse emulation code
#include "mouseemu.h"

#endif


////////////////////////////////////////////////////////////
// Hardware description                                  {{{

/* ATmega8 pin assignments:
 * PB0: (not used)
 * PB1: (not used)
 * PB2: (not used)
 * PB3: (not used - MOSI)
 * PB4: (not used - MISO)
 * PB5: (not used - SCK)
 * PB6: 12MHz crystal
 * PB7: 12MHz crystal
 *
 * PC0: Button 1
 * PC1: Button 2
 * PC2: Button 3
 * PC3: Switch button (if held to GND during power-on, starts the bootloader)
 * PC4: I2C - SDA
 * PC5: I2C - SCL
 * PC6: Reset pin (with an external 10K pull-up to VCC)
 *
 * PD0: USB-
 * PD1: (not used - debug tx)
 * PD2: USB+ (int0)
 * PD3: (not used)
 * PD4: (not used)
 * PD5: red debug LED
 * PD6: yellow debug LED
 * PD7: green debug LED
 *
 * If you change the ports, remember to update:
 * - main.c: hardware_init()
 * - buttons.c: update_button_state()
 * - buttons.h and menu.c: BUTTON_* definitions
 * - mouseemu.c: mouse_update_buttons()
 *
 * If the Switch is ON, runs the "mouseemu" code. In this mode, buttons 1,
 * 2 and 3 emulate mouse click.
 * If the Switch is OFF, runs the "keyemu" code. In this mode, there is an
 * interactive menu system to configure and debug the sensor. Buttons 1 and
 * 2 are next/prev item, and button 3 is "confirm".
 */

#define LED_TURN_ON(led)  do { PORTD |=  (led); } while(0)
#define LED_TURN_OFF(led) do { PORTD &= ~(led); } while(0)
#define LED_TOGGLE(led)   do { PORTD ^=  (led); } while(0)

// Bit masks for each LED (in PORTD)
#define RED_LED    (1 << 5)
#define YELLOW_LED (1 << 6)
#define GREEN_LED  (1 << 7)
#define ALL_LEDS   (GREEN_LED | YELLOW_LED | RED_LED)

// }}}

////////////////////////////////////////////////////////////
// USB HID Report Descriptor                             {{{

// If this HID report descriptor is changed, remember to update
// USB_CFG_HID_REPORT_DESCRIPTOR_LENGTH from usbconfig.h
PROGMEM const char usbHidReportDescriptor[USB_CFG_HID_REPORT_DESCRIPTOR_LENGTH]
__attribute__((externally_visible))
= {
	// Keyboard
	0x05, 0x01,              // USAGE_PAGE (Generic Desktop)
	0x09, 0x06,              // USAGE (Keyboard)
	0xa1, 0x01,              // COLLECTION (Application)
	0x85, 0x01,              //   REPORT_ID (1)
	// Modifier keys (they must come BEFORE the real keys)
	0x05, 0x07,              //   USAGE_PAGE (Keyboard)
	0x19, 0xe0,              //   USAGE_MINIMUM (Keyboard LeftControl)
	0x29, 0xe7,              //   USAGE_MAXIMUM (Keyboard Right GUI)
	0x15, 0x00,              //   LOGICAL_MINIMUM (0)
	0x25, 0x01,              //   LOGICAL_MAXIMUM (1)
	0x75, 0x01,              //   REPORT_SIZE (1)
	0x95, 0x08,              //   REPORT_COUNT (8)
	0x81, 0x02,              //   INPUT (Data,Var,Abs)
	// Normal keys
//	0x05, 0x07,              //   USAGE_PAGE (Keyboard)
	0x19, 0x00,              //   USAGE_MINIMUM (Reserved (no event indicated))
	0x29, 0x65,              //   USAGE_MAXIMUM (Keyboard Application)
//	0x15, 0x00,              //   LOGICAL_MINIMUM (0)
	0x25, 0x65,              //   LOGICAL_MAXIMUM (101)
	0x75, 0x08,              //   REPORT_SIZE (8)
	0x95, 0x01,              //   REPORT_COUNT (1)
	0x81, 0x00,              //   INPUT (Data,Ary,Abs)
	0xc0,                    // END_COLLECTION

	// Mouse
	0x05, 0x01,              // USAGE_PAGE (Generic Desktop)
	0x09, 0x02,              // USAGE (Mouse)
	0xa1, 0x01,              // COLLECTION (Application)
	0x85, 0x02,              //   REPORT_ID (2)
//	0x05, 0x01,              //   USAGE_PAGE (Generic Desktop)
	0x09, 0x01,              //   USAGE (Pointer)
	0xa1, 0x00,              //   COLLECTION (Physical)
	// X, Y movement
//	0x05, 0x01,              //     USAGE_PAGE (Generic Desktop)
	0x09, 0x30,              //     USAGE (X)
	0x09, 0x31,              //     USAGE (Y)
//	0x15, 0x00,              //     LOGICAL_MINIMUM (0)
	0x26, 0xff, 0x7f,        //     LOGICAL_MAXIMUM (32767)
//	0x35, 0x00,              //     PHYSICAL_MINIMUM (0)
//	0x46, 0xff, 0x7f,        //     PHYSICAL_MAXIMUM (32767)
	0x75, 0x10,              //     REPORT_SIZE (16)
	0x95, 0x02,              //     REPORT_COUNT (2)
	0x81, 0x42,              //     INPUT (Data,Var,Abs,Null)
	0xc0,                    //   END_COLLECTION
	// Buttons
	0x05, 0x09,              //   USAGE_PAGE (Button)
	0x19, 0x01,              //   USAGE_MINIMUM (Button 1)
	0x29, 0x03,              //   USAGE_MAXIMUM (Button 3)
//	0x15, 0x00,              //   LOGICAL_MINIMUM (0)
	0x25, 0x01,              //   LOGICAL_MAXIMUM (1)
	0x75, 0x01,              //   REPORT_SIZE (1)
	0x95, 0x03,              //   REPORT_COUNT (3)
	0x81, 0x02,              //   INPUT (Data,Var,Abs)
	// Padding for the buttons
//	0x75, 0x01,              //   REPORT_SIZE (1)
	0x95, 0x05,              //   REPORT_COUNT (5)
	0x81, 0x03,              //   INPUT (Cnst,Var,Abs)
	0xc0                     // END_COLLECTION
};

// This device does not support BOOT protocol from HID specification.
//
// The keyboard portion is very limited, when compared to actual keyboards,
// but it's perfect for a simple communication from the firmware to the
// user.  It supports the common keyboard modifiers (although the firmware
// only uses the left shift), and supports only one key at time. This is
// enough for writing the "menu" interface, and uses only 2 bytes (plus the
// report ID).
//
// The mouse portion is actually an absolute pointing device, and not a
// standard mouse (that instead sends relative movements). It supports 2
// axes (X and Y) with 16-bit for each one, although it doesn't use the
// full 16-bit range. It also has 3 buttons. That means 2+2+1=5 bytes for
// the report (plus 1 byte for the report ID).
//
// Redundant entries (such as LOGICAL_MINIMUM and USAGE_PAGE) have been
// commented out where possible, in order to save a few bytes.
//
// PHYSICAL_MINIMUM and PHYSICAL_MAXIMUM, when undefined, assume the same
// values as LOGICAL_MINIMUM and LOGICAL_MAXIMUM.
//
// Note about where the buttons are located in the Report Descriptor:
// The buttons are placed outside the "Physical" collection just because in
// this project the buttons are on the breadboard, while the sensor is more
// than one meter away from the buttons.
// However, putting these buttons inside or outside that collection makes
// no difference at all for the software, feel free to move them around.
//
// Also note that ENABLE_KEYBOARD and ENABLE_MOUSE options don't change the
// HID Descriptor. Instead, they only enable/disable the code that
// implements the keyboard or the mouse.

// }}}

////////////////////////////////////////////////////////////
// Main code                                             {{{

// Disabling idle rate because it is useless here. And because it saves quite
// a few bytes.
#define ENABLE_IDLE_RATE 0

#if ENABLE_IDLE_RATE
// As defined in section 7.2.4 Set_Idle Request
// of Device Class Definition for Human Interface Devices (HID) version
// 1.11 pages 52 and 53 (or 62 and 63) of HID1_11.pdf
//
// Set/Get IDLE defines how long the device should keep "quiet" if the
// state has not changed.
// Recommended default value for keyboard is 500ms, and infinity for
// joystick and mice.
//
// This value is measured in multiples of 4ms.
// A value of zero means indefinite/infinity.
static uchar idle_rate;
#endif

static void hardware_init(void) {  // {{{
	// Configuring Watchdog to about 2 seconds
	// See pages 43 and 44 from ATmega8 datasheet
	// See also http://www.nongnu.org/avr-libc/user-manual/group__avr__watchdog.html
	wdt_enable(WDTO_2S);

	PORTB = 0xff;  // activate all pull-ups
	DDRB = 0;      // all pins input
	PORTC = 0xff;  // activate all pull-ups
	DDRC = 0;      // all pins input

	// From usbdrv.h:
	//#define USBMASK ((1<<USB_CFG_DPLUS_BIT) | (1<<USB_CFG_DMINUS_BIT))

	// activate pull-ups, except on USB lines and LED pins
	PORTD = 0xFF ^ (USBMASK | ALL_LEDS);
	// LED pins as output, the other pins as input
	DDRD = 0 | ALL_LEDS;

	// Doing a USB reset
	// This is done here because the device might have been reset
	// by the watchdog or some condition other than power-up.
	//
	// A reset is done by holding both D+ and D- low (setting the
	// pins as output with value zero) for longer than 10ms.
	//
	// See page 145 of usb_20.pdf
	// See also http://www.beyondlogic.org/usbnutshell/usb2.shtml

	DDRD |= USBMASK;    // Setting as output
	PORTD &= ~USBMASK;  // Setting as zero

	_delay_ms(15);  // Holding this state for at least 10ms

	DDRD &= ~USBMASK;   // Setting as input
	//PORTD &= ~USBMASK;  // Pull-ups are already disabled

	// End of USB reset

	// Disabling Timer0 Interrupt
	// It's disabled by default, anyway, so this shouldn't be needed
	TIMSK &= ~(TOIE0);

	// Configuring Timer0 (with main clock at 12MHz)
	// 0 = No clock (timer stopped)
	// 1 = Prescaler = 1     =>   0.0213333ms
	// 2 = Prescaler = 8     =>   0.1706666ms
	// 3 = Prescaler = 64    =>   1.3653333ms
	// 4 = Prescaler = 256   =>   5.4613333ms
	// 5 = Prescaler = 1024  =>  21.8453333ms
	// 6 = External clock source on T0 pin (falling edge)
	// 7 = External clock source on T0 pin (rising edge)
	// See page 72 from ATmega8 datasheet.
	// Also thanks to http://frank.circleofcurrent.com/cache/avrtimercalc.htm
	TCCR0 = 3;

	// I'm using Timer0 as a 1.365ms ticker. Every time it overflows, the TOV0
	// flag in TIFR is set.

	// I'm not using serial-line debugging
	//odDebugInit();

	LED_TURN_ON(YELLOW_LED);
}  // }}}


uchar
__attribute__((externally_visible))
usbFunctionSetup(uchar data[8]) {  // {{{
	usbRequest_t *rq = (void *)data;

	if ((rq->bmRequestType & USBRQ_TYPE_MASK) == USBRQ_TYPE_CLASS) {
		// class request type

		if (rq->bRequest == USBRQ_HID_GET_REPORT){
			// wValue: ReportType (highbyte), ReportID (lowbyte)
			// we only have one report type, so don't look at wValue

			// This seems to be called as one of the final initialization
			// steps of the device, after the ReportDescriptor has been sent.
			// Returns the initial state of the device.
			LED_TURN_OFF(GREEN_LED);

#if ENABLE_KEYBOARD
			if (rq->wValue.bytes[0] == 1) {
				// Keyboard report

				// Not needed as I the struct already has sane values
				// build_report_from_char('\0');

				usbMsgPtr = (void*) &keyboard_report;
				return sizeof(keyboard_report);
			}
#endif

#if ENABLE_MOUSE
			if (rq->wValue.bytes[0] == 2) {
				// Mouse report
				usbMsgPtr = (void*) &mouse_report;
				return sizeof(mouse_report);
			}
#endif

#if ENABLE_IDLE_RATE
		} else if (rq->bRequest == USBRQ_HID_GET_IDLE) {
			usbMsgPtr = &idle_rate;
			return 1;

		} else if (rq->bRequest == USBRQ_HID_SET_IDLE) {
			idle_rate = rq->wValue.bytes[1];
#endif
		}

	} else {
		/* no vendor specific requests implemented */
	}
	return 0;
}  // }}}


void
__attribute__ ((noreturn))
main(void) {  // {{{
	uchar sensor_probe_counter = 0;
	uchar timer_overflow = 0;

#if ENABLE_IDLE_RATE
	int idle_counter = 0;
#endif

	cli();

	hardware_init();

#if ENABLE_KEYBOARD
	init_keyboard_emulation();
	init_ui_system();
#endif
#if ENABLE_MOUSE
	init_mouse_emulation();
#endif

	TWI_Master_Initialise();
	usbInit();
	init_int_eeprom();
	init_button_state();

	wdt_reset();
	sei();

	// Sensor initialization must be done with interrupts enabled!
	// It uses I2C (TWI) to configure the sensor.
	sensor_init_configuration();

	LED_TURN_ON(GREEN_LED);

	for (;;) {	// main event loop
		wdt_reset();
		usbPoll();

		if (TIFR & (1<<TOV0)) {
			timer_overflow = 1;

			// Resetting the Timer0
			// Setting this bit to one will clear it.
			TIFR = 1<<TOV0;
		} else {
			timer_overflow = 0;
		}

		update_button_state(timer_overflow);

		// Red LED lights up if there is any kind of error in I2C communication
		if ( TWI_statusReg.lastTransOK ) {
			LED_TURN_OFF(RED_LED);
		} else {
			LED_TURN_ON(RED_LED);
		}

		// Handling the state change of the main switch
		if (ON_KEY_UP(BUTTON_SWITCH)) {
			// Upon releasing the switch, stop the continuous reading.
			sensor_stop_continuous_reading();

#if ENABLE_KEYBOARD
			// And also reset the menu system.
			init_ui_system();
#endif
		} else if (ON_KEY_DOWN(BUTTON_SWITCH)) {
			// Upon pressing the switch, start the continuous reading for
			// mouse emulation code.
			sensor_start_continuous_reading();
		}

		// Continuous reading of sensor data
		if (sensor.continuous_reading) {  // {{{
			// Timer is set to 1.365ms
			if (timer_overflow) {
				// The sensor is configured for 75Hz measurements.
				// I'm using this timer to read the values twice that rate.
				// 5 * 1.365ms = 6.827ms ~= 146Hz
				if (sensor_probe_counter > 0){
					// Waiting...
					sensor_probe_counter--;
				}
			}
			if (sensor_probe_counter == 0) {
				// Time for reading new data!
				uchar return_code;

				return_code = sensor_read_data_registers();
				if (return_code == SENSOR_FUNC_DONE || return_code == SENSOR_FUNC_ERROR) {
					// Restart the counter+timer
					sensor_probe_counter = 5;
				}
			}
		}  // }}}

#if ENABLE_IDLE_RATE
		// Timer is set to 1.365ms
		if (timer_overflow) {  // {{{
			// Implementing the idle rate...
			if (idle_rate != 0) {
				if (idle_counter > 0){
					idle_counter--;
				} else {
					// idle_counter counts how many Timer0 overflows are
					// required before sending another report.
					// The exact formula is:
					// idle_counter = (idle_rate * 4)/1.365;
					// But it's better to avoid floating point math.
					// 4/1.365 = 2.93, so let's just multiply it by 3.
					idle_counter = idle_rate * 3;

					//keyDidChange = 1;
					LED_TOGGLE(YELLOW_LED);
					// TODO: Actually implement idle rate... Should re-send
					// the current status.
				}
			}
		}  // }}}
#endif

		// MAIN code. Code that emulates the mouse or implements the menu
		// system.
		if (button.state & BUTTON_SWITCH) {
			// Code for when the switch is held down
			// Should read data and do things

#if ENABLE_MOUSE
			// nothing here
#endif
		} else {
			// Code for when the switch is "off"
			// Basically, this is the menu system (implemented as keyboard)

#if ENABLE_KEYBOARD
			ui_main_code();
#endif
		}

		// Sending USB Interrupt-in report
		if(usbInterruptIsReady()) {
			if (0) {
				// This useless "if" is here to make all the following
				// conditionals an "else if", and thus making it a lot
				// easier to add/remove them using preprocessor directives.
			}
#if ENABLE_KEYBOARD
			else if(string_output_pointer != NULL){
				// Automatically send keyboard report if there is something
				// in the buffer
				send_next_char();
				usbSetInterrupt((void*) &keyboard_report, sizeof(keyboard_report));
			}
#endif
#if ENABLE_MOUSE
			else if (button.state & BUTTON_SWITCH) {
				if (mouse_prepare_next_report()) {
					usbSetInterrupt((void*) &mouse_report, sizeof(mouse_report));
				}
			}
#endif
		}
	}
}  // }}}

// }}}

// vim:noexpandtab tabstop=4 shiftwidth=4 foldmethod=marker foldmarker={{{,}}}
