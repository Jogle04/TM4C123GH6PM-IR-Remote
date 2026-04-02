//IR Remote Control Project jho5135
#include "tm4c123gh6pm.h"
#include "clock.h"
#include "uart0.h"
#include <stdint.h>

#define PB4 (*((volatile uint32_t *)0x420A7F90)) // Bit-banded Port B4 address, for PWM
#define PD0 (*((volatile uint32_t *)0x420E7F80)) // Bit-banded Port D0 address, for TSOP input
#define PD1 (*((volatile uint32_t *)0x420E7F84)) // Bit-banded Port D1 address, for testing will be hooked to LEDR

// PWM frequency needs to equal 38 KHz
// System clock frequency running at 40 MHz
// Reload value = (40 MHz / 38 KHz) - 1 = 1052 ticks
#define PWMRELOAD 1052 // 38 KHz
#define PWMDUTY 526 // 50% duty cycle

// Global variables for ISR
volatile uint32_t prevTime = 0;
volatile uint32_t pulse[300];
volatile uint32_t index = 0;

// Global variables for UART0 use
uint32_t keys[17] = {0}; // 16 keys, 0 will be ignored for index purposes
uint8_t recv = 0; // Recording


// GPIO D interrupt handler function
void GPIOD_Handler(void)
{
	GPIO_PORTD_ICR_R |= 0x01; // Clear interrupt

	uint32_t currentTime = TIMER0_TAV_R; // Read current time
	uint32_t pulseInterval; // = prevTime - currentTime; // Get pulse width

	if (prevTime >= currentTime)
	{
		pulseInterval = (prevTime - currentTime) / 40; // Get microseconds
	}
	else
	{
		pulseInterval = (prevTime + (0xFFFFFFFF - currentTime) + 1) / 40; // Get microseconds
	}

	prevTime = currentTime; // Reset previous time value to current

	if (pulseInterval > 200 && pulseInterval < 15000 && index < 300)
	{
		pulse[index++] = pulseInterval; // Fill pulse array values
		PD1 ^= 1; // Toggle LED on during pulse
	}
}

// UART0 integer terminal printing function
void intPrintUart0 (uint32_t value)
{
	char buffer[12];
	int i = 0;

	if (value == 0) // If there is no value, return 0
	{
		putcUart0('0');
		return;
	}

	while (value > 0) // While there is a value, store it in the buffer
	{
		buffer[i++] = (value % 10) + '0';
		value /= 10;
	}

	while (i > 0) // While there are values in the buffer, print them using UART0
	{
		putcUart0(buffer[--i]);
	}
}

int findPulseLeader(void)
{
	int i;
	for (i = 0; i < (int)index - 1; i++)
	{
		if (pulse [i] > 8500 && pulse[i] < 9500 && pulse[i+1] > 4000 && pulse[i+1] < 5000) // Leading pulse values
		{
			return i + 2;
		}

		if (pulse[i] > 8500 && pulse[i] < 9500 && pulse[i+1] > 1800 && pulse[i+1] < 2800) // Subsequent pulse values
		{
			return 0;
		}
	}
	return 0;
}

uint32_t pulseDecoder(void)
{
	int start = findPulseLeader();
	if (start == 0 || (index - start) < 64)
	{
		return 0;
	}

	// putsUart0("Decoded bits:\r\n"); Was used for testing

	uint32_t decoded = 0;
	int i;
	int bitCount = 0;

	for(i = start; i < index-1 && bitCount < 32; i++)
	{
		uint32_t mark = pulse[i];
		uint32_t space = pulse[i+1];

		if (mark > 400 && mark < 750)
		{
			decoded <<= 1;
			if (space > 1125)
			{
				//putcUart0('1');
				decoded |= 1;
			}
			/*else
			{
				putcUart0('0');
			}*/
			bitCount++;
			i++;
		}
	}
	//putsUart0("\r\n");
	return decoded; // Return the decoded IR value
}

void pwmOn(void)
{
	TIMER1_TAMATCHR_R = PWMDUTY; // Set PWM to duty cycle value - high
}

void pwmOff(void)
{
	TIMER1_TAMATCHR_R = PWMRELOAD - 1; // Set PWM to reload value - low
}

void delayMicroSec(uint32_t us)
{
	uint32_t start = TIMER0_TAV_R; // start = timer value
	uint32_t ticks = us * 40; // Microseconds
	while ((start - TIMER0_TAV_R) < ticks);
}

void transmit(uint32_t code)
{
	int i;

	pwmOn(); // Enable PWM
	delayMicroSec(9000); // Wait for pulse leader
	pwmOff(); // Disable PWM
	delayMicroSec(4500); // Wait for pulse leader end

	for (i = 31; i >= 0; i--) // Run through signal code
	{
		pwmOn();
		delayMicroSec(562); // Wait half duty cycle
		pwmOff();
		if (code & (1 << i))
		{
			delayMicroSec(1687);
		}
		else
		{
			delayMicroSec(562);
		}
	}

	pwmOn();
	delayMicroSec(562);
	pwmOff();

	// Confirm transmit signal code
	putsUart0("Transmitting: ");
	    intPrintUart0(code);
	    putsUart0("\r\n");

	// Waiting period to prevent accidental IR saving loop
	   index = 0;
	   _delay_cycles(40000000);
	   index = 0;
}

void processCommand(char* command)
{
	uint8_t number = 0; // Hold key pressed

	if (command[0] == 'r' && command[1] == 'e' && command[2] == 'c' && command[3] == 'v' && command[4] == ' ')
	{
		number = command[5] - '0'; // ASCII character conversion

		if (command[6] >= '0' && command[6] <= '9') // 2 digit handling
		{
			number = number * 10 + (command[6] - '0');
		}

		if (number >= 2 && number <= 16)
		{
			putsUart0("Waiting for signal. \r\n");
			recv = number; // Store key number in receive
		}
		else
		{
			putsUart0("Invalid key. \r\n"); // Error handling
		}

	}
	else if (command[0] == 'x' && command[1] == 'm' && command[2] == 'i' && command[3] == 't' && command[4] == ' ')
	{
		number = command[5] - '0'; // ASCII character conversion

		if (command[6] >= '0' && command[6] <= '9') // 2 digit handling
		{
			number = number * 10 + (command[6] - '0');
		}

		if (number >= 1 && number <= 16) // Check for key validity
		{
			if (keys[number] != 0)
			{
				transmit(keys[number]); // transmit code
			}
			else
			{
				putsUart0("No recorded key. \r\n"); // Error handling
			}
		}
		else
		{
			putsUart0("Invalid. \r\n"); // Error handling
		}
	}
	else
	{
		putsUart0("Unknown command, try recv # or xmit #. \r\n");
	}
}

/**
 * main.c
 */
int main(void)
{
	// Set system clock to 40MHz
	initSystemClockTo40Mhz();

	// Initialize UART
	initUart0();

	// Enable and configure Port B for PWM
	SYSCTL_RCGCGPIO_R |= SYSCTL_RCGCGPIO_R1; // Enable clocks to GPIO Port B
	_delay_cycles(3);
	GPIO_PORTB_DIR_R |= (0x10); // Set direction for PB4 to output
	GPIO_PORTB_DEN_R |= (0x10); // Digitally enable PB4
	GPIO_PORTB_AFSEL_R |= (0x10); // Enable alternate function select for PB4
	GPIO_PORTB_PCTL_R &= ~(0xF << 16); // Clear PB4 PCTL
	GPIO_PORTB_PCTL_R |= (0x7 << 16); // Set PB4 to T1CCP0 - page 1351

	// Enable and configure Port D for input/output
	SYSCTL_RCGCGPIO_R |= SYSCTL_RCGCGPIO_R3; // Enable clocks to GPIO Port D
	_delay_cycles(3);
	GPIO_PORTD_DIR_R &= ~(0x01); // Set direction for PD0 to input
	GPIO_PORTD_DIR_R |= (0x02); // Set direction for PD1 to output
	GPIO_PORTD_DEN_R |= (0x03); // Digitally enable PD0 and PD1
	GPIO_PORTD_DR2R_R |= (0x02); // Set 2 mA drive for PD1
	GPIO_PORTD_PUR_R |= (0x01); // Enable pull-up resistor for PD0

	// GPIO interrupt configuration - page 660
	GPIO_PORTD_IS_R &= ~(0x01); // Interrupt sense
	GPIO_PORTD_IBE_R |= (0x01); // Interrupt both edges
	GPIO_PORTD_ICR_R = (0x01); // Interrupt clear
	GPIO_PORTD_IM_R |= (0x01); // Un-Mask interrupt

	// Enable NVIC for Port D
	NVIC_EN0_R |= (1 << 3);

	// GPTM timer A setup - Decoding
	SYSCTL_RCGCTIMER_R |= SYSCTL_RCGCTIMER_R0; // 16/32 bit GPTM, 0 Run mode clock gating control
	_delay_cycles(3);
	TIMER0_CTL_R &= ~TIMER_CTL_TAEN; // Disable timer A using timer control and NOT TimerA enable
	TIMER0_CFG_R = TIMER_CFG_32_BIT_TIMER; // Select 32-bit timer configuration
	TIMER0_TAMR_R = TIMER_TAMR_TAMR_PERIOD; // Timer A Periodic timer mode
	TIMER0_CTL_R &= ~TIMER_TAMR_TACDIR; // Set count direction to count down
	TIMER0_TAILR_R = 0xFFFFFFFF; // Timer A interval load - max value
	TIMER0_CTL_R |= TIMER_CTL_TAEN; // Enable timer A
	_delay_cycles(3);
	prevTime = TIMER0_TAV_R; // Initialize previous time to avoid garbage start

	// GPTM Timer 1 setup - PWM
	SYSCTL_RCGCTIMER_R |= SYSCTL_RCGCTIMER_R1; // Enable clocks to timer 1
	_delay_cycles(3);
	TIMER1_CTL_R &= ~TIMER_CTL_TAEN; // Disable timer
	TIMER1_CFG_R = TIMER_CFG_16_BIT; // Select 16-bit timer configuration
	TIMER1_TAMR_R = TIMER_TAMR_TAMR_PERIOD | TIMER_TAMR_TAAMS; // Clear Timer 1 periodic timer mode
	TIMER1_CTL_R |= TIMER_CTL_TAPWML; // Timer 1 PWM output level

	// PWM period and duty
	TIMER1_TAILR_R = PWMRELOAD - 1; // Timer 1 interval load - max value
	TIMER1_TAMATCHR_R = PWMDUTY; // Timer 1 Match register to duty cycle
	TIMER1_CTL_R |= TIMER_CTL_TAEN; // Enable timer 1
	pwmOff();

	char commands[32]; // Storage for full 32 bit commands
	uint8_t commandIndex = 0; // Index for each command

	keys[1] = 0x57E3E817; // Hardcoded key 1 for On/Off (Need to change per TV, convert decimal to hex)

	while(1)
	{
		if (kbhitUart0())
		{
			char c = getcUart0(); // Get character from keyboard
			putcUart0(c); // Echo for visual purposes

			if (c == '\r' || c == '\n') // Check for enter or newline
			{
				commands[commandIndex] = '\0'; // NULL terminator
				putsUart0("\r\n"); // Line break
				processCommand(commands);
				commandIndex = 0; // Reset index
			}
			else if (commandIndex < 31) // Overflow prevention
			{
				commands[commandIndex++] = c;
			}
		}

		if (recv != 0 && index >= 20) // If receive # is valid
		{
			_delay_cycles(4000000);
			uint32_t code = pulseDecoder(); // Store decoded signal

			if (code != 0)
			{
				keys[recv] = code; // Store code in key index
				putsUart0("Key saved. \r\n");
				recv = 0; // Reset receive
			}

			index = 0; // Reset index
		}

		/*if (index >= 20) // For active decoding of IR signals from remote
		{
			_delay_cycles(4000000);
			pulseDecoder(); Decode data
			PD1 = 0;
			index = 0; // Reset index
		}*/
	}
}
