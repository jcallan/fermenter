#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include "fermenter.h"

#define GPIO_CMD_TEMPLATE	"gpio export %u high"
#define GPIO_FILE_TEMPLATE	"/sys/class/gpio/gpio%u/value"
#define W1_FILE_TEMPLATE	"/sys/bus/w1/devices/28-%012llx/w1_slave"

const unsigned led_index[NUM_LEDS] = {14, 3, 2};		/* GPIO pins for LEDs A, B, Centre */
const unsigned heater_index[NUM_FERMENTERS] = {24, 10};	/* GPIO pins for relays */
const unsigned long long sensor_serial[NUM_FERMENTERS] = {0x03168169dfffull, 0x0416857569ffull};

static FILE *heater[NUM_FERMENTERS];
static FILE *led[NUM_LEDS];


float read_temperature(unsigned index)
{
	char buf[256];
	const char *equals_ptr;
	FILE *f;
	int ret;
	unsigned temp;
	float temperature = 999.0;
	
	if ((index < NUM_FERMENTERS) && sensor_serial[index])
	{
		sprintf(buf, W1_FILE_TEMPLATE, sensor_serial[index]);
		f = fopen(buf, "r");
		if (f)
		{
			ret = fread(buf, 1, sizeof(buf) - 1, f);
			fclose(f);
			if (ret > 0)
			{
				buf[ret] = 0;
				equals_ptr = strstr(buf, "t=");
				if (equals_ptr)
				{
					ret = sscanf(equals_ptr + 2, "%u", &temp);
					if (ret == 1)
					{
						temperature = (float)temp / 1000.0;
					}
					else
					{
						printf("Couldn't parse temperature from file:\n%s\nLocated '%s'\n",
							   buf, index, equals_ptr + 2);
					}
				}
				else
				{
					printf("Failed to find 't=' in the sensor %u output\n", index);
				}
			}
			else
			{
				printf("Failed to read data from sensor %u\n", index);
			}
		}
		else
		{
			printf("Failed to open sensor file '%s'\n", buf);
		}
	}
	else
	{
		printf("Bad sensor %u\n", index);
	}
	
	return temperature;
}

void set_heater(unsigned index, int on)
{
	if ((index < NUM_FERMENTERS) && heater[index])
	{
		fprintf(heater[index], "%u", (on ? 0 : 1));
		fflush(heater[index]);
		fprintf(led[index], "%d", on);
		fflush(led[index]);
	}
	else
	{
		printf("Bad heater %u\n", index);
	}
}

int init_gpio(void)
{
	int i, ret;
	char buf[80];

	/* Use the gpio utility to export all the GPIOs we need */	
	for (i = 0; i < NUM_LEDS; ++i)
	{
		sprintf(buf, GPIO_CMD_TEMPLATE, led_index[i]);
		ret = system(buf);
		if (ret != 0)
		{
			printf("Executing '%s' failed with return code %d\n", ret);
		}
	}
	for (i = 0; i < NUM_FERMENTERS; ++i)
	{
		sprintf(buf, GPIO_CMD_TEMPLATE, heater_index[i]);
		ret = system(buf);
		if (ret != 0)
		{
			printf("Executing '%s' failed with return code %d\n", ret);
		}
	}
	
	/* Open the file that controls the GPIO pin for each heater */
	for (i = 0; i < NUM_FERMENTERS; ++i)
	{
		sprintf(buf, GPIO_FILE_TEMPLATE, heater_index[i]);
		heater[i] = fopen(buf, "w");

		if (heater[i] == NULL)
		{
			printf("Could not open heater %u\n", heater_index[i]);
			return 4;
		}
	}
	
	/* Open the file that controls the GPIO pin for each LED */
	for (i = 0; i < NUM_LEDS; ++i)
	{
		sprintf(buf, GPIO_FILE_TEMPLATE, led_index[i]);
		led[i] = fopen(buf, "w");

		if (led[i] == NULL)
		{
			printf("Could not open led[%u]\n", i);
			return 4;
		}
	}		

	return 0;	
}

void *update_leds(void *arg)
{
	int blink = 0;

	/* Loop while updating the LED statuses */
	while(1)
	{
		sleep(1);
		blink = 1 - blink;
		fprintf(led[LED_C], "%d", blink);
		fflush(led[LED_C]);
	}
}

