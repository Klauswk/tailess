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
  assert(line.count < 4096 && "Something went wrong here");
  assert(line.line && "Line can't be empty");
  line_reserve(lines, lines->count + 1);
  lines->lines[lines->count++] = line;
}

#define MAX_BUFFER_SIZE 4096

typedef struct {
  size_t y;
  size_t x;
} Hui_List_Offset;

typedef struct {
  size_t width;
  size_t height;
  size_t x;
  size_t y;
  Lines lines;
  Hui_List_Offset offset;
  Line needle;
  uint8_t following;
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
    uint64_t offset_y = i + list_window.offset.y;
    uint64_t offset_x = list_window.offset.x;

    if (offset_y > list_window.lines.count) break;

    Line line = list_window.lines.lines[offset_y];
    
    Sv sv_line;
    if (offset_x > line.count) {
      sv_line = sv_from_cstr("", 0);
    } else {
      uint64_t real_offset = line.count - offset_x > list_window.width ? list_window.width : line.count - offset_x;
      sv_line = sv_from_cstr(line.line + offset_x, real_offset);
    }

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

    if (sv_line.cstr) hui_put_text_at_window(win, sv_line.cstr, sv_line.size, i + y, x + acc);
  }
}

void hui_free_list_window(Hui_List_Window list_window) {
  for (size_t i = 0; i < list_window.lines.count; i++) {
    free(list_window.lines.lines[i].line);
  }
  free(list_window.lines.lines);
}

void hui_go_up_list_window(Hui_List_Window* list_window) {
  if (list_window->offset.y > 0) list_window->offset.y--;
}

void hui_page_up_list_window(Hui_List_Window* list_window) {
  for (size_t i = 0; i < list_window->height; i++) {
    hui_go_up_list_window(list_window);
  }
}

void hui_go_down_list_window(Hui_List_Window* list_window) {
  size_t n = list_window->lines.count;
  size_t cursor = list_window->offset.y;
  size_t height = list_window->height;

  if (n < height) return;

  if (n > height && cursor > n - height) {
    return ;
  }

  if (n > 0 && cursor < n -1) list_window->offset.y++;
}

void hui_page_down_list_window(Hui_List_Window* list_window) {
  for (size_t i = 0; i < list_window->height; i++) {
    hui_go_down_list_window(list_window);
  }
}

void hui_end_list_window(Hui_List_Window* list_window) {
  if (list_window->lines.count > list_window->height) {
    list_window->offset.y = list_window->lines.count - list_window->height;
  } else {
    list_window->offset.y = 0;
  }
}

void hui_go_left_list_window(Hui_List_Window* list_window) {
  if (list_window->offset.x > 0) list_window->offset.x--;
}

void hui_go_right_list_window(Hui_List_Window* list_window) {
  list_window->offset.x++;
}

void hui_push_line_list_window(Hui_List_Window* list_window, Line line) {
  push_line(&list_window->lines, line);
  
  size_t n = list_window->lines.count;

  if (n > list_window->height && list_window->following) hui_end_list_window(list_window);
;

 
}

void hui_home_list_window(Hui_List_Window* list_window) {
  list_window->offset.y = 0;
}

int hui_go_to_next_occurrence(Hui_List_Window* list_window) {
  if (!list_window->needle.line && list_window->needle.count == 0) return 0;

  for (size_t i = list_window->offset.y + 1; i < list_window->lines.count; i++) {
    if (strstr(list_window->lines.lines[i].line, list_window->needle.line)) {
     list_window->offset.y = i;
     return 1;
    }
  }

  return 0;
}

int hui_go_to_previous_occurrence(Hui_List_Window* list_window) {
  if (!list_window->needle.line && list_window->needle.count == 0) return 0;

  if (list_window->offset.y == 0) {
    return 0;
  }

  size_t i = list_window->offset.y - 1;

  while (1) {
    if (strstr(list_window->lines.lines[i].line, list_window->needle.line)) {
     list_window->offset.y = i;
     return 1;
    }

    // We can't allow it to overflow, but we must include 0
    if (i == 0) return 0;

    i--;
  }

  return 0;
}

typedef struct {
  struct pollfd fd[2];
  Hui_Window window;
  Hui_List_Window list_window;
  Hui_Input input_window;
  Hui_Window message_window;
  uint8_t numberFds;
} Tailess_Context;

uint8_t handle_read_data(Tailess_Context* context)
{
  static char buffer[MAX_BUFFER_SIZE];
  static char buffer2[MAX_BUFFER_SIZE];
  static size_t b2_size = 0;
  uint8_t updated = 0;

  if (context->numberFds > 1) {
    if (context->fd[1].revents & POLLIN) {
      ssize_t bytes = read(context->fd[1].fd, buffer, MAX_BUFFER_SIZE);
      if (bytes > 0) {
        for (int i = 0; i < bytes; i++) {
          if (buffer[i] == '\n') {
            Line line = {0};
            line.count = b2_size;
            line.line = malloc(sizeof(char) * line.count);
            strncpy(line.line, buffer2, b2_size);
            b2_size = 0;
            hui_push_line_list_window(&context->list_window, line);
            updated = 1;
          } else if (buffer[i] == '\t' || buffer[i] == '\r') {
            buffer2[b2_size++] = ' ';
          } else {
            if (b2_size >= MAX_BUFFER_SIZE - 1) {
              Line line = {0};
              line.count = b2_size;
              line.line = malloc(sizeof(char) * line.count);
              strncpy(line.line, buffer2, b2_size);
              b2_size = 0;
              hui_push_line_list_window(&context->list_window, line);
              updated = 1;
            }
            buffer2[b2_size++] = buffer[i];
          }
        }
      } else if (bytes == 0) {
        //Something is weird, we got a POLLIN event but there was nothing to read??
        context->numberFds--;
      }
    } else if (context->fd[1].revents & POLLERR || context->fd[1].revents & POLLNVAL|| context->fd[1].revents & POLLHUP) {
      context->numberFds--;
    }
  }

  return updated;
}

uint8_t handle_hui_events(Tailess_Context* context) {
  Hui_Event evt =  hui_poll_event();

  if (evt == RESIZE) {
    hui_set_window_size(&context->window);
    context->list_window.height = context->window.height - 2;
    context->list_window.width = context->window.width;
    context->list_window.x = 0;
    context->list_window.y = 0;

    context->input_window.width = context->window.width;
    context->input_window.height = 1;
    context->input_window.y = context->window.height - 1;
    context->input_window.x = 0;
    return 1;
  }

  return 0;
}

uint8_t handle_input(Tailess_Context* context) {
  uint8_t updated = 0;
  struct pollfd* fd = context->fd;
  char ch;
  if (fd[0].revents & POLLIN) {
    read(fd[0].fd, &ch, 1);

    if (ch == 27) { // ESC
      context->input_window.focus = 0;
      context->input_window.cursor = 0;
      updated = 1;
    } else if (ch == 23) { // CTRL + W
      if (context->input_window.focus && context->input_window.cursor > 0) {
        for (int i = context->input_window.cursor; i > 0; i--) {
          if (context->input_window.buffer[i] != ' ') {
            hui_input_pop_char(&context->input_window);
            continue;
          }
          break;
        }
      }
      updated = 1;
    } else if (ch == '\n') { //ENTER
      context->input_window.focus = 0;

      if (context->list_window.needle.line != 0) {
        free(context->list_window.needle.line);
        context->list_window.needle.line = 0;
        context->list_window.needle.count = 0;
      }

      if (context->input_window.cursor > 3) {
        context->list_window.needle.line = malloc(sizeof(char) * (context->input_window.cursor + 1));
        strncpy(context->list_window.needle.line, context->input_window.buffer, context->input_window.cursor);
        context->list_window.needle.line[context->input_window.cursor] = '\0';
        context->list_window.needle.count = context->input_window.cursor;
      }

      hui_go_to_next_occurrence(&context->list_window);
      context->input_window.cursor = 0;
      updated = 1;
    } else if (ch == 127) { //BACKSPACE
      if (!hui_input_pop_char(&context->input_window)) {
        context->input_window.focus = 0;
      }
      updated = 1;
    } else if (context->input_window.focus && hui_input_push_char(&context->input_window, ch)) {
      updated = 1;
    } else if (ch == 'q') {
      return 2;
    } else if (ch == 'j') {
      updated = 1;
      context->list_window.following = 0;
      hui_go_down_list_window(&context->list_window);
    } else if (ch == 'k') {
      updated = 1;
      context->list_window.following = 0;
      hui_go_up_list_window(&context->list_window);
    } else if (ch == 'h') {
      updated = 1;
      context->list_window.following = 0;
      hui_go_left_list_window(&context->list_window);
    } else if (ch == 'l') {
      updated = 1;
      context->list_window.following = 0;
      hui_go_right_list_window(&context->list_window);
    } else if (ch == 'N') {
      hui_go_to_previous_occurrence(&context->list_window);
      context->list_window.following = 0;
      updated = 1;
    } else if (ch == 'n') {
      hui_go_to_next_occurrence(&context->list_window);
      context->list_window.following = 0;
      updated = 1;
    } else if (ch == 2) { // CTRL + B
      updated = 1;
      context->list_window.following = 0;
      hui_page_up_list_window(&context->list_window);
    } else if (ch == 6) { // CTRL + F
      updated = 1;
      context->list_window.following = 0;
      hui_page_down_list_window(&context->list_window);
    } else if (ch == 'G') {
      updated = 1;
      context->list_window.following = 0;
      hui_end_list_window(&context->list_window);
    } else if (ch == 'g') {
      updated = 1;
      context->list_window.following = 0;
      hui_home_list_window(&context->list_window);
    } else if (ch == '/') {
      context->input_window.focus = 1;
      updated = 1;
    } else if (ch == 'f') {
      context->list_window.following = 1;
      updated = 1;
    }
  }

  return updated;
}

int main(int argc, char** args) {
  Tailess_Context context = {0};
  context.fd[0].fd = STDIN_FILENO;
  context.fd[0].events = POLLIN;

  context.fd[1].fd = STDIN_FILENO;
  context.fd[1].events = POLLIN;
  context.numberFds = 2;

  if (!isatty(fileno(stdin))) {
    int input = open("/dev/tty", O_RDONLY | O_CLOEXEC);

    if (!input) {
      fprintf(stderr, "Error opening tty input: %s \n", strerror(errno));
      return 1;
    }
    context.fd[0].fd = input;
  } else if (argc > 1) {
    FILE* fileinput = fopen(args[1], "r");

    if (!fileinput) {
      fprintf(stderr, "Error opening %s: %s \n", args[1], strerror(errno));
      return 1;
    }

    context.fd[1].fd = fileno(fileinput);
  } else {
    fprintf(stderr, "You must redirect some info to the application\n");
    return 1;
  }

  context.window = hui_init();

  int updated = 1;

  context.list_window = hui_create_list_window(context.window.width, context.window.height - 2, 0, 0);


  context.input_window = hui_create_input_window(context.window.width, 1, context.window.height - 1, 0);

  context.message_window = hui_create_window(context.window.width, 1, context.window.height - 2, 0);

  //hui_use_retain_mode();

  while(1) {

    if (updated) {
      start_drawing();
      hui_draw_list_window(context.list_window);
      hui_draw_input_window(context.input_window);
      if (context.list_window.following) hui_put_text_at_window(context.message_window, "Following..", 11, 0, 0);
      end_drawing();
      updated = 0;
    }

    int retval = poll(context.fd, context.numberFds, 1000);

    if (retval == -1) {
      if (errno == EINTR) continue;
      return 1;
    } else if (retval) {
      updated = handle_input(&context);

      if (updated == 2) break;

      updated += handle_hui_events(&context);

      updated += handle_read_data(&context); 

    }
    if (updated) {
      hui_clear_window();
    }
  }
  hui_free_list_window(context.list_window);
  kill(getpid(), SIGINT);
  return 0;
}
