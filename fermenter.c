#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include "fermenter.h"

#define VERSION_MAJOR		0
#define VERSION_MINOR		1

#define FIFO_IN_FILE_NAME	"/var/tmp/fermenter.in"
#define FIFO_OUT_FILE_NAME	"/var/tmp/fermenter.out"
#define LOCK_FILE_TEMPLATE	"/var/tmp/fermenter%u"

const char fermenter_id[NUM_FERMENTERS] = {'A', 'B'};

typedef enum 
{
	FERMENTER_NO_COMMAND,
	FERMENTER_STOP,
} fermenter_cmd_t;

typedef struct
{
	char id;
	unsigned index;
	volatile fermenter_cmd_t command;
	programme_t *head;
	programme_t *current;
} fermenter_t;

void *run_fermenter(void *arg)
{
	fermenter_t *f = (fermenter_t *)arg;
	int heat = 0;
	float t_actual, t_desired;
	time_t now;
	
	f->head = load_programme("cb1.programme");
	start_programme(f->head);
	
	while (f->command == FERMENTER_NO_COMMAND)
	{
		now = time(NULL);
		t_actual = read_temperature(f->index);
		t_desired = programme_temperature(f->head, now);
		printf("Fermenter %c: actual %.2f, desired %.2f\n", t_actual, t_desired);
		set_heater(f->index, t_actual < t_desired);
		sleep(60);
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
	int i, ret;
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
		fermenter[i].index   = i;
		fermenter[i].head    = NULL;
		fermenter[i].current = NULL;
		fermenter[i].command = FERMENTER_NO_COMMAND;
		read_lock_file(i);
	}
	
	ret = init_gpio();
	if (ret != 0)
	{
		return ret;
	}
	
	/* Start the other threads */
	pthread_create(&listener_thread, NULL, listener, NULL);
	pthread_create(&led_thread, NULL, update_leds, NULL);
	pthread_create(&fermenter_thread[0], NULL, run_fermenter, &fermenter[0]);

	pthread_join(listener_thread, &listener_return);
	
	return (int)listener_return;
}
