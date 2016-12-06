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

#define FILE_NAME_LENGTH	80

#define DAEMON_GID			997			/* The gpio group */
#define DAEMON_UID			1001		/* jcallan */


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
	time_t programme_start_time;
	char programme_file_name[FILE_NAME_LENGTH];
} fermenter_t;

void write_lock_file(const fermenter_t *f);
void read_lock_file(fermenter_t *f);


void *run_fermenter(void *arg)
{
	fermenter_t *f = (fermenter_t *)arg;
	int heat = 0, ret;
	float t_actual, t_desired;
	time_t now;

	f->programme_start_time = 0;
	read_lock_file(f);
	if (f->programme_start_time && f->programme_file_name[0])
	{
		/* Attept to restart running programme */
		f->head = load_programme(f->programme_file_name);
		if (f->head)
		{
			restart_programme(f->head, f->programme_start_time);
		}
		else
		{
			printf("Failed to reload programme from %s\n", f->programme_file_name);
		}
	}
	else
	{
		/* For now, start a fixed programme */
		realpath("cb1.programme", f->programme_file_name);
		f->head = load_programme(f->programme_file_name);
		if (f->head)
		{
			start_programme(f->head);
			f->programme_start_time = f->head->start_time;
		}
		else
		{
			printf("Failed to read programme!\n");
		}
	}
	
	write_lock_file(f);

	
	while (f->command == FERMENTER_NO_COMMAND)
	{
		now = time(NULL);
		t_actual = read_temperature(f->index);
		t_desired = programme_temperature(f->head, now);
		heat = t_actual < t_desired;
		printf("Fermenter %c: actual %.2f, desired %.2f, %s\n",
			   f->id, t_actual, t_desired, heat ? "on" : "off");
		set_heater(f->index, heat);
		fflush(stdout);
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

void read_lock_file(fermenter_t *f)
{
	char lock_file_name[FILE_NAME_LENGTH], buf[FILE_NAME_LENGTH + 15];
	FILE *lock_file;
	char *p;
	time_t start_time;
	int ret;
	
	sprintf(lock_file_name, LOCK_FILE_TEMPLATE, f->index);
	lock_file = fopen(lock_file_name, "r");
	
	if (!lock_file)
	{
		printf("Fermenter %c not running\n", f->id);
	}
	else
	{
		p = fgets(buf, sizeof(buf), lock_file);
		if (p)
		{
			printf("Fermenter %c was running: lock file contains %s\n", f->id, buf);
			ret = sscanf(buf, "%lu %80s", &f->programme_start_time, f->programme_file_name);
			if (ret != 2)
			{
				printf("Failed to read 2 arguments from lock file\n");
				f->programme_file_name[0] = 0;
				f->programme_start_time = 0;
			}
		}
		else
		{
			printf("Corrupt lock file for fermenter %c\n", f->id);
		}
		fclose(lock_file);
		unlink(lock_file_name);
	}
}

void write_lock_file(const fermenter_t *f)
{
	char file_name[FILE_NAME_LENGTH];
	FILE *lock_file;

	sprintf(file_name, LOCK_FILE_TEMPLATE, f->index);
	lock_file = fopen(file_name, "w");
	
	if (!lock_file)
	{
		printf("Could not create lock file %s\n", file_name);
	}
	else
	{
		fprintf(lock_file, "%lu %s\n", f->programme_start_time, f->programme_file_name);
		fclose(lock_file);
	}
}

int main(int argc, const char *argv[])
{
	int i, ret;
	pthread_t listener_thread, led_thread, fermenter_thread[NUM_FERMENTERS];
	FILE *control_fifo;
	void *listener_return;
	fermenter_t fermenter[NUM_FERMENTERS];
	
	/*  Drop superuser privileges */
	if (setgid(DAEMON_GID) == -1)
	{
		perror("Failed to setguid");
	}
	if (setuid(DAEMON_UID) == -1)
	{
		perror("Failed to setuid\n");
	}

	/* Make sure our lock files can be deleted by anyone */
	umask(0111);
	
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
	
	ret = init_gpio();
	if (ret != 0)
	{
		return ret;
	}
	
	/* Initialise the state of the fermenters, if running */
	for (i = 0; i < NUM_FERMENTERS; ++i)
	{
		fermenter[i].id      = fermenter_id[i];
		fermenter[i].index   = i;
		fermenter[i].head    = NULL;
		fermenter[i].current = NULL;
		fermenter[i].command = FERMENTER_NO_COMMAND;
		fermenter[i].programme_file_name[0] = 0;
		fermenter[i].programme_start_time   = 0;
		set_heater(i, 0);
	}
	
	/* Start the other threads */
	pthread_create(&listener_thread, NULL, listener, NULL);
	pthread_create(&led_thread, NULL, update_leds, NULL);
	pthread_create(&fermenter_thread[0], NULL, run_fermenter, &fermenter[0]);

	pthread_join(listener_thread, &listener_return);
	
	return (int)listener_return;
}
