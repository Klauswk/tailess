#ifndef HOTUI_H_
#define HOTUI_H_
#include <sys/ioctl.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <signal.h>
#include <assert.h>

// ----------------------------------------------------
// Hui_Window
typedef enum {
  NONE,
  RESIZE,
} Hui_Event;

typedef struct {
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
Hui_Window hui_create_window(uint64_t width, uint64_t height, uint64_t y, uint64_t x);
void hui_move_cursor_to(uint64_t y, uint64_t x);
void hui_put_text_at(char* c, size_t size,uint64_t y, uint64_t x);
void hui_put_character_at(char c, uint64_t y, uint64_t x);
void hui_put_text_at_window(Hui_Window window, char* c, size_t size, size_t y, size_t x);
void hui_put_character_at_window(Hui_Window window, char c, size_t y, size_t x);
//Retain mode
void start_drawing();
void end_drawing();

// ----------------------------------------------------
// Hui_Input

typedef struct {
  size_t width;
  size_t height;
  size_t x;
  size_t y;
  char* buffer;
  size_t capacity;
  size_t cursor;
  size_t input_on;
} Hui_Input;

Hui_Input hui_create_input_window(uint64_t width, uint64_t height, uint64_t y, uint64_t x);
int64_t hui_reserve(char** buffer, size_t* capacity, size_t expected_capacity);
int64_t hui_append_to(char** buffer, size_t* capacity, size_t* size, const char* append, size_t append_size);
void hui_draw_input_window(Hui_Input input);
int64_t hui_input_pop_char(Hui_Input* input);
int64_t hui_input_accept(Hui_Input* input, char c);
int64_t hui_input_push_char(Hui_Input* input, char c);

#endif // HOTUI_H_

#ifdef HOTUI_IMPLEMENTATION

static int64_t output_fd;
static struct termios initial;
static uint16_t terminal_width;
static uint16_t terminal_height;

#define EVENT_QUEUE_SIZE 256
static Hui_Event hui_event_queue[EVENT_QUEUE_SIZE];
static size_t hui_event_cursor_write_index = 0;
static size_t hui_event_cursor_read_index = 0;

static struct {
  char* buffer;
  size_t capacity;
  size_t size;
  uint8_t buffering;
} screen_buffer = {0};

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

Hui_Window hui_create_window(uint64_t width, uint64_t height, uint64_t y, uint64_t x) {
  return (Hui_Window) {
    .width = width,
    .height = height,
    .y = y,
    .x = x,
  };
}

void hui_move_cursor_to(uint64_t y, uint64_t x) {
  y++;
  x++;
  char buffer[50];
  sprintf(buffer, "\x1b[%"PRIu64";%"PRIu64"H", y, x);
  hui_print(buffer); 
}

/*
 * We assume 0 based index 
 */
void hui_put_text_at(char* c, size_t size,uint64_t y, uint64_t x) {
  if (screen_buffer.buffering) {
    y++;
    x++;
    char buffer[50];
    size_t sprintf_result = sprintf(buffer, "\x1b[%"PRIu64";%"PRIu64"H", y, x);
    hui_append_to(&screen_buffer.buffer, &screen_buffer.capacity, &screen_buffer.size, buffer, sprintf_result);

    hui_reserve(&screen_buffer.buffer, &screen_buffer.capacity, screen_buffer.size + size);
    hui_append_to(&screen_buffer.buffer, &screen_buffer.capacity, &screen_buffer.size, c, size);
  } else {
    hui_move_cursor_to(y,x);
    write(output_fd, c, size);
  }
}

/*
 * We assume 0 based index 
 */
void hui_put_character_at(char c, uint64_t y, uint64_t x) {
  if (screen_buffer.buffering) {
    y++;
    x++;
    char buffer[50];
    size_t size = sprintf(buffer, "\x1b[%"PRIu64";%"PRIu64"H", y, x);
    hui_append_to(&screen_buffer.buffer, &screen_buffer.capacity, &screen_buffer.size, buffer, size);

    hui_reserve(&screen_buffer.buffer, &screen_buffer.capacity, screen_buffer.size + 1);
    screen_buffer.buffer[screen_buffer.size++] = c;
  } else {
    hui_move_cursor_to(y,x);
    write(output_fd, &c, 1);
  }
}

/*
 * We assume 0 based index 
 */
void hui_put_text_at_window(Hui_Window window, char* c, size_t size, size_t y, size_t x) {
  int within_y_boundaries = y < window.y + window.height;
  int within_x_boundaries = x < window.x + window.width;

  if (within_x_boundaries && within_y_boundaries) {
    hui_put_text_at(c, size, window.y + y, window.x + x);
  }
}

/*
 * We assume 0 based index 
 */
void hui_put_character_at_window(Hui_Window window, char c, size_t y, size_t x) {
  int within_y_boundaries = y >= window.y && y < window.y + window.height;
  int within_x_boundaries = x >= window.x && x < window.x + window.width;
  if (within_x_boundaries && within_y_boundaries) {
    hui_put_character_at(c, window.y + y, window.x + x);
  }
}

int64_t hui_reserve(char** buffer, size_t* capacity, size_t expected_capacity) 
{
  if (expected_capacity > *capacity) {
    if (*capacity == 0) {
       *capacity = 10;
    }
    while(expected_capacity >= *capacity) {
      *capacity *= 2;
    }

    void* result = realloc(*buffer, (*capacity * sizeof(char)));

    assert(result && "Couldn't reserve memory for buffer");
    if (result) {
      *buffer = result;
      return 1;
    }
  }
  return 1;
}

int64_t hui_append_to(char** buffer, size_t* capacity, size_t* size, const char* append, size_t append_size) 
{
  hui_reserve(buffer, capacity, *size + append_size);
  int offset = *size;
  for (size_t i = 0; i < append_size; i++) {
    (*buffer)[i + (offset)] = append[i];
    (*size)++;
  }
  return 0;
}



Hui_Input hui_create_input_window(uint64_t width, uint64_t height, uint64_t y, uint64_t x) {
  Hui_Input input = (Hui_Input) {
    .width = width,
    .height = height,
    .y = y,
    .x = x,
    .buffer = 0,
    .capacity = 0,
    .cursor = 0,
    .input_on = 0,
  };

  return input;
}

/*
 * Return > 0 if char consumed
 */
int64_t hui_input_push_char(Hui_Input* input, char c) {
  if (hui_reserve(&input->buffer, &input->capacity, input->cursor + 1)) {
    input->buffer[input->cursor++] = c;
    return 1;
  }
  return 0;
}

/**
 * Return > 0 if char consumed
 */
int64_t hui_input_accept(Hui_Input* input, char c) {
  if (input->input_on) {
     return hui_input_push_char(input, c);
  }
  return 0;
}

/*
 * Return > 0 if char pop 
 */
int64_t hui_input_pop_char(Hui_Input* input) {
  if (input->cursor > 0) {
    input->cursor--; 
    return 1;
  }
  return 0;
}

void hui_draw_input_window(Hui_Input input) {
  Hui_Window win = *((Hui_Window *) &input);
  if (input.input_on) {
    hui_put_text_at_window(win, "/", 1, 0, 0);
  };
  if (input.cursor > 0) {
    hui_put_text_at_window(win, input.buffer, input.cursor, 0, 1);
  }
}

void start_drawing() {
  screen_buffer.buffering = 1;
  screen_buffer.size = 0;
  size_t screen_size = terminal_width * terminal_height * sizeof(char);
  
  hui_reserve(&screen_buffer.buffer, &screen_buffer.capacity, screen_size);
}

void end_drawing() {
  screen_buffer.buffering = 0;
  write(output_fd, screen_buffer.buffer, screen_buffer.size);
  screen_buffer.size = 0;
}

#endif // HOTUI_IMPLEMENTATION
