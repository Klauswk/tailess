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
 Hui_Window window;
 Lines lines;
 size_t cursor;
} Hui_List_Window;

Hui_List_Window hui_create_list_window(int width, int height, int y, int x) {
  Hui_Window win = hui_create_window(width, height, y, x);

  return (Hui_List_Window) {
    .window = win,
  };
}

void hui_draw_list_window(Hui_List_Window list_window) {
  size_t width = list_window.window.width;
  size_t height = list_window.window.height;
  size_t x = list_window.window.x;
  size_t y = list_window.window.y;
  
  for (size_t i = 0; i < height; i++) {
    int offset = i + list_window.cursor;
    Line line = list_window.lines.lines[offset];
    hui_put_text_at_window(list_window.window, line.line, line.count, i, x);
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
  for (size_t i = 0; i < list_window->window.height; i++) {
    hui_go_up_list_window(list_window);
  }
}

void hui_go_down_list_window(Hui_List_Window* list_window) {
  size_t n = list_window->lines.count;
  if (n > 0 && list_window->cursor < n -1) list_window->cursor++;
}

void hui_page_down_list_window(Hui_List_Window* list_window) {
  for (size_t i = 0; i < list_window->window.height; i++) {
    hui_go_down_list_window(list_window);
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

  Hui_List_Window list_window = hui_create_list_window(window.width, window.height, 0, 0);

  while(1) {
    int retval = poll(fd, numberFds, 1000);
    hui_clear_window();
    if (retval == -1) {
      if (errno == EINTR) continue;

      return 1;
    } else if (retval) {
      if (fd[0].revents & POLLIN) {
        read(input, &ch, 1);
        if (ch == 'q') {
          break;
        } else if (ch == 'j') {
          hui_go_down_list_window(&list_window);
        } else if (ch == 'k') {
          hui_go_up_list_window(&list_window);
        } else if (ch == 2) {
          hui_page_up_list_window(&list_window);
        } else if (ch == 6) {
          hui_page_down_list_window(&list_window);
        }
      } 
      
      Hui_Event evt =  hui_poll_event();

      if (evt == RESIZE) {
        hui_set_window_size(&window);
        hui_set_window_size(&list_window.window);
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
                push_line(&list_window.lines, line);
              } else {
                if (b2_size >= MAX_BUFFER_SIZE - 1) {
                  Line line = {0};
                  line.count = b2_size + 1;
                  line.line = malloc(sizeof(char) * line.count + 1);
                  strncpy(line.line, buffer2, b2_size);
                  line.line[b2_size] = '\0';
                  b2_size = 0;
                  push_line(&list_window.lines, line);
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
    hui_draw_list_window(list_window);
  }
  hui_free_list_window(list_window);
  kill(getpid(), SIGINT);
  return 0;
}
