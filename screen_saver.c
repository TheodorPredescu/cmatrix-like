#include <asm-generic/ioctls.h>
#include <bits/time.h>
#include <signal.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#define RAINDROP_MAX 1024
#define RAINDROP_MAX_HEIGHT 8
#define RAINDROP_COL_CONCENTRATION 0.2f
#define FPS 30
#define ONE_FRAME_MICROS 1000000 / FPS
#define NEW_DROP_PROCENTAGE 0.95

unsigned short terminal_row;
unsigned short terminal_col;

struct rain_drop {
	int displayed_elements;
	int is_active;

	int x;
	int y;
};

struct buffers {
	int dropsToUpdate[RAINDROP_MAX];
	unsigned int dropsToUpdateLen;
	// char displayCommands[100000];
	// unsigned int displayCommandsPos;
};

struct rain_drop dropsPool[RAINDROP_MAX];
// int dropsActiveIndex[RAINDROP_MAX + 1];
int active_count = 0;

void close_program()
{
	// move back to the original screen buffer
	printf("\033[?1049l\033[?25h");

	printf("\033[49m");
	fflush(stdout);
	exit(1);
}

// TODO: I need to clear the buffer here also. It remains there uncleared.
void clear_extra_drops(int new_total_drops, int old_total_drops)
{
	if (new_total_drops >= old_total_drops)
		return;

	char clearBuffer[100000];
	unsigned int clearBufferPos = 0;
	// int activeIndexPos = 0;
	active_count = 0;

	// TODO: I need to not che
	for (int index = 0; index < RAINDROP_MAX; index++) {
		if (dropsPool[index].is_active == 0)
			continue;

		if (dropsPool[index].x <= terminal_col &&
		    dropsPool[index].y <= terminal_row)
			continue;

		for (int i = 0; i < RAINDROP_MAX_HEIGHT; i++) {
			int y = dropsPool[index].y - i;

			if (y < 1 || y > terminal_row)
				continue;
			clearBufferPos += snprintf(
				clearBuffer + clearBufferPos,
				sizeof(clearBuffer) - clearBufferPos,
				"\033[%d;%dH\033[49m ", y, dropsPool[index].x);
		}

		dropsPool[index].is_active = 0;
	}
	// for (int index = new_total_drops; index < old_total_drops; index++) {
	// 	dropsPool[index].is_active = 0;
	//
	// 	for (int i = 0; i < RAINDROP_MAX_HEIGHT; i++) {
	// 		clearBufferPos += snprintf(clearBuffer + clearBufferPos,
	// 					   100000 - clearBufferPos,
	// 					   "\033[%d;%dH\033[49m ",
	// 					   dropsPool[index].y - i,
	// 					   dropsPool[index].x);
	// 	}
	// }

	fwrite(clearBuffer, 1, clearBufferPos, stdout);
}

unsigned short should_update_dim = 0;
void update_dimensions(int signal)
{
	struct winsize ts;
	ioctl(0, TIOCGWINSZ, &ts);

	const unsigned short new_row = ts.ws_row;
	const unsigned short new_col = ts.ws_col;

	const unsigned short old_col = terminal_col;

	terminal_row = new_row;
	terminal_col = new_col;

	if (signal != -1)
		should_update_dim = old_col;
}

void display_drop(const struct rain_drop drop, char *const displayBuffer,
		  unsigned int *displayBufferPos)
{
	if (drop.x > terminal_col)
		return;

	for (int i = 0; i < RAINDROP_MAX_HEIGHT; i++) {
		if (drop.y - i >= 1 && drop.y - i <= terminal_row)
			*displayBufferPos +=
				snprintf(displayBuffer + *displayBufferPos,
					 100000 - *displayBufferPos,
					 "\033[%d;%dH\033[38;5;%dm*",
					 drop.y - i, drop.x, 255 - i * 3);
	}

	const int previous_pos = drop.y - RAINDROP_MAX_HEIGHT;
	if (previous_pos >= 1 && previous_pos <= terminal_row) {
		*displayBufferPos += snprintf(displayBuffer + *displayBufferPos,
					      100000 - *displayBufferPos,
					      // "\033[%d;%dH\033[1X\033[49m ",
					      "\033[%d;%dH\033[49m ",
					      previous_pos, drop.x);
	}
}

int is_valid_drop(const struct rain_drop drop)
{
	return drop.x <= terminal_col;
}

int should_advance(const struct rain_drop drop)
{
	return drop.is_active && drop.y - RAINDROP_MAX_HEIGHT <= terminal_row;
}

void drop_advance(struct rain_drop *const drop)
{
	drop->y++;
}

// void add_new_drops(struct buffers *buffers)
// // void add_new_drops(const int newDrops[], const unsigned int newDropsLen,
// // 		   char *const displayBuffer, int *const displayBufferPos)
// {
// 	int dropsToDisplay[RAINDROP_MAX];
// 	int dropsToDisplayLen = 0;
//
// 	// Add new drop this frame if available
// 	for (unsigned int i = 0; i < buffers->dropsToUpdateLen; i++) {
// 		// Because I can have multiple new frames, I do not want all of
// 		// them to be displayed on the next new row. Add a low
// 		// probability of display for each one.
// 		if ((float)rand() / RAND_MAX < NEW_DROP_PROCENTAGE)
// 			continue;
//
// 		int newPos = buffers->dropsToUpdate[i];
// 		dropsPool[newPos].y = 1;
// 		dropsPool[newPos].x = rand() % terminal_col;
//
// 		dropsPool[newPos].is_active = 1;
//
// 		dropsToDisplay[dropsToDisplayLen++] = newPos;
//
// 		display_drop(dropsPool[newPos], buffers->displayCommands,
// 			     &buffers->displayCommandsPos);
// 	}
// }

unsigned int update_drops(char *displayBuffer)
// int *update_current_drops(int *const dropsToUpdate, char *const displayBuffer,
// 			  int *displayBufferPos)
{
	int total_drops = terminal_col * RAINDROP_COL_CONCENTRATION;
	// static int lenghts[2];

	// int dropsToUpdateLen = 0;
	// buffer->dropsToUpdateLen = 0;
	unsigned int displayCommandsPos = 0;

	/// I do not go through all the drops if I zoom in and the
	// for (int i = 0; i < RAINDROP_MAX + 1; i++) {
	// 	const int index = dropsActiveIndex[i];
	// 	if (index == -1)
	// 		break;

	// TODO: I need to check if all the displayed drops have finished
	for (int i = 0; i < RAINDROP_MAX; i++) {
		if (!is_valid_drop(dropsPool[i])) {
			continue;
		}

		if (total_drops <= 0) {
			break;
		}
		total_drops--;

		if (should_advance(dropsPool[i])) {
			drop_advance(&dropsPool[i]);
			display_drop(dropsPool[i], displayBuffer,
				     &displayCommandsPos);
			continue;
		}

		dropsPool[i].is_active = 0;
		// buffer->dropsToUpdate[buffer->dropsToUpdateLen++] = i;

		if ((float)rand() / RAND_MAX < NEW_DROP_PROCENTAGE)
			continue;

		// int newPos = buffers->dropsToUpdate[i];
		dropsPool[i].y = 1;
		dropsPool[i].x = (rand() % terminal_col) + 1;

		dropsPool[i].is_active = 1;

		display_drop(dropsPool[i], displayBuffer, &displayCommandsPos);
	}
	// lenghts[0] = dropsToUpdateLen;
	// lenghts[1] = *displayBufferPos;

	return displayCommandsPos;
}

void check_dim_change()
{
	// TODO:
	if (should_update_dim == 0)
		return;

	clear_extra_drops(terminal_col * RAINDROP_COL_CONCENTRATION,
			  should_update_dim * RAINDROP_COL_CONCENTRATION);

	should_update_dim = 0;
}

int main()
{
	signal(SIGWINCH, update_dimensions);
	signal(SIGTERM, close_program);
	signal(SIGINT, close_program);

	srand(time(NULL));

	update_dimensions(-1);

	// save screen to alternate buffer and deactivate the cursor
	printf("\033[?1049h\033[?25l");

	printf("\033[40m");

	// int dropsToUpdate[RAINDROP_MAX];
	// unsigned int dropsToUpdateLen;

	struct buffers buffers;
	char displayBuffer[100000];
	// int displayBufferPos;

	while (1) {
		struct timespec start;
		clock_gettime(CLOCK_REALTIME, &start);

		// Reset the list of elements that should be updated.
		// dropsToUpdateLen = 0;
		// displayBufferPos = 0;

		check_dim_change();
		// int dropsToUpdateLen
		// int *lengths = update_current_drops(
		// 	dropsToUpdate, displayBuffer, &displayBufferPos);

		const unsigned int displayLen = update_drops(displayBuffer);
		// add_new_drops(&buffers);
		// add_new_drops(dropsToUpdate, dropsToUpdateLen, displayBuffer,
		// 	      &displayBufferPos);

		fwrite(displayBuffer, 1, displayLen, stdout);
		fflush(stdout);

		struct timespec end;
		clock_gettime(CLOCK_REALTIME, &end);

		const long long elapsed_micro =
			(end.tv_sec - start.tv_sec) * 1000000 +
			(end.tv_nsec - start.tv_nsec) / 1000;
		if (elapsed_micro < ONE_FRAME_MICROS)
			usleep(ONE_FRAME_MICROS - elapsed_micro);
	}
}
