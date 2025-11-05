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

typedef struct  {
  Line* lines;
  size_t count;
  size_t capacity;
} Lines;

Lines lines = {0};

void line_reserve(size_t expected_capacity) {
  if (expected_capacity > lines.capacity) {
    if (lines.capacity == 0) {
       lines.capacity = 1000;
    }
    while(expected_capacity > lines.capacity) {
      lines.capacity *= 2;
    }
    lines.lines = realloc(lines.lines, lines.capacity * sizeof(Line));

    assert(lines.lines != NULL && "Out of memory");
  }
}

void push_line(Line line) {
    line_reserve(line.count+1);
    lines.lines[lines.count++] = line;
}

#define MAX_BUFFER_SIZE 10

int main() {

  int input = STDIN_FILENO; 
  if (!isatty(fileno(stdin))) {
    int fd = open("/dev/tty", O_RDONLY | O_CLOEXEC);

    if (!fd) {
      printf("Erro opening tty\n");
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

  while(1) {
    int retval = poll(fd, numberFds, 1000);
    if (retval == -1) {
      if (errno == EINTR) continue;

      return 1;
    } else if (retval) {
      if (fd[0].revents & POLLIN) {
        read(input, &ch, 1);
        if (ch == 'q') {
          return 1;
        } 
      } 
      
      if (fd[1].revents & POLLIN) {
        ssize_t bytes = read(STDIN_FILENO, buffer, MAX_BUFFER_SIZE);
        if (bytes > 0) {
          for (int i = 0; i < bytes; i++) {
            if (buffer[i] == '\n' || b2_size >= MAX_BUFFER_SIZE - 1) {
              Line line = {0};
              line.count = b2_size + 1;
              line.line = malloc(sizeof(char) * line.count + 1);
              strncpy(line.line, buffer2, b2_size);
              line.line[b2_size] = '\0';
              b2_size = 0;
              //printf("%.*s\n", line.count, line.line);
              push_line(line);
            } else {
              buffer2[b2_size++] = buffer[i];
            }
          }
        }
      } else if (fd[1].revents & POLLHUP) {
        printf("Stream closed.\n");
        for (int i = 0; i < lines.count; i++) {
          printf("%.*s\n", lines.lines[i].count, lines.lines[i].line);
          free(lines.lines[i].line);
        }
        numberFds--;
        return 0;
      }

    }
  }
  return 0;
}
