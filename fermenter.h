
#define TIME_INCREMENT_S	60	/* TODO use 3600 and allow floating point hours */

typedef struct programme_s
{
	float start_temp;
	float end_temp;
	time_t start_time;
	unsigned length;
	struct programme_s *next;
} programme_t;

extern programme_t *load_programme(const char *file_name);
extern void start_programme(programme_t *head);
extern void free_programme(programme_t *head);
