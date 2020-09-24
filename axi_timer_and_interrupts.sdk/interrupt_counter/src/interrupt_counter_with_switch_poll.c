/*
 * interrupt_counter_with_switch_poll.c
 *
 *  Created on:		09/23/2020
 *      Author:		Leomar Duran
 *     Version:		1.2
 *
 * interrupt_counter_tut_2B.c
 *
 *  Created on: 	Unknown
 *      Author: 	Ross Elliot
 *     Version:		1.1
 */

/********************************************************************************************

* VERSION HISTORY
********************************************************************************************
* 	v1.2 - 09/23/2020
* 		Added switch to disable button interrupts, reset expirations
* 		and the LED counter data.
*
* 	v1.1 - 01/05/2015
* 		Updated for Zybo ~ DN
*
*	v1.0 - Unknown
*		First version created.
*******************************************************************************************/

#include "xparameters.h"
#include "xgpio.h"
#include "xtmrctr.h"
#include "xscugic.h"
#include "xil_exception.h"
#include "xil_printf.h"

// Parameter definitions
#define INTC_DEVICE_ID 		XPAR_PS7_SCUGIC_0_DEVICE_ID
#define TMR_DEVICE_ID		XPAR_TMRCTR_0_DEVICE_ID
#define BTNS_DEVICE_ID		XPAR_AXI_GPIO_0_DEVICE_ID
#define LEDS_DEVICE_ID		XPAR_AXI_GPIO_1_DEVICE_ID
#define SWCS_DEVICE_ID		XPAR_AXI_GPIO_2_DEVICE_ID
#define INTC_GPIO_INTERRUPT_ID XPAR_FABRIC_AXI_GPIO_0_IP2INTC_IRPT_INTR
#define INTC_TMR_INTERRUPT_ID XPAR_FABRIC_AXI_TIMER_0_INTERRUPT_INTR

#define BTN_INT 			XGPIO_IR_CH1_MASK

// original TMR_LOAD:
// since 32 LED counts ~ 166.62 seconds
// TMR_LOAD = 0xF8000000 ~ 166.62 seconds/(32 counts * 3 expirations) = 1.7356 seconds
// so  3 expirations = 5.2068 seconds, 32 counts = 166.62 seconds
// and 7 expirations = 12.149 seconds, 32 counts = 388.77 seconds
// 1.7356 seconds/7 = 0.24795 seconds should be enough for debounce
// decreasing the clock increases delay, so the clock is in normal mode
// and appears to be a 4-byte clock
//
// new TMR_LOAD:
// 0x: FFFFFFFF - F8000000 + 1 = 08000000
// 0x: 08000000 / 7 = 01249249
// 0x: FFFFFFFF - 01249249 + 1 = FEDB6DB7
// TMR_LOAD = 0xFEDB6DB7 should be 0.24795 seconds
#define TMR_LOAD			0xFEDB6DB7	// ~0.24795 seconds
#define	EXPIRATION_SCALE	8			// scales expirations

// default number of interrupts for LED count incrementing
#define	DEFAULT_N_EXPIRES	3
// the maximum allowed number of expires
#define MAX_N_EXPIRES		7

// button to increase the expiration
#define	BTN_INC_EXPIRES		0b0010
// switch to disable button interrupts
#define	SWC_DISABLE_BTNS	0b0001
// switch to enable the increment expirations button
#define	SWC_ENABLE_INC_BTN	0b0010

// GPIO instances
XGpio LEDInst, BTNInst, SWCInst;
// interrupt controller driver instance data
XScuGic INTCInst;
// timer instance
XTmrCtr TMRInst;
// global state
static int led_data;
static int btn_value;
static int tmr_count;
static int n_expires;	// number of timer expires before scale
						// increments
static enum { YES, NO } is_inc_enabled = NO;

// for debouncing
static enum { NOT_DEBOUNCING, DEBOUNCING } dbn_state = NOT_DEBOUNCING;
static int dbn_tmr_count = 0;	// count at the time of starting debouncing



//----------------------------------------------------
// PROTOTYPE FUNCTIONS
//----------------------------------------------------
static void BTN_Intr_Handler(void *baseaddr_p);
static void TMR_Intr_Handler(void *baseaddr_p);
static int InterruptSystemSetup(XScuGic *XScuGicInstancePtr);
static int IntcInitFunction(u16 DeviceId, XTmrCtr *TmrInstancePtr, XGpio *GpioInstancePtr);

//----------------------------------------------------
// INTERRUPT HANDLER FUNCTIONS
// - called by the timer, button interrupt, performs
// - LED flashing
//----------------------------------------------------


void BTN_Intr_Handler(void *InstancePtr)
{
	// Disable GPIO interrupts
	XGpio_InterruptDisable(&BTNInst, BTN_INT);
	// Ignore additional button presses
	if ((XGpio_InterruptGetStatus(&BTNInst) & BTN_INT) !=
			BTN_INT) {
			return;
		}
	btn_value = XGpio_DiscreteRead(&BTNInst, 1);
	xil_printf("button pressed:\t0x%02x\t\t", btn_value);
	xil_printf("# expirations:\t%d\t\t", n_expires);
	xil_printf("inc enabled:\t%d\n", (is_inc_enabled == YES) & 1);


	// debounce if the button to increment the expiration is pressed and n_expires is not already at max
	if ((is_inc_enabled == YES)
			&& (btn_value == BTN_INC_EXPIRES)
			&& (n_expires != MAX_N_EXPIRES))
	{
		dbn_state = DEBOUNCING;		// set state to debouncing
		dbn_tmr_count = tmr_count;	// record current time
		return;	// do not continue
	}

	// Increment counter based on button value
	// Reset if centre button pressed
	led_data = led_data + btn_value;
	xil_printf("LED count:\t0x%02x\n", led_data);

    XGpio_DiscreteWrite(&LEDInst, 1, led_data);
    (void)XGpio_InterruptClear(&BTNInst, BTN_INT);
    // Enable GPIO interrupts
    XGpio_InterruptEnable(&BTNInst, BTN_INT);
}

void TMR_Intr_Handler(void *data)
{
	if (XTmrCtr_IsExpired(&TMRInst,0)){
		// stop the timer if it expires
		XTmrCtr_Stop(&TMRInst,0);

		// check the debounce
		switch (dbn_state) {
			case DEBOUNCING:
				xil_printf("debouncing . . .\n");
				// check if enough time has elapsed
				if (tmr_count != dbn_tmr_count) {
					// continue if still pressing the button to increment the expiration
					if (btn_value == BTN_INC_EXPIRES) {
						dbn_state = NOT_DEBOUNCING;	// stop debouncing
						n_expires++;	// increase the n_expires
						xil_printf("# expirations:\t%d\n", n_expires);
						(void)XGpio_InterruptClear(&BTNInst, BTN_INT);

						// Enable GPIO interrupts
					    XGpio_InterruptEnable(&BTNInst, BTN_INT);
					}
				}
			break;
			// if not debouncing, do nothing
			default:	break;
		}

		// Once timer has expired (n_expires scaled) times,
		// stop, increment counter reset timer and start
		// running again
		if(tmr_count == n_expires * EXPIRATION_SCALE){
			tmr_count = 0;
			led_data++;
			XGpio_DiscreteWrite(&LEDInst, 1, led_data);

		}
		else tmr_count++;
		// in either case, reset and restart the timer
		XTmrCtr_Reset(&TMRInst,0);
		XTmrCtr_Start(&TMRInst,0);
	}
}



//----------------------------------------------------
// MAIN FUNCTION
//----------------------------------------------------
int main (void)
{
  int status;
  int swc_value;
  int next_swc_value;

  // set the number of interrupts for LED count incrementing to the
  // default
  n_expires = DEFAULT_N_EXPIRES;

  //----------------------------------------------------
  // INITIALIZE THE PERIPHERALS & SET DIRECTIONS OF GPIO
  //----------------------------------------------------
  // Initialize LEDs
  status = XGpio_Initialize(&LEDInst, LEDS_DEVICE_ID);
  if(status != XST_SUCCESS) return XST_FAILURE;
  // Initialize Push Buttons
  status = XGpio_Initialize(&BTNInst, BTNS_DEVICE_ID);
  if(status != XST_SUCCESS) return XST_FAILURE;
  // Initialize Slide Switches
  status = XGpio_Initialize(&SWCInst, SWCS_DEVICE_ID);
  if(status != XST_SUCCESS) return XST_FAILURE;
  // Set LEDs direction to outputs
  XGpio_SetDataDirection(&LEDInst, 1, 0x00);
  // Set all buttons direction to inputs
  XGpio_SetDataDirection(&BTNInst, 1, 0xFF);
  // Set all switches direction to inputs
  XGpio_SetDataDirection(&SWCInst, 1, 0xFF);
  // Initialize first swc_value
  swc_value = XGpio_DiscreteRead(&SWCInst, 1);


  //----------------------------------------------------
  // SETUP THE TIMER
  //----------------------------------------------------
  status = XTmrCtr_Initialize(&TMRInst, TMR_DEVICE_ID);
  if(status != XST_SUCCESS) return XST_FAILURE;
  XTmrCtr_SetHandler(&TMRInst, TMR_Intr_Handler, &TMRInst);
  XTmrCtr_SetResetValue(&TMRInst, 0, TMR_LOAD);
  XTmrCtr_SetOptions(&TMRInst, 0, XTC_INT_MODE_OPTION | XTC_AUTO_RELOAD_OPTION);
 

  // Initialize interrupt controller
  status = IntcInitFunction(INTC_DEVICE_ID, &TMRInst, &BTNInst);
  if(status != XST_SUCCESS) return XST_FAILURE;

  XTmrCtr_Start(&TMRInst, 0);




  // log polling
  xil_printf("polling . . .\n");

  while(1) {
	  // poll switch 0
	  next_swc_value = XGpio_DiscreteRead(&SWCInst, 1);
	  // update and log on swc_value change
	  if (swc_value != next_swc_value) {
		  swc_value = next_swc_value;
		  xil_printf("new switch value:\t0x%02x\n", swc_value);
		  // only re-enable the button interrupts when changing on->off
		  if ((swc_value & SWC_DISABLE_BTNS) == 0) {
			  XGpio_InterruptEnable(&BTNInst, BTN_INT);
		  }
	  }
	  // if disable buttons on
	  if ((swc_value & SWC_DISABLE_BTNS) == SWC_DISABLE_BTNS) {
		  // disable button interrupts
		  XGpio_InterruptDisable(&BTNInst, BTN_INT);
		  // reset the number of interrupts for LED count
		  // incrementing
		  n_expires = DEFAULT_N_EXPIRES;
		  // reset the LED count display
		  led_data = 0b0000;
	  }
	  // check the switch to enable the increment expirations button
	  switch (swc_value & SWC_ENABLE_INC_BTN) {
	  	  // enable if all on
	  	  case SWC_ENABLE_INC_BTN:
			  is_inc_enabled = YES;
	  	  break;
	  	  // disable if all off
	  	  case 0:
			  is_inc_enabled = NO;
	  	  break;
  		  // do nothing otherwise
	  	  default:	break;
	  }
  }

  return 0;
}

//----------------------------------------------------
// INITIAL SETUP FUNCTIONS
//----------------------------------------------------

int InterruptSystemSetup(XScuGic *XScuGicInstancePtr)
{
	// Enable interrupt
	XGpio_InterruptEnable(&BTNInst, BTN_INT);
	XGpio_InterruptGlobalEnable(&BTNInst);

	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,
			 	 	 	 	 	 (Xil_ExceptionHandler)XScuGic_InterruptHandler,
			 	 	 	 	 	 XScuGicInstancePtr);
	Xil_ExceptionEnable();


	return XST_SUCCESS;

}

int IntcInitFunction(u16 DeviceId, XTmrCtr *TmrInstancePtr, XGpio *GpioInstancePtr)
{
	XScuGic_Config *IntcConfig;
	int status;

	// Interrupt controller initialisation
	IntcConfig = XScuGic_LookupConfig(DeviceId);
	status = XScuGic_CfgInitialize(&INTCInst, IntcConfig, IntcConfig->CpuBaseAddress);
	if(status != XST_SUCCESS) return XST_FAILURE;

	// Call to interrupt setup
	status = InterruptSystemSetup(&INTCInst);
	if(status != XST_SUCCESS) return XST_FAILURE;
	
	// Connect GPIO interrupt to handler
	status = XScuGic_Connect(&INTCInst,
					  	  	 INTC_GPIO_INTERRUPT_ID,
					  	  	 (Xil_ExceptionHandler)BTN_Intr_Handler,
					  	  	 (void *)GpioInstancePtr);
	if(status != XST_SUCCESS) return XST_FAILURE;


	// Connect timer interrupt to handler
	status = XScuGic_Connect(&INTCInst,
							 INTC_TMR_INTERRUPT_ID,
							 (Xil_ExceptionHandler)TMR_Intr_Handler,
							 (void *)TmrInstancePtr);
	if(status != XST_SUCCESS) return XST_FAILURE;

	// Enable GPIO interrupts interrupt
	XGpio_InterruptEnable(GpioInstancePtr, 1);
	XGpio_InterruptGlobalEnable(GpioInstancePtr);

	// Enable GPIO and timer interrupts in the controller
	XScuGic_Enable(&INTCInst, INTC_GPIO_INTERRUPT_ID);
	
	XScuGic_Enable(&INTCInst, INTC_TMR_INTERRUPT_ID);
	

	return XST_SUCCESS;
}

