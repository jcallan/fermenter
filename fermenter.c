#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>

#define VERSION_MAJOR		0
#define VERSION_MINOR		1

#define NUM_FERMENTERS		2
#define NUM_LEDS			3
#define TIME_INCREMENT_S	60	/* TODO use 3600 and allow floating point hours */

#define FIFO_IN_FILE_NAME	"/var/tmp/fermenter.in"
#define FIFO_OUT_FILE_NAME	"/var/tmp/fermenter.out"
#define LOCK_FILE_TEMPLATE	"/var/tmp/fermenter%u"
#define GPIO_CMD_TEMPLATE	"gpio export %u high"
#define GPIO_FILE_TEMPLATE	"/sys/class/gpio/gpio%u/value"

typedef struct programme_s
{
	float start_temp;
	float end_temp;
	time_t start_time;
	unsigned length;
	struct programme_s *next;
} programme_t;

typedef enum 
{
	FERMENTER_NO_COMMAND,
	FERMENTER_STOP,
} fermenter_cmd_t;

typedef struct
{
	char id;
	int gpio;
	volatile fermenter_cmd_t command;
	programme_t *head;
	programme_t *current;
} fermenter_t;

const unsigned led_index[NUM_LEDS] = {3, 14, 15};		/* GPIO pins for LEDs */
const unsigned heater_index[NUM_FERMENTERS] = {24, 10};	/* GPIO pins for relays */
const char fermenter_id[NUM_FERMENTERS] = {'A', 'B'};

void init_gpio(void)
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
}

void *update_leds(void *arg)
{
	FILE *led[NUM_LEDS];
	char buf[80];
	int i, blink = 0;

	for (i = 0; i < NUM_LEDS; ++i)
	{
		sprintf(buf, GPIO_FILE_TEMPLATE, led_index[i]);
		led[i] = fopen(buf, "w");

		if (led[i] == NULL)
		{
			printf("Could not open led[%u]\n", i);
			return (void *)4;
		}
	}		

	/* Loop while updating the LED statuses */
	while(1)
	{
		sleep(1);
		blink = 1 - blink;
		fprintf(led[0], "%d", blink);
		fflush(led[0]);
	}
}

programme_t *load_programme(const char *file_name)
{
	FILE *programme_file;
	programme_t *head = NULL, *prog = NULL, *last = NULL;
	float start_temp, end_temp;
	unsigned length;
	char buf[80];
	int ret;
	
	programme_file = fopen(file_name, "r");
	if (programme_file == NULL)
	{
		printf("%s opening programme file %s\n", strerror(errno), file_name);
		return NULL;
	}
	
	do
	{
		fgets(buf, sizeof(buf), programme_file);
		ret = sscanf(buf, "%f %f %u", &start_temp, &end_temp, &length);
		if (ret != 3)
		{
			printf("Failed to read 3 arguments from line '%s'\n", buf);
		}
		else
		{
			prog = calloc(1, sizeof(programme_t));
			if (prog)
			{
				prog->start_temp = start_temp;
				prog->end_temp   = end_temp;
				prog->length     = length;
				prog->start_time = 0;
				
				if (last)
				{
					last->next = prog;
				}
				if (!head)
				{
					head = prog;
				}
				last = prog;
				printf("Added step: %.1f->%.1f in %u minutes\n",
					   prog->start_temp, prog->end_temp, prog->length);
			}
		}
	} while (!feof(programme_file));

	return head;		
}

void free_programme(programme_t *head)
{
	programme_t *tmp;
	
	while (head)
	{
		tmp = head->next;
		free(head);
		head = tmp;
	}
}

void start_programme(programme_t *head)
{
	time_t now;
	char *buf;
	programme_t *prog;
	int step_no = 0;
	
	/* Write start time to lock file */
	now = time(NULL);
	/* TODO */
	
	/* Write the correct start time to each programme step */
	for (prog = head; prog != NULL; prog = prog->next)
	{
		prog->start_time = now;
		now += prog->length * TIME_INCREMENT_S;
		buf = ctime(&prog->start_time);
		buf[strlen(buf) - 1] = 0;
		printf("Step %2d: %s %.1f->%.1f\n", step_no, buf,
			   prog->start_temp, prog->end_temp);
		++step_no;
	}
}

void *run_fermenter(void *arg)
{
	fermenter_t *f = (fermenter_t *)arg;
	FILE *heater;
	char buf[80];
	int i, heat = 0;
	
	f->head = load_programme("cb1.programme");
	start_programme(f->head);
	
	/* Open the file that controls the GPIO pin for our heater */
	sprintf(buf, GPIO_FILE_TEMPLATE, f->gpio);
	heater = fopen(buf, "w");

	if (heater == NULL)
	{
		printf("Could not open heater %c\n", f->id);
		return (void *)4;
	}
	
	while (f->command == FERMENTER_NO_COMMAND)
	{
		sleep(15);
		heat = 1 - heat;
		fprintf(heater, "%d", heat);
		fflush(heater);
		printf("%c%d ", f->id, heat);
		fflush(stdout);
	}
	
	return NULL;
}

void *listener(void *arg)
{
	FILE *fifo_in, *fifo_out;
	char buf[80];
	int ret;
	char *p;
	
	/* Make sure the control FIFOs exist */
	ret = mkfifo(FIFO_IN_FILE_NAME, 0777);
	if (ret < 0)
	{
		if (errno == EEXIST)
		{
			printf("Input FIFO already exists\n");
		}
		else
		{
			printf("%s creating input FIFO!\n", strerror(errno));
			return (void *)5;
		}
	}
	ret = mkfifo(FIFO_OUT_FILE_NAME, 0775);
	if (ret < 0)
	{
		if (errno == EEXIST)
		{
			printf("Output FIFO already exists\n");
		}
		else
		{
			printf("%s creating output FIFO!\n", strerror(errno));
			return (void *)6;
		}
	}

	while(1)
	{
		/* Open the control FIFOs */
		fifo_in = fopen(FIFO_IN_FILE_NAME, "r");
		if (fifo_in == NULL)
		{
			printf("Failed to open input fifo %s, quitting\n", FIFO_IN_FILE_NAME);
			break;
		}
		fifo_out = fopen(FIFO_OUT_FILE_NAME, "w");
		if (fifo_out == NULL)
		{
			printf("Failed to open output fifo %s, quitting\n", FIFO_OUT_FILE_NAME);
			break;
		}

		/* Read the input FIFO until the other process disconnects */
		while(1)
		{
			p = fgets(buf, sizeof(buf), fifo_in);
			if (p)
			{
				printf("Listener read [%s]\n", buf);
				switch (buf[0])
				{
				case 'q':
					printf("Quitting\n");
					return (void *)0;
					break;

				case 'v':
					fprintf(fifo_out, "Fermenter Control version %u.%u\n", VERSION_MAJOR, VERSION_MINOR);
					break;
					
				default:
					break;
				}
			}
			else
			{
				if (feof(fifo_in))
				{
					printf("Client disconnected\n");
					fclose(fifo_in);
					break;
				}
			}
		}
	}
	
	return (void *)3;
}

void read_lock_file(int f)
{
	char buf[80];
	FILE *lock_file;
	char *p;
	
	sprintf(buf, LOCK_FILE_TEMPLATE, f);
	lock_file = fopen(buf, "r");
	
	if (!lock_file)
	{
		printf("Fermenter %d not running\n", f);
	}
	else
	{
		p = fgets(buf, sizeof(buf), lock_file);
		if (p)
		{
			printf("Fermenter %d was running: lock file contains [%s]\n", buf);
		}
		else
		{
			printf("Corrupt lock file for fermenter %d\n", f);
		}
	}
}
	
int main(int argc, const char *argv[])
{
	int i;
	pthread_t listener_thread, led_thread, fermenter_thread[NUM_FERMENTERS];
	FILE *control_fifo;
	void *listener_return;
	fermenter_t fermenter[NUM_FERMENTERS];
	
	printf("%s version %u.%u\n", argv[0], VERSION_MAJOR, VERSION_MINOR);
	for (i = 1; i < argc; ++i)
	{
		if (argv[i][0] == '-')
		{
			switch (argv[i][1])
			{
			case 'h':
				printf("Usage:\n%s [-h]\n\n-h: show this help information\n\n", argv[0]);
				return 1;
				break;
			default:
				printf("Bad flag -%c!\n", argv[i][1]);
				return 2;
				break;
			}
		}
	}
	
	/* Read the state of the fermenters, if running */
	for (i = 0; i < NUM_FERMENTERS; ++i)
	{
		fermenter[i].id      = fermenter_id[i];
		fermenter[i].gpio    = heater_index[i];
		fermenter[i].head    = NULL;
		fermenter[i].current = NULL;
		fermenter[i].command = FERMENTER_NO_COMMAND;
		read_lock_file(i);
	}
	
	init_gpio();
	
	/* Start the other threads */
	pthread_create(&listener_thread, NULL, listener, NULL);
	pthread_create(&led_thread, NULL, update_leds, NULL);
	pthread_create(&fermenter_thread[0], NULL, run_fermenter, &fermenter[0]);

	pthread_join(listener_thread, &listener_return);
	
	return (int)listener_return;
}
