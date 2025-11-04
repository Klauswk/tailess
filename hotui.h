#ifndef HOTUI_H_
#define HOTUI_H_
#include <sys/ioctl.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <signal.h>

typedef enum {
  NONE,
  RESIZE,
} Hui_Event;

typedef struct Hui_Window {
  size_t width;
  size_t height;
  size_t x;
  size_t y;
} Hui_Window;

void hui_push_event(Hui_Event event);
Hui_Event hui_poll_event();
void hui_print_sz(char* string, size_t size);
void hui_print(char* string);
void hui_clear_window();
Hui_Window hui_init();
void hui_set_window_size(Hui_Window* window);
Hui_Window hui_create_window(int width, int height, int y, int x);
void hui_move_cursor_to(int y, int x);
void hui_put_text_at(char* c, size_t size,int y, int x);
void hui_put_character_at(char c, int y, int x);
void hui_put_text_at_window(Hui_Window window, char* c, size_t size, size_t y, size_t x);
void hui_put_character_at_window(Hui_Window window, char c, size_t y, size_t x);
void hui_draw_border_at_window(Hui_Window window);
#endif // HOTUI_H_

#ifdef HOTUI_IMPLEMENTATION

static int output_fd;
static struct termios initial;
static uint16_t terminal_width;
static uint16_t terminal_height;

#define EVENT_QUEUE_SIZE 256
static Hui_Event hui_event_queue[EVENT_QUEUE_SIZE];
static size_t hui_event_cursor_write_index = 0;
static size_t hui_event_cursor_read_index = 0;

void push_event(Hui_Event event) {
  if (hui_event_cursor_write_index >= EVENT_QUEUE_SIZE) {
    hui_event_cursor_write_index = 0;
  }

  hui_event_queue[hui_event_cursor_write_index++] = event;

  if (hui_event_cursor_write_index == hui_event_cursor_read_index) {
    hui_event_cursor_read_index++;
  }
}

Hui_Event hui_poll_event() {
  if (hui_event_cursor_read_index >= EVENT_QUEUE_SIZE) {
    hui_event_cursor_read_index = 0;
  }

  if (hui_event_cursor_write_index == hui_event_cursor_read_index) {
    return NONE;
  } 

  return hui_event_queue[hui_event_cursor_read_index++];
}

static void hui_resize(int i) {
  (void)i;
  struct winsize ws;
  ioctl(1, TIOCGWINSZ, &ws);
  terminal_width = ws.ws_col;
  terminal_height = ws.ws_row;
  push_event(RESIZE);
}

void hui_print_sz(char* string, size_t size) {
 write(output_fd, string, size); 
}

void hui_print(char* string) {
  hui_print_sz(string, strlen(string));
}

void hui_clear_window() {
  char* clean_buffer = "\x1b[2J";
  hui_print(clean_buffer);
}

static void hui_restore() {
	tcsetattr(1, TCSANOW, &initial);
  hui_clear_window();

  char* switch_back_to_terminal = "\x1b[?1049l";

  hui_print(switch_back_to_terminal);

  char* show_cursor = "\x1b[?25h";

  hui_print(show_cursor);
}

static void hui_die(int i) {
(void)i;
	exit(1);
}


Hui_Window hui_init() {
	struct termios term;
	tcgetattr(1, &term);
	initial = term;
	atexit(hui_restore);
  signal(SIGWINCH, hui_resize);
	signal(SIGTERM, hui_die);
	signal(SIGINT, hui_die);
	term.c_lflag &= (~ECHO & ~ICANON);
	tcsetattr(1, TCSANOW, &term);
  output_fd = 1;
  hui_resize(0);
  
  char* enter_alternate_buffer = "\x1b[?1049h";
  
  hui_print(enter_alternate_buffer);

  char* clean_buffer = "\x1b]2J";
  hui_print(clean_buffer);

  char* hide_cursor = "\x1b[?25l";
  hui_print(hide_cursor);

  Hui_Window window = {
    .width = terminal_width,
    .height = terminal_height,
    .x = 0,
    .y = 0,
  };

  return window;
}

void hui_set_window_size(Hui_Window* window) {
  window->height = terminal_height;
  window->width = terminal_width;
}

Hui_Window hui_create_window(int width, int height, int y, int x) {
  return (Hui_Window) {
    .width = width,
    .height = height,
    .y = y,
    .x = x,
  };
}

void hui_move_cursor_to(int y, int x) {
  y++;
  x++;
  char buffer[50];
  hui_print("\x1b[");   
  sprintf(buffer, "\x1b[%d;%dH", y, x);
  hui_print(buffer); 
}

/*
 * We assume 0 based index 
 */
void hui_put_text_at(char* c, size_t size,int y, int x) {
  hui_move_cursor_to(y,x);
  write(1, c, size);
}

/*
 * We assume 0 based index 
 */
void hui_put_character_at(char c, int y, int x) {
  hui_move_cursor_to(y,x);
  write(1, &c, 1);
}

/*
 * We assume 0 based index 
 */
void hui_put_text_at_window(Hui_Window window, char* c, size_t size, size_t y, size_t x) {
  int within_y_boundaries = y >= window.y && y < window.y + window.height;
  int within_x_boundaries = x >= window.x && x < window.x + window.width;
  if (within_x_boundaries && within_y_boundaries) {
    hui_move_cursor_to(window.y + y, window.x + x);
    write(1, c, size);
  }
}

/*
 * We assume 0 based index 
 */
void hui_put_character_at_window(Hui_Window window, char c, size_t y, size_t x) {
  int within_y_boundaries = y >= window.y && y < window.y + window.height;
  int within_x_boundaries = x >= window.x && x < window.x + window.width;
  if (within_x_boundaries && within_y_boundaries) {
    hui_move_cursor_to(window.y + y, window.x + x);
    write(1, &c, 1);
  }
}

void hui_draw_border_at_window(Hui_Window window) {
  for (size_t i = 0; i < window.width; i++) {
    hui_put_character_at_window(window, '*', 0, i);
  }

  for (size_t i = 1; i < window.height - 1; i++) {
    hui_put_character_at_window(window, '*', i, 0);
    hui_put_character_at_window(window, '*', i, window.width - 1);
  }

  for (size_t i = 0; i < window.width; i++) {
    hui_put_character_at_window(window, '*', window.height - 1, i);
  }
}

#endif // HOTUI_IMPLEMENTATION
