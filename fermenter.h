
#define NUM_FERMENTERS			2
#define NUM_LEDS				3

#define TIME_INCREMENT_S		60
#define SECONDS_PER_TIME_UNIT	3600

#define LED_A					0
#define LED_B					1
#define LED_C					2

typedef struct programme_s
{
	float start_temp;
	float end_temp;
	time_t start_time;
	float length;
	struct programme_s *next;
} programme_t;

extern programme_t *load_programme(const char *file_name);
extern void start_programme(programme_t *head);
extern void free_programme(programme_t *head);
extern float programme_temperature(programme_t *prog, time_t now);

extern void *update_leds(void *arg);
extern int init_gpio(void);
extern void set_heater(unsigned index, int on);
extern float read_temperature(unsigned index);
