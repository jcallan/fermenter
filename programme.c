#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include "fermenter.h"

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

float programme_temperature(programme_t *prog, time_t now)
{
	float temperature = 0.0, done_fraction;
	time_t end_time;
	
	while (prog)
	{
		end_time = prog->start_time + prog->length * TIME_INCREMENT_S;
		if ((prog->start_time <= now) && (end_time >= now))
		{
			if (end_time == prog->start_time)
			{
				done_fraction = 1.0;
			}
			else
			{
				done_fraction = (now - prog->start_time) / (end_time - prog->start_time);
			}
			temperature = prog->start_temp * (1.0 - done_fraction) + prog->end_temp * done_fraction;
			break;
		}
		prog = prog->next;
	}
	
	return temperature;
}
