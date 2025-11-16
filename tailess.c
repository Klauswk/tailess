#include <stdio.h>
#include <sys/poll.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>

#define HOTUI_IMPLEMENTATION
#include "hotui.h"

typedef struct {
  char* line;
  size_t count;
} Line;

typedef struct {
  Line* lines;
  size_t count;
  size_t capacity;
} Lines;

void line_reserve(Lines* lines, size_t expected_capacity) {
  if (expected_capacity > lines->capacity) {
    if (lines->capacity == 0) {
       lines->capacity = 10;
    }
    while(expected_capacity >= lines->capacity) {
      lines->capacity *= 2;
    }
    lines->lines = realloc(lines->lines, (lines->capacity * sizeof(Line)));

    assert(lines->lines != NULL && "Out of memory");
  }
}

void push_line(Lines* lines, Line line) {
  line_reserve(lines, lines->count + 1);
  lines->lines[lines->count++] = line;
}

#define MAX_BUFFER_SIZE 4096

typedef struct {
  size_t width;
  size_t height;
  size_t x;
  size_t y;
  Lines lines;
  size_t cursor;
  Line needle;
} Hui_List_Window;

Hui_List_Window hui_create_list_window(int width, int height, int y, int x) {
  Hui_Window win = hui_create_window(width, height, y, x);
  Line line = {
    .line = 0,
    .count = 0,
  };

  return (Hui_List_Window) {
    .width = win.width,
    .height = win.height,
    .x = win.x,
    .y = win.y,
    .needle = line, 
  };
}

typedef struct {
  char* cstr;
  size_t size;
} Sv;

Sv sv_from_cstr(char* cstr, size_t size) {
  return (Sv) {
    .cstr = cstr,
    .size = size,
  };
}

Sv sv_chop_by_size(Sv* sv, size_t size) {
  if (size > sv->size) {
    size = sv->size;
  }

  Sv result = sv_from_cstr(sv->cstr, size);

  sv->cstr += size;
  sv->size -= size;

  return result;
}

void hui_draw_list_window(Hui_List_Window list_window) {
  size_t height = list_window.height;
  size_t x = list_window.x;
  size_t y = list_window.y;
  Hui_Window win = {
    .width = list_window.width,
    .height = list_window.height,
    .x = list_window.x,
    .y = list_window.y,
  };

  size_t n = list_window.lines.count < height ? list_window.lines.count : height;

  for (size_t i = 0; i < n; i++) {
    int offset = i + list_window.cursor;
    Line line = list_window.lines.lines[offset];

    Sv sv_line = sv_from_cstr(line.line, line.count);

    size_t acc = 0; 
    
    if (line.count < 1) continue;

    if (list_window.needle.line) {
      char* substring = strstr(sv_line.cstr, list_window.needle.line);
      while (substring) {
        size_t substring_size = substring - sv_line.cstr; 
        Sv sv1 = sv_chop_by_size(&sv_line, substring_size);
        hui_put_text_at_window(win, sv1.cstr, sv1.size, i + y, x + acc);

        acc = acc + substring_size;

        Sv sv_needle = sv_chop_by_size(&sv_line, strlen(list_window.needle.line));

        Sv blue_fg = sv_from_cstr("\x1b[34m", 5);
        hui_put_text_at_window(win, blue_fg.cstr, blue_fg.size, i + y, x + acc); 
        hui_put_text_at_window(win, sv_needle.cstr, sv_needle.size, i + y, x + acc);
        Sv reset_fg = sv_from_cstr("\x1b[m", 3);
        hui_put_text_at_window(win, reset_fg.cstr, reset_fg.size, i + y, x + acc); 
        acc = acc + sv_needle.size;

        substring = strstr(sv_line.cstr, list_window.needle.line);
      }

    }

    hui_put_text_at_window(win, sv_line.cstr, sv_line.size, i + y, x + acc);
  }
}

void hui_free_list_window(Hui_List_Window list_window) {
  for (size_t i = 0; i < list_window.lines.count; i++) {
    free(list_window.lines.lines[i].line);
  }
  free(list_window.lines.lines);
}

void hui_go_up_list_window(Hui_List_Window* list_window) {
  if (list_window->cursor > 0) list_window->cursor--;
}

void hui_page_up_list_window(Hui_List_Window* list_window) {
  for (size_t i = 0; i < list_window->height; i++) {
    hui_go_up_list_window(list_window);
  }
}

void hui_go_down_list_window(Hui_List_Window* list_window) {
  size_t n = list_window->lines.count;
  size_t cursor = list_window->cursor;
  size_t height = list_window->height;

  if (n > height && cursor > n - height) {
    return ;
  } 

  if (n > 0 && cursor < n -1) list_window->cursor++;
}

void hui_page_down_list_window(Hui_List_Window* list_window) {
  for (size_t i = 0; i < list_window->height; i++) {
    hui_go_down_list_window(list_window);
  }
}

void hui_push_line_list_window(Hui_List_Window* list_window, Line line) {
  push_line(&list_window->lines, line);
}

void hui_home_list_window(Hui_List_Window* list_window) {
  list_window->cursor = 0;
}

void hui_end_list_window(Hui_List_Window* list_window) {
  if (list_window->lines.count > list_window->height) {
    list_window->cursor = list_window->lines.count - list_window->height;
  } else {
    list_window->cursor = 0;
  }
}

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

int hui_input_reserve(Hui_Input* input, size_t expected_capacity) {
  size_t capacity = input->capacity;
  if (expected_capacity > capacity) {
    if (capacity == 0) {
       capacity = 10;
    }
    while(expected_capacity >= capacity) {
      capacity *= 2;
    }

    void* result = realloc(input->buffer, (capacity * sizeof(char)));

    if (result) {
      input->capacity = capacity;
      input->buffer = result;
      return 1;
    }

    return 0;
  }
  return 1;
}

Hui_Input hui_create_input_window(int width, int height, int y, int x) {
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
int hui_input_push_char(Hui_Input* input, char c) {
  if (hui_input_reserve(input, input->cursor + 1)) {
    input->buffer[input->cursor++] = c;
    return 1;
  }
  return 0;
}

/**
 * Return > 0 if char consumed
 */
int hui_input_accept(Hui_Input* input, char c) {
  if (input->input_on) {
     return hui_input_push_char(input, c);
  }
  return 0;
}

/*
 * Return > 0 if char pop 
 */
int hui_input_pop_char(Hui_Input* input) {
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

int main() {
  int input = STDIN_FILENO; 

  Hui_Window window = hui_init();

  if (!isatty(fileno(stdin))) {
    int fd = open("/dev/tty", O_RDONLY | O_CLOEXEC);

    if (!fd) {
      printf("Error opening tty\n");
      return 1;
    }
    input = fd;
  }

  struct pollfd fd[2];
  fd[0].fd = input;
  fd[0].events = POLLIN;

  fd[1].fd = STDIN_FILENO;
  fd[1].events = POLLIN;
  char ch;
  char buffer[MAX_BUFFER_SIZE];
  char buffer2[MAX_BUFFER_SIZE];
  size_t b2_size = 0;
  int numberFds = 2;
  int updated = 1;

  Hui_List_Window list_window = hui_create_list_window(window.width, window.height - 2, 0, 0);

  Hui_Input input_window = hui_create_input_window(window.width, 1, window.height - 1, 0);

  while(1) {
    
    if (updated) {
      hui_draw_list_window(list_window);
      hui_draw_input_window(input_window);
      updated = 0;
    }

    int retval = poll(fd, numberFds, 1000);
    
    if (retval == -1) {
      if (errno == EINTR) continue;
      return 1;
    } else if (retval) {
      if (fd[0].revents & POLLIN) {
        read(input, &ch, 1);
        if (input_window.input_on && ch != '\n' && ch != 27 && ch != 127 && hui_input_push_char(&input_window, ch)) {
          updated = 1;
        } else if (ch == 'q') {
          break;
        } else if (ch == 'j') {
          updated = 1;
          hui_go_down_list_window(&list_window);
        } else if (ch == 'k') {
          updated = 1;
          hui_go_up_list_window(&list_window);
        } else if (ch == 2) {
          updated = 1;
          hui_page_up_list_window(&list_window);
        } else if (ch == 6) {
          updated = 1;
          hui_page_down_list_window(&list_window);
        } else if (ch == 'G') {
          updated = 1;
          hui_end_list_window(&list_window);
        } else if (ch == 'g') {
          updated = 1;
          hui_home_list_window(&list_window);
        } else if (ch == '/') {
          input_window.input_on = 1;
          updated = 1;
        } else if (ch == 27) { 
          input_window.input_on = 0;
          input_window.cursor = 0;
          updated = 1;
        } else if (ch == '\n') {
          input_window.input_on = 0;

          if (list_window.needle.line != 0) {
            free(list_window.needle.line);
            list_window.needle.line = 0;
            list_window.needle.count = 0;
          }

          if (input_window.cursor > 3) {
            list_window.needle.line = malloc(sizeof(char) * (input_window.cursor + 1));
            strncpy(list_window.needle.line, input_window.buffer, input_window.cursor);
            list_window.needle.line[input_window.cursor] = '\0';
            list_window.needle.count = input_window.cursor;
          }

          input_window.cursor = 0;
          updated = 1;
        } else if (ch == 127) {
          if (!hui_input_pop_char(&input_window)) {
            input_window.input_on = 0;
          }
          updated = 1;
        }
      } 
      
      Hui_Event evt =  hui_poll_event();

      if (evt == RESIZE) {
        hui_set_window_size(&window);
        Hui_Window win = *((Hui_Window *) &list_window);
        hui_set_window_size(&win);
        updated = 1;
      }

      if (numberFds > 1) {
        if (fd[1].revents & POLLIN) {
          ssize_t bytes = read(STDIN_FILENO, buffer, MAX_BUFFER_SIZE);
          if (bytes > 0) {
            for (int i = 0; i < bytes; i++) {
              if (buffer[i] == '\n') {
                Line line = {0};
                line.count = b2_size + 1;
                line.line = malloc(sizeof(char) * line.count + 1);
                strncpy(line.line, buffer2, b2_size);
                line.line[b2_size] = '\0';
                b2_size = 0;
                hui_push_line_list_window(&list_window, line);
                updated = 1;
              } else {
                if (b2_size >= MAX_BUFFER_SIZE - 1) {
                  Line line = {0};
                  line.count = b2_size + 1;
                  line.line = malloc(sizeof(char) * line.count + 1);
                  strncpy(line.line, buffer2, b2_size);
                  line.line[b2_size] = '\0';
                  b2_size = 0;
                  hui_push_line_list_window(&list_window, line);
                  updated = 1;
                }
                buffer2[b2_size++] = buffer[i];
              }
            }
          }
        } else if (fd[1].revents & POLLHUP) {
          numberFds--;    
        }
      }
    }
    if (updated) {
      hui_clear_window();
    }
  }
  hui_free_list_window(list_window);
  kill(getpid(), SIGINT);
  return 0;
}
