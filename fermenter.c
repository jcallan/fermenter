#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include "fermenter.h"

#define VERSION_MAJOR		1
#define VERSION_MINOR		0

#define FIFO_IN_FILE_NAME	"/var/tmp/fermenter.in"
#define FIFO_OUT_FILE_NAME	"/var/tmp/fermenter.out"
#define LOCK_FILE_TEMPLATE	"/var/tmp/fermenter%u"
#define CSV_FILE_TEMPLATE	"/var/www/html/fermenter/fermenter%u_%03u.csv"

#define FILE_NAME_LENGTH	80
#define MAX_LOG_NO			999

#define DAEMON_GID			997			/* The gpio group */
#define DAEMON_UID			1001		/* jcallan */


const char fermenter_id[NUM_FERMENTERS] = {'A', 'B'};

typedef enum 
{
	FERMENTER_NO_COMMAND,
	FERMENTER_STOP,
	FERMENTER_START,
} fermenter_cmd_t;

typedef struct
{
	char id;
	unsigned index;
	volatile fermenter_cmd_t command;
	programme_t *head;
	programme_t *current;
	time_t programme_start_time;
	FILE *csv_file;
	char programme_file_name[FILE_NAME_LENGTH];
} fermenter_t;

void write_lock_file(const fermenter_t *f);
void read_lock_file(fermenter_t *f);
void delete_lock_file(const fermenter_t *f);

static fermenter_t fermenter[NUM_FERMENTERS];

void rotate_csv_files(unsigned fermenter_no, unsigned max_log_no)
{
	int i, ret;
	char new_file_name[FILE_NAME_LENGTH], old_file_name[FILE_NAME_LENGTH];
	struct stat stat_buf;
	
	/* If there is already space for this log file, we're done */
	sprintf(new_file_name, CSV_FILE_TEMPLATE, fermenter_no, 0);
	ret = stat(new_file_name, &stat_buf);
	if (ret)
	{
		if (errno == ENOENT)
		{
			printf("No CSV files need rotating\n");
		}
		else
		{
			perror("Cannot stat CSV file");
		}
		return;
	}
			
	/* First remove the highest possible filename */
	sprintf(old_file_name, CSV_FILE_TEMPLATE, fermenter_no, max_log_no);
	remove(old_file_name);
	
	/* Now rename each file using a higher number */
	for (i = max_log_no - 1; i >= 0; --i)
	{
		sprintf(new_file_name, CSV_FILE_TEMPLATE, fermenter_no, i + 1);
		sprintf(old_file_name, CSV_FILE_TEMPLATE, fermenter_no, i);
		rename(old_file_name, new_file_name);
	}
}	

void open_csv_file(fermenter_t *f)
{
	char file_name[FILE_NAME_LENGTH];
	struct stat stat_buf;
	int ret, need_header = 0;
	
	sprintf(file_name, CSV_FILE_TEMPLATE, f->index, 0);
	ret = stat(file_name, &stat_buf);
	if (ret)
	{
		if (errno == ENOENT)
		{
			need_header = 1;
		}
	}
	else
	{
		if (stat_buf.st_size == 0)
		{
			need_header = 1;
		}
	}
	
	f->csv_file = fopen(file_name, "a");
	
	if (!f->csv_file)
	{
		perror("Can't open CSV file");
	}
	else
	{
		/* Make the file line-buffered */
		setvbuf(f->csv_file, NULL, _IOLBF, 0);
		if (need_header)
		{
			fprintf(f->csv_file, "Time,Actual,Desired,Heat\n");
		}
	}
}

int start_fermenter(fermenter_t *f)
{
	int ret = 0;
	
	printf("Starting programme %s on fermenter %c\n", f->programme_file_name, f->id);
	f->head = load_programme(f->programme_file_name);
	if (f->head)
	{
		rotate_csv_files(f->index, MAX_LOG_NO);
		start_programme(f->head);
		f->programme_start_time = f->head->start_time;
		write_lock_file(f);
		open_csv_file(f);
	}
	else
	{
		printf("Failed to read programme!\n");
		ret = -1;
	}

	return ret;
}

void stop_fermenter(fermenter_t *f)
{
	printf("Stopping programme on fermenter %c\n", f->id);
	set_heater(f->index, 0);
	f->head = f->current = NULL;
	f->programme_file_name[0] = 0;
	f->programme_start_time   = 0;
	delete_lock_file(f);
	if (f->csv_file)
	{
		fclose(f->csv_file);
		f->csv_file = NULL;
	}
}

void *run_fermenter(void *arg)
{
	fermenter_t *f = (fermenter_t *)arg;
	int heat = 0;
	float t_actual, t_desired;
	time_t now, target_time;

	/* Initialise the state of the fermenter */
	f->id       = fermenter_id[f->index];
	f->csv_file = NULL;
	f->head     = NULL;
	f->current  = NULL;
	f->command  = FERMENTER_NO_COMMAND;
	f->programme_file_name[0] = 0;
	f->programme_start_time   = 0;
	set_heater(f->index, 0);

	/* Do we need to restart a programme that was interrupted? */
	read_lock_file(f);
	if (f->programme_start_time && f->programme_file_name[0])
	{
		/* Attept to restart programme */
		f->head = load_programme(f->programme_file_name);
		if (f->head)
		{
			restart_programme(f->head, f->programme_start_time);
			open_csv_file(f);
		}
		else
		{
			printf("Failed to reload programme from %s\n", f->programme_file_name);
		}
	}
	
	target_time = time(NULL);
	
	while (1)
	{
		now = time(NULL);
		if (now >= target_time && f->head)
		{
			t_actual = read_temperature(f->index);
			t_desired = programme_temperature(f->head, now);
			if (t_desired < -10.0)
			{
				/* We have reached the end of the programme */
				f->command = FERMENTER_STOP;
			}
			else
			{
				heat = t_actual < t_desired;
				printf("Fermenter %c: actual %.2f, desired %.2f, %s\n",
					   f->id, t_actual, t_desired, heat ? "on" : "off");
				fprintf(f->csv_file, "%lu,%.2f,%.2f,%d\n", now, t_actual, t_desired, heat);
				set_heater(f->index, heat);
				fflush(stdout);
				fflush(f->csv_file);
			}
			target_time += TIME_INCREMENT_S;
		}
		sleep(1);
		if (f->command == FERMENTER_STOP)
		{
			stop_fermenter(f);
		}
		if (f->command == FERMENTER_START)
		{
			start_fermenter(f);
			target_time = now;
		}
		f->command = FERMENTER_NO_COMMAND;
	}
	
	return NULL;
}

void *listener(void *arg)
{
	FILE *fifo_in, *fifo_out;
	char buf[80];
	int ret;
	char *p;
	unsigned fermenter_no, length;
	
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
		/* Make the file line-buffered */
		setvbuf(fifo_out, NULL, _IOLBF, 0);

		/* Read the input FIFO until the other process disconnects */
		while(1)
		{
			p = fgets(buf, sizeof(buf), fifo_in);
			if (p)
			{
				printf("Listener read [%s]\n", buf);
				length = strlen(buf);
				
				switch (buf[0])
				{
				case 'q':
					printf("Quitting\n");
					return (void *)0;
					break;

				case 'v':
					fprintf(fifo_out, "Fermenter Control version %u.%u\n", VERSION_MAJOR, VERSION_MINOR);
					break;
					
				case 'p':
					if (length >= 3)
					{
						fermenter_no = buf[1] - '0';
						if (fermenter_no < NUM_FERMENTERS)
						{
							if (fermenter[fermenter_no].head == NULL)
							{
								realpath(&buf[2], fermenter[fermenter_no].programme_file_name);
								fermenter[fermenter_no].command = FERMENTER_START;
								fprintf(fifo_out, "Starting programme [%s] on fermenter %u\n",
										fermenter[fermenter_no].programme_file_name, fermenter_no);
							}
						}
					}
					break;
					
				case 's':
					if (length >= 2)
					{
						fermenter_no = buf[1] - '0';
						if (fermenter_no < NUM_FERMENTERS)
						{
							fermenter[fermenter_no].command = FERMENTER_STOP;
							fprintf(fifo_out, "Stopping programme [%s] on fermenter %u\n",
									fermenter[fermenter_no].programme_file_name, fermenter_no);
						}
					}
					break;
					
				case 'i':
					if (length >= 2)
					{
						fermenter_no = buf[1] - '0';
						if (fermenter_no < NUM_FERMENTERS)
						{
							if (fermenter[fermenter_no].head)
							{
								fprintf(fifo_out, "Running programme [%s] on fermenter %u\n",
										fermenter[fermenter_no].programme_file_name, fermenter_no);
							}
							else
							{
								fprintf(fifo_out, "Fermenter %u is stopped\n", fermenter_no);
							}
							
						}
					}
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

void delete_lock_file(const fermenter_t *f)
{
	char file_name[FILE_NAME_LENGTH];

	sprintf(file_name, LOCK_FILE_TEMPLATE, f->index);
	unlink(file_name);
}

int main(int argc, const char *argv[])
{
	int i, ret;
	pthread_t listener_thread, led_thread, fermenter_thread[NUM_FERMENTERS];
	void *listener_return;
	
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
	
	/* Start the other threads */
	pthread_create(&listener_thread, NULL, listener, NULL);
	pthread_create(&led_thread, NULL, update_leds, NULL);
	for (i = 0; i < NUM_FERMENTERS; ++i)
	{
		fermenter[i].index = i;
		pthread_create(&fermenter_thread[i], NULL, run_fermenter, &fermenter[i]);
	}

	pthread_join(listener_thread, &listener_return);
	
	return (int)listener_return;
}
