//Header-only Terminal UI
//I hope this is actually more usable than ncurses
//Aims to be super simple

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
#include <errno.h>

// ----------------------------------------------------
// Hui_Window
// ----------------------------------------------------
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

//Basic operations for everyday life
Hui_Window hui_init();
Hui_Window hui_create_window(uint64_t width, uint64_t height, uint64_t y, uint64_t x);
void hui_put_text_at(char* c, size_t size, uint64_t y, uint64_t x);
void hui_put_character_at(char c, uint64_t y, uint64_t x);
void hui_put_text_at_window(Hui_Window window, char* c, size_t size, size_t y, size_t x);
void hui_put_character_at_window(Hui_Window window, char c, size_t y, size_t x);
void hui_move_cursor_to(uint64_t y, uint64_t x);
void hui_clear_window();

//Events that might happen, the most important is RESIZE
Hui_Event hui_poll_event();
void hui_push_event(Hui_Event event);
void hui_set_window_size(Hui_Window* window);

// ----------------------------------------------------
// Retain mode - Fuck I hate you microsoft
// ----------------------------------------------------
// Return 1 if it was already active;
int64_t use_retain_mode();
void start_drawing();
void end_drawing();

// ----------------------------------------------------
// Hui_Input
// ----------------------------------------------------
typedef struct {
  size_t width;
  size_t height;
  size_t x;
  size_t y;
  char* buffer;
  size_t capacity;
  size_t cursor;
  size_t focus;
} Hui_Input;

//A dynamic array + a window, 
Hui_Input hui_create_input_window(uint64_t width, uint64_t height, uint64_t y, uint64_t x);
void hui_draw_input_window(Hui_Input input);
int64_t hui_input_pop_char(Hui_Input* input);
int64_t hui_input_accept(Hui_Input* input, char c);
int64_t hui_input_push_char(Hui_Input* input, char c);

// Usefull dynamic array stuff

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

static void init_double_buffering();
static int64_t hui_reserve(char** buffer, size_t* capacity, size_t expected_capacity);
static int64_t hui_append_to(char** buffer, size_t* capacity, size_t* size, const char* append, size_t append_size);

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

typedef struct {
  char* buffer;
  uint8_t* foreground;
  uint8_t* background;
  size_t capacity;
  size_t size;
} Screen_Buffer;

static Screen_Buffer scr_buf[2] = {0};
int curr_buff = 0;
int64_t buffering = 0;


static void hui_resize(int i) {
  (void)i;
  struct winsize ws;
  ioctl(1, TIOCGWINSZ, &ws);
  terminal_width = ws.ws_col;
  terminal_height = ws.ws_row;
  if (scr_buf[curr_buff].buffer) {
    free(scr_buf[curr_buff].buffer);
    free(scr_buf[curr_buff].foreground);
    free(scr_buf[curr_buff].background);
  } else if (scr_buf[!curr_buff].buffer) {
    free(scr_buf[!curr_buff].buffer);
    free(scr_buf[!curr_buff].foreground);
    free(scr_buf[!curr_buff].background);
  }

  init_double_buffering();

  push_event(RESIZE);
}


void hui_print_sz(char* string, size_t size) {
 write(output_fd, string, size); 
}

void hui_print(char* string) {
  hui_print_sz(string, strlen(string));
}

void hui_clear_window() {
  if (!buffering) {
    char* clean_buffer = "\x1b[2J";
    hui_print(clean_buffer);
  }
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
  
  char* move_cursor_to_top = "\x1b[H";
  hui_print(move_cursor_to_top);

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
void hui_put_text_at(char* c, size_t size, uint64_t y, uint64_t x) {
  if (buffering) {
    Screen_Buffer* screen_buffer = &scr_buf[curr_buff];
    int ansi_escape = 0;
    size_t skip = 0;
    //This info needs to be statefull, since the consumer might send a control in one line
    //And the data in another, we would lose the "context" in this case
    static uint8_t foreground = 39;
    static uint8_t background = 49;
    char* start_chunk = 0;
    char* end_chunk = 0;
    
    for (size_t i = 0; i < size; i++) {
      if (c[i] == '\x1b' && c[i+1] == '[') {
        ansi_escape = 1;
        skip=+2;
        start_chunk = &c[i+2];
        i++;
        continue;
      } else if (ansi_escape && c[i] == 'm') {
        ansi_escape = 0;
        skip++;
        end_chunk = &c[i-1];
        
        long escape_value = strtol(start_chunk, &end_chunk, 0);
        
        //A problem happened when trying to process the escape code
        if (escape_value == 0 && errno) {
         assert(0 && "Error processing the escape code");
        } else if (escape_value == 0) {
          foreground = 39; 
          background = 49;
        } else if (escape_value) {
          if (escape_value > 29 && escape_value < 40) foreground = (uint8_t) escape_value;
          if (escape_value > 39 && escape_value < 50) background = (uint8_t) escape_value;
        } 
        
        continue;
      } 
      //Screen modes, we don't really care
      else if (ansi_escape && c[i] == 'h') {
        ansi_escape = 0;
        skip++;
        end_chunk = &c[i-1];
      } else if (ansi_escape) {
        skip++;
        continue;
      }
      size_t offset = y*terminal_width + x + i - skip;

      screen_buffer->buffer[offset]      = c[i];
      screen_buffer->foreground[offset]  = foreground;
      screen_buffer->background[offset]  = background;
    }
  } else {
    hui_move_cursor_to(y,x);
    write(output_fd, c, size);
  }
}

/*
 * We assume 0 based index 
 */
void hui_put_character_at(char c, uint64_t y, uint64_t x) {
  if (buffering) {
    Screen_Buffer* screen_buffer = &scr_buf[curr_buff];
    screen_buffer->buffer[y*terminal_width + x]  = c;
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

static int64_t hui_reserve(char** buffer, size_t* capacity, size_t expected_capacity) 
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

static int64_t hui_append_to(char** buffer, size_t* capacity, size_t* size, const char* append, size_t append_size) 
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
    .focus = 0,
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
  if (input->focus) {
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
  char buffer[256] = {0};
  sprintf(buffer, "\x1b[%"PRIu64";%"PRIu64"H", input.y, input.x);
  hui_print(buffer);
  hui_print("\x1b[?25h");
  if (input.focus) {
    hui_put_text_at_window(win, "/", 1, 0, 0);
  };
  if (input.cursor > 0) {
    hui_put_text_at_window(win, input.buffer, input.cursor, 0, 1);
  }
}

static void init_double_buffering()
{
  if (buffering) {
    size_t screen_size = terminal_width * terminal_height;
    scr_buf[curr_buff].size = screen_size;
    scr_buf[curr_buff].buffer = malloc(screen_size * sizeof(char));
    scr_buf[curr_buff].foreground = malloc(screen_size * sizeof(uint8_t));
    scr_buf[curr_buff].background = malloc(screen_size * sizeof(uint8_t));

    memset(scr_buf[curr_buff].buffer, ' ', screen_size * sizeof(char)); 
    memset(scr_buf[curr_buff].foreground, 39, screen_size * sizeof(uint8_t)); 
    memset(scr_buf[curr_buff].background, 49, screen_size * sizeof(uint8_t)); 

    scr_buf[!curr_buff].size = screen_size;
    scr_buf[!curr_buff].buffer = malloc(screen_size * sizeof(char));
    scr_buf[!curr_buff].foreground = malloc(screen_size * sizeof(uint8_t));
    scr_buf[!curr_buff].background = malloc(screen_size * sizeof(uint8_t));

    memset(scr_buf[!curr_buff].buffer, ' ', screen_size * sizeof(char)); 
    memset(scr_buf[!curr_buff].foreground, 39, screen_size * sizeof(uint8_t)); 
    memset(scr_buf[!curr_buff].background, 49, screen_size * sizeof(uint8_t)); 
  }
}

int64_t hui_use_retain_mode() {
  int64_t result = buffering;
  buffering = 1;
  init_double_buffering();

  return result;
}

void start_drawing() {
  Screen_Buffer* screen_buffer = &scr_buf[curr_buff];
  size_t screen_size = terminal_width * terminal_height;
  screen_buffer->size = screen_size;
  if (!screen_buffer->buffer) {
    screen_buffer->buffer = malloc(screen_size * sizeof(char));
    screen_buffer->foreground = malloc(screen_size * sizeof(uint8_t));
    screen_buffer->background = malloc(screen_size * sizeof(uint8_t));
  }

  memset(screen_buffer->buffer, ' ', screen_size * sizeof(char)); 
  memset(screen_buffer->foreground, 39, screen_size * sizeof(uint8_t)); 
  memset(screen_buffer->background, 49, screen_size * sizeof(uint8_t)); 
}

static struct {
  char* content;
  size_t capacity;
  size_t size;
} patches_buffer = {0};

void end_drawing() {
  //Latest display, the one about to be draw
  Screen_Buffer* screen_buffer = &scr_buf[curr_buff];

  //The last frame buffer, the one that was draw before
  Screen_Buffer back_buffer = scr_buf[!curr_buff];
  patches_buffer.size = 0;

  if (scr_buf[curr_buff].size != scr_buf[!curr_buff].size) {
    write(output_fd, screen_buffer->buffer, screen_buffer->size);
  } else {
    for (size_t row = 0; row < terminal_height; row++) {
      for (size_t col = 0; col < terminal_width; col++) {
        size_t offset = row * terminal_width + col;
        char c1 = screen_buffer->buffer[offset];
        char c2 = back_buffer.buffer[offset];

        uint8_t fg1 = screen_buffer->foreground[offset];
        uint8_t fg2 = back_buffer.foreground[offset];

        uint8_t bg1 = screen_buffer->background[offset];
        uint8_t bg2 = back_buffer.background[offset];

        if (c2 != c1 || fg1 != fg2 || bg1 != bg2) {
          char internal_buffer[50];
          int n = sprintf(internal_buffer, "\x1b[%"PRIu64";%"PRIu64"H\x1b[%d;%dm%c", row+1, col+1, fg1, bg1, c1);
          hui_append_to(&patches_buffer.content, &patches_buffer.capacity, &patches_buffer.size, internal_buffer, n);
        }
      }
    }

    if (patches_buffer.size > 0) write(output_fd, patches_buffer.content, patches_buffer.size);
  }

  curr_buff = !curr_buff;
}

#endif // HOTUI_IMPLEMENTATION
