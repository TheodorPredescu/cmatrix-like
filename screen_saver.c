#include <asm-generic/ioctls.h>
#include <bits/time.h>
#include <signal.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <stdatomic.h>

#define RAINDROP_MAX 4096
#define RAINDROP_MAX_HEIGHT 8
#define DROP_SCREEN_CONCENTRATION 0.05f
#define FPS 15
#define ONE_FRAME_MICROS 1000000 / FPS

#define DISPLAY_BUFFER_SIZE 10000000

struct rain_drop {
	int is_active;

	int x;
	int y;
};

float drop_miss_procentage = 0.9995f;
float STEP_DROP_MISS_PROCENTAGE = 0.00001f;
float UPPER_TRIGGER_DROP_MISS_PROCENTAGE = 0.9f;
float LOWER_TRIGGER_DROP_MISS_PROCENTAGE = 0.7f;

int previous_total_drops = -1;

unsigned short term_row;
unsigned short term_col;

struct rain_drop dropsPool[RAINDROP_MAX];

static atomic_int_fast16_t end_program = 1;
void close_program(int sig)
{
	(void)sig;

	end_program = 0;
}

void end_sequence(char *displayBuffer)
{
	free(displayBuffer);
	// move back to the original screen buffer
	printf("\033[?1049l\033[?25h");

	printf("\033[49m");
	fflush(stdout);
}

void update_dimensions(int sig)
{
	(void)sig;

	struct winsize ts;
	ioctl(0, TIOCGWINSZ, &ts);

	term_row = ts.ws_row;
	term_col = ts.ws_col;
}

char random_character()
{
	int random = rand() % (122 - 33);
	return (char)(33 + random);
}

void display_drop(const struct rain_drop *drop, char *const displayBuffer,
		  unsigned int *displayBufferPos)
{
	if (drop->x > term_col)
		return;

	for (int i = 0; i < RAINDROP_MAX_HEIGHT; i++) {
		if (drop->y - i >= 1 && drop->y - i <= term_row)
			*displayBufferPos += snprintf(
				displayBuffer + *displayBufferPos,
				DISPLAY_BUFFER_SIZE - *displayBufferPos,
				"\033[%d;%dH\033[38;2;0;%d;%dm%c", drop->y - i,
				drop->x, 255 - i * 26, 65 - i * 8,
				random_character());
	}

	const int previous_pos = drop->y - RAINDROP_MAX_HEIGHT;
	if (previous_pos >= 1 && previous_pos <= term_row) {
		*displayBufferPos +=
			snprintf(displayBuffer + *displayBufferPos,
				 DISPLAY_BUFFER_SIZE - *displayBufferPos,
				 "\033[%d;%dH\033[49m ", previous_pos, drop->x);
	}
}

int is_on_screen(const struct rain_drop *const drop)
{
	return drop->x <= term_col && drop->y - RAINDROP_MAX_HEIGHT <= term_row;
}

int should_advance(const struct rain_drop *const drop)
{
	return drop->is_active;
}

void drop_advance(struct rain_drop *const drop)
{
	drop->y++;
}

int update_current_drops(char *displayBuffer, unsigned int *displayCommandsPos)
{
	if (previous_total_drops == -1)
		return 0;

	int current_drops_cnt = 0;
	int prev_drops_cnt = 0;

	for (int i = 0; i < RAINDROP_MAX; i++) {
		struct rain_drop *const drop = &dropsPool[i];

		if (drop->is_active == 0)
			continue;

		// Is out of the screen after the zoom.
		if (!is_on_screen(drop)) {
			if (drop->is_active == 1) {
				drop->is_active = 0;
				prev_drops_cnt++;
			}
			continue;
		}

		current_drops_cnt++;

		if (drop->is_active == 1) {
			prev_drops_cnt++;
		} else {
			drop->is_active = 1;
		}

		drop_advance(drop);
		display_drop(drop, displayBuffer, displayCommandsPos);

		if (prev_drops_cnt >= previous_total_drops)
			break;
	}

	return current_drops_cnt;
}

int add_new_drops(char *displayBuffer, unsigned int *displayCommandsPos,
		  int total_drops, int drops_displayed)
{
	int index = 0;
	while (drops_displayed < total_drops && index < RAINDROP_MAX) {
		struct rain_drop *const drop = &dropsPool[index++];

		if (drop->is_active == 1)
			continue;

		float procentage = (float)rand() / RAND_MAX;
		if (procentage < drop_miss_procentage)
			continue;

		drops_displayed++;

		drop->y = 1;
		drop->x = (rand() % term_col) + 1;
		drop->is_active = 1;

		display_drop(drop, displayBuffer, displayCommandsPos);
	}

	return drops_displayed;
}

void recalibrade_drop_proc(int total_possible, int actual_dropped)
{
	// recalibrate the drop procentage
	float procentage_dropped = (float)actual_dropped / total_possible;
	if (procentage_dropped > UPPER_TRIGGER_DROP_MISS_PROCENTAGE)
		drop_miss_procentage += STEP_DROP_MISS_PROCENTAGE;
	else if (procentage_dropped < LOWER_TRIGGER_DROP_MISS_PROCENTAGE)
		drop_miss_procentage -= STEP_DROP_MISS_PROCENTAGE;
}

void main_loop(char *displayBuffer)
{
	int total_drops = term_col * term_row * DROP_SCREEN_CONCENTRATION;
	unsigned int displayCommandsPos = 0;

	int drops_displayed =
		update_current_drops(displayBuffer, &displayCommandsPos);

	int updated_drops_displayed =
		add_new_drops(displayBuffer, &displayCommandsPos, total_drops,
			      drops_displayed);

	recalibrade_drop_proc(total_drops, updated_drops_displayed);

	fwrite(displayBuffer, 1, displayCommandsPos, stdout);

	// printf("\033[1;1Htotal_drops%d displayed: %d  \nrow: %d col: %d\ndisplay com pos%d",
	//        total_drops, valid_drops_cnt, terminal_row, terminal_col,
	//        displayCommandsPos);

	fflush(stdout);

	previous_total_drops = total_drops;
}

int main()
{
	signal(SIGWINCH, update_dimensions);
	signal(SIGTERM, close_program);
	signal(SIGINT, close_program);

	srand(time(NULL));

	update_dimensions(0);

	// save screen to alternate buffer and deactivate the cursor
	printf("\033[?1049h\033[?25l");

	printf("\033[40m");

	char *displayBuffer = malloc(DISPLAY_BUFFER_SIZE);
	if (displayBuffer == NULL) {
		printf("Faild to asign on boot %d space", DISPLAY_BUFFER_SIZE);
		return -1;
	}

	while (end_program) {
		struct timespec start;
		clock_gettime(CLOCK_REALTIME, &start);

		main_loop(displayBuffer);

		struct timespec end;
		clock_gettime(CLOCK_REALTIME, &end);

		const long long elapsed_micro =
			(end.tv_sec - start.tv_sec) * 1000000 +
			(end.tv_nsec - start.tv_nsec) / 1000;
		if (elapsed_micro < ONE_FRAME_MICROS)
			usleep(ONE_FRAME_MICROS - elapsed_micro);
	}

	end_sequence(displayBuffer);

	return 0;
}
