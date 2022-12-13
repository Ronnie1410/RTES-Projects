/*
 * CSE-522 Assignment 2
 * Author: Roshan Raj Kanakaiprath
 * ASU ID: 1222478062
 */

#include <zephyr.h>
#include <device.h>
#include <devicetree.h>
#include <drivers/pwm.h>
#include <drivers/spi.h>
#include <sys/util.h>
#include <sys/printk.h>
#include <shell/shell.h>
#include <shell/shell_uart.h>
#include <version.h>
#include <stdlib.h>
#include <fsl_iomuxc.h>
#include <drivers/display.h>

//#define DEBUG 

#if defined(DEBUG) 
	#define DPRINTK(fmt, args...) printk("DEBUG: %s():%d: " fmt, \
   		 __func__, __LINE__, ##args)
#else
 	#define DPRINTK(fmt, args...) /* do nothing if not defined*/
#endif

// Use DT marcos to get pwm channel information

#define PWM_NODE_R 			flexpwm1_pwm3
#define PWM_NODE_G			flexpwm1_pwm1
#define PWM_NODE_B			flexpwm1_pwm3

#define RED_PWM				DT_NODELABEL(PWM_NODE_R)
#define GREEN_PWM			DT_NODELABEL(PWM_NODE_G)
#define BLUE_PWM			DT_NODELABEL(PWM_NODE_B)

#define RED_PWM_LABEL 		DT_PROP(RED_PWM, label)
#define GREEN_PWM_LABEL     DT_PROP(GREEN_PWM, label)
#define BLUE_PWM_LABEL      DT_PROP(BLUE_PWM, label)


#define RED_PWM_CHANNEL		DT_PWMS_CHANNEL(DT_NODELABEL(r_led))
#define GREEN_PWM_CHANNEL	DT_PWMS_CHANNEL(DT_NODELABEL(g_led))
#define BLUE_PWM_CHANNEL	DT_PWMS_CHANNEL(DT_NODELABEL(b_led))

#define RED_PWM_FLAGS		DT_PWMS_FLAGS(RED_PWM)
#define GREEN_PWM_FLAGS		DT_PWMS_FLAGS(GREEN_PWM)
#define BLUE_PWM_FLAGS		DT_PWMS_FLAGS(BLUE_PWM)

//USE DT to get DISPLAY NODE Information from overlay
#define DISPLAY_NODE_LABEL	DT_PROP(DT_NODELABEL(display_node),label)
#define MAX_HEIGHT			DT_PROP(DT_NODELABEL(display_node),height)

//PWM Frequency and Period
#define PWM_FREQUENCY			50U
#define PERIOD					USEC_PER_SEC/(PWM_FREQUENCY)


//Create pointers to struct device
const struct device *pwm_red, *pwm_green, *pwm_blue, *display;
static bool blinking_on = false;// Initialize to false
static uint8_t data[MAX_HEIGHT] = {0}; //Initialize to 0

//Shell command callback to implement MAX719_pattern change
static int cmd_led_matrix(const struct shell *shell, size_t argc, char **argv)
{
	uint8_t row_start, height;

	//Error check for number of command arguments
	if (argc < 3)
	{
		shell_print(shell, "Error, Invalid number of arguments\n");
		return 0;
	}

	row_start  = atoi(argv[1]); //Get the row value from command
	height = argc -2;

	//Error check for row bounds
	if(row_start>7)
	{
		shell_print(shell, "Error, row number entered is wrong\n");
		return 0;
	}

	//Error check for max number of command parameters
	if((height)>(MAX_HEIGHT - row_start))
	{
		shell_print(shell, "Error, Invalid number of arguments\n");
		return 0;
	}

	uint8_t loop;

	//Store the command arguments in to data 
	for (loop = 0;loop<height;loop++)
	{
		data[loop + row_start]= strtol(argv[loop+2],NULL,16);
	}

	//Create a structure varialble to point to display_buffer_descriptor
	struct display_buffer_descriptor desc;
	desc.buf_size  = sizeof(data);
	desc.width     = MAX_HEIGHT;
	desc.height    = height;
	desc.pitch		= MAX_HEIGHT;
	
	//Call display driver API to send the data
	display_write(display,0, row_start, &desc, &data);
	return 0;
}

static int cmd_rgb_control(const struct shell *shell, size_t argc, char **argv)
{
	uint32_t rpwm_dc, gpwm_dc, bpwm_dc;
	rpwm_dc = atoi(argv[1]);
	gpwm_dc = atoi(argv[2]);
	bpwm_dc = atoi(argv[3]);

	if(argc != 4)
	{
		shell_print(shell, "Error, Invalid number of arguments\n");
		return 0;
	}
	if((rpwm_dc<0) || (gpwm_dc < 0) || (bpwm_dc < 0) || (rpwm_dc>100) || (gpwm_dc > 100) || (bpwm_dc > 100))
	{
		shell_print(shell, "Error, Arguments must be in the range 0 to 100");
		return 0;
	}
	pwm_pin_set_usec(pwm_red, RED_PWM_CHANNEL, PERIOD, (PERIOD * rpwm_dc)/100, RED_PWM_FLAGS);
	DPRINTK("red blinking\n");
	k_sleep(K_USEC(PERIOD));
	pwm_pin_set_usec(pwm_green, GREEN_PWM_CHANNEL, PERIOD, (PERIOD * gpwm_dc)/100, GREEN_PWM_FLAGS);
	DPRINTK("green blinking\n");
	k_sleep(K_USEC(PERIOD));
	pwm_pin_set_usec(pwm_blue, BLUE_PWM_CHANNEL, PERIOD, (PERIOD * bpwm_dc)/100, BLUE_PWM_FLAGS);
	DPRINTK("blue blinking\n");
	
	return 0;
}

static int cmd_blinking_disp(const struct shell *shell, size_t argc, char **argv)
{

	if(argc!=2){
		shell_print(shell,"Invalid number of arguments or invalid input for n!");
	}
	blinking_on=true;
	if(atoi(argv[1])==0){
		blinking_on=false;
		display_blanking_off(display);
	}
	
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_commands,
	SHELL_CMD(rgb, NULL, "Provide duty cycles for red, green and blue leds respectively", cmd_rgb_control),
	SHELL_CMD(ledm, NULL, "Led Matrix Command", cmd_led_matrix),
	SHELL_CMD(ledb, NULL,"Blinking mode on and off", cmd_blinking_disp),
	SHELL_SUBCMD_SET_END /* Array terminated. */
);
SHELL_CMD_REGISTER(p2, &sub_commands, "Commands for Assignment2", NULL);

//Function to clear max7219 buffer before using
void max7219_clear()
{
	struct display_buffer_descriptor desc;
	desc.buf_size  = sizeof(data);
	desc.width     = MAX_HEIGHT;
	desc.height    = MAX_HEIGHT;
	desc.pitch		= MAX_HEIGHT;
	
	//Call display driver API to send the data
	display_write(display,0, 0, &desc, &data);
}

void main(void)
{
	// check the labels for each pwm channel and dispaly node

	DPRINTK("From device tree : %s, %s, %s, %s\n", RED_PWM_LABEL, GREEN_PWM_LABEL, BLUE_PWM_LABEL, DISPLAY_NODE_LABEL);

	// hard-coded statements to set up IOMUX for PWM Channels

	IOMUXC_SetPinMux(IOMUXC_GPIO_AD_B0_11_FLEXPWM1_PWMB03, 0);

	IOMUXC_SetPinConfig(IOMUXC_GPIO_AD_B0_11_FLEXPWM1_PWMB03,
			    IOMUXC_SW_PAD_CTL_PAD_PUE(1) |
			    IOMUXC_SW_PAD_CTL_PAD_PKE_MASK |
			    IOMUXC_SW_PAD_CTL_PAD_SPEED(2) |
			    IOMUXC_SW_PAD_CTL_PAD_DSE(6));

	IOMUXC_SetPinMux(IOMUXC_GPIO_SD_B0_03_FLEXPWM1_PWMB01, 0);

	IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B0_03_FLEXPWM1_PWMB01,
			    IOMUXC_SW_PAD_CTL_PAD_PUE(1) |
			    IOMUXC_SW_PAD_CTL_PAD_PKE_MASK |
			    IOMUXC_SW_PAD_CTL_PAD_SPEED(2) |
			    IOMUXC_SW_PAD_CTL_PAD_DSE(6));

	IOMUXC_SetPinMux(IOMUXC_GPIO_AD_B0_10_FLEXPWM1_PWMA03, 0);

	IOMUXC_SetPinConfig(IOMUXC_GPIO_AD_B0_10_FLEXPWM1_PWMA03,
			    IOMUXC_SW_PAD_CTL_PAD_PUE(1) |
			    IOMUXC_SW_PAD_CTL_PAD_PKE_MASK |
			    IOMUXC_SW_PAD_CTL_PAD_SPEED(2) |
			    IOMUXC_SW_PAD_CTL_PAD_DSE(6));

	//Hard-coded statements to set IOMUX for SPI Interface
	IOMUXC_SetPinMux(IOMUXC_GPIO_SD_B0_01_LPSPI1_PCS0, 0);

	IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B0_01_LPSPI1_PCS0,
			    IOMUXC_SW_PAD_CTL_PAD_PUE(1) |
			    IOMUXC_SW_PAD_CTL_PAD_PKE_MASK |
			    IOMUXC_SW_PAD_CTL_PAD_SPEED(2) |
			    IOMUXC_SW_PAD_CTL_PAD_DSE(6));

	IOMUXC_SetPinMux(IOMUXC_GPIO_SD_B0_02_LPSPI1_SDO, 0);

	IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B0_02_LPSPI1_SDO,
			    IOMUXC_SW_PAD_CTL_PAD_PUE(1) |
			    IOMUXC_SW_PAD_CTL_PAD_PKE_MASK |
			    IOMUXC_SW_PAD_CTL_PAD_SPEED(2) |
			    IOMUXC_SW_PAD_CTL_PAD_DSE(6));	

	IOMUXC_SetPinMux(IOMUXC_GPIO_SD_B0_00_LPSPI1_SCK, 0);
	
	IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B0_00_LPSPI1_SCK,
			    IOMUXC_SW_PAD_CTL_PAD_PUE(1) |
			    IOMUXC_SW_PAD_CTL_PAD_PKE_MASK |
			    IOMUXC_SW_PAD_CTL_PAD_SPEED(2) |
			    IOMUXC_SW_PAD_CTL_PAD_DSE(6));	
	
	//Get Device bindings for all PWM channels and Display device
	pwm_red=device_get_binding(RED_PWM_LABEL);
	if (!pwm_red) {
		DPRINTK("error binding red channel\n");
		return;
	}

	pwm_green=device_get_binding(GREEN_PWM_LABEL);
	if (!pwm_green) {
		DPRINTK("error binding green channel\n");
		return;
	}

	pwm_blue=device_get_binding(BLUE_PWM_LABEL);
	if (!pwm_blue) {
		DPRINTK("error binding blue channel\n");
		return;
	}

	display = device_get_binding(DISPLAY_NODE_LABEL);
	if (!pwm_blue) {
		DPRINTK("error binding blue channel\n");
		return;
	}

	max7219_clear();
	//Loop to implement blinking on/off mode
	while(1)
	{
		if(blinking_on)
		{
			display_blanking_on(display);
			k_msleep(1000);
			display_blanking_off(display);
			k_msleep(1000);
		}
		k_msleep(1);
	}

}
