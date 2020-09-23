/*
 * interrupt_counter_with_switch_poll.c
 *
 *      Created on: 	09/23/2020
 *  current Author:     Leomar Duran
 *   tut_2B Author: 	Ross Elliot
 *         Version:		1.2
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
#define TMR_LOAD			0xF8000000
// default number of interrupts for LED count incrementation
#define	DEFAULT_N_EXPIRES	3

// switch to disable button interrupts
#define	SWC_DISABLE_BTNS	0b0001

// GPIO instances
XGpio LEDInst, BTNInst, SWCInst;
// interrupt controller driver instance data
XScuGic INTCInst;
// timer instance
XTmrCtr TMRInst;
// shared state
static int led_data;
static int btn_value;
static int tmr_count;
static int n_expires;

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
	// Increment counter based on button value
	// Reset if centre button pressed
	led_data = led_data + btn_value;

    XGpio_DiscreteWrite(&LEDInst, 1, led_data);
    (void)XGpio_InterruptClear(&BTNInst, BTN_INT);
    // Enable GPIO interrupts
    XGpio_InterruptEnable(&BTNInst, BTN_INT);
}

void TMR_Intr_Handler(void *data)
{
	if (XTmrCtr_IsExpired(&TMRInst,0)){
		// Once timer has expired (n_expires) times, stop, increment
		// counter reset timer and start running again
		if(tmr_count == n_expires){
			XTmrCtr_Stop(&TMRInst,0);
			tmr_count = 0;
			led_data++;
			XGpio_DiscreteWrite(&LEDInst, 1, led_data);
			XTmrCtr_Reset(&TMRInst,0);
			XTmrCtr_Start(&TMRInst,0);

		}
		else tmr_count++;
	}
}



//----------------------------------------------------
// MAIN FUNCTION
//----------------------------------------------------
int main (void)
{
  int status;
  int swc_value;

  // set the number of interrupts for LED count incrementation to the
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
  // Set LEDs direction to outputs
  XGpio_SetDataDirection(&LEDInst, 1, 0x00);
  // Set all buttons direction to inputs
  XGpio_SetDataDirection(&BTNInst, 1, 0xFF);
  // Set all switches direction to inputs
  XGpio_SetDataDirection(&SWCInst, 1, 0xFF);


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




  while(1) {
	  // poll switch 0
	  swc_value = XGpio_DiscreteRead(&SWCInst, 1);
	  // if on
	  if ((swc_value & SWC_DISABLE_BTNS) == SWC_DISABLE_BTNS) {
		  // disable button interrupts
		  XGpio_InterruptDisable(&BTNInst, BTN_INT);
		  // reset the number of interrupts for LED count
		  // incrementation
		  n_expires = DEFAULT_N_EXPIRES;
		  // reset the LED count display
		  led_data = 0b0000;
	  }
	  else {
		  // re-enable the button interrupts
		  XGpio_InterruptEnable(&BTNInst, BTN_INT);
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
