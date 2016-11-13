#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#define VERSION_MAJOR		0
#define VERSION_MINOR		1

#define NUM_FERMENTERS		2
#define NUM_LEDS			3
#define TIME_INCREMENT_S	60

#define FIFO_FILE_NAME		"/var/run/fermenter"
#define LOCK_FILE_TEMPLATE	"/var/tmp/fermenter%u"
#define LED_FILE_TEMPLATE	"/sys/class/gpio/gpio%u/value"

void *update_leds(void *arg)
{
	FILE *led[NUM_LEDS];
	const unsigned led_index[NUM_LEDS] = {2, 3, 4};
	char buf[80];
	int blink = 0;
	
	sprintf(buf, LED_FILE_TEMPLATE, led_index[0]);
	led[0] = fopen(buf, "w");
	
	if (led[0] == NULL)
	{
		printf("Could not open led[%u]\n", 0);
		return (void *)4;
	}
		
	while(1)
	{
		sleep(1);
		blink = 1 - blink;
		fprintf(led[0], "%d", blink);
		fflush(led[0]);
		printf(".");
		fflush(stdout);
	}
}

void *listener(void *arg)
{
	FILE *fifo;
	char buf[80];
	int ret;
	char *p;
	
	while(1)
	{
		/* Open the control FIFO */
		fifo = fopen(FIFO_FILE_NAME, "r");
		if (fifo == NULL)
		{
			printf("Failed to open control fifo %s, quitting\n", FIFO_FILE_NAME);
			break;
		}

		/* Read the FIFO until the other process disconnects */
		while(1)
		{
			p = fgets(buf, sizeof(buf), fifo);
			if (p)
			{
				printf("Listener read [%s]\n", buf);
				switch (buf[0])
				{
				case 'q':
					printf("Quitting\n");
					return (void *)0;
					break;

				default:
					break;
				}
			}
			else
			{
				printf("Listener got NULL on fgets() call\n");
				if (feof(fifo))
				{
					fclose(fifo);
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
	pthread_t listener_thread, led_thread;
	FILE *control_fifo;
	void *listener_return;
	
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
		read_lock_file(i);
	}
	
	/* Start the other threads */
	pthread_create(&listener_thread, NULL, listener, NULL);
	pthread_create(&led_thread, NULL, update_leds, NULL);

	pthread_join(listener_thread, &listener_return);
	
	return (int)listener_return;
}
