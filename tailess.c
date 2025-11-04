#include <stdio.h>
#include <sys/poll.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#define HOTUI_IMPLEMENTATION
#include "hotui.h"

int main1() {

  Hui_Window window = hui_init();
  struct pollfd fd;
  fd.fd = STDIN_FILENO;
  fd.events = POLLIN;
  char ch;
  int row = window.height - 1;
  int col = 0;

  Hui_Window input_field = hui_create_window(window.width, 1, window.height - 1, 0);

  while(1) { 
    int retval = poll(&fd, 1, 1000);
    
    if (retval == -1) {
      return 1;
    } else if (retval) {
        read(1, &ch, 1);
        if (ch == 'q') {
          return 1;
        } else if (ch == '\n') {
          row++;
          col = 0;
        } else {
         hui_put_character_at_window(input_field, ch, row, col); 
         col++;
        }
    } else {
      //Handle no input available
    }
  }

  return 0;
}

int main2() {
  Hui_Window window = hui_init();

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
  char buffer[256];
  int row = 1;
  int numberFds = 2;
  hui_clear_window();

  while(1) {
    Hui_Event evt = hui_poll_event();
    if (evt == RESIZE) {
      hui_set_window_size(&window);
      hui_clear_window();
    }

    hui_draw_border_at_window(window);

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

        ssize_t bytes = read(STDIN_FILENO, buffer, 256);

        if (bytes > 0) {
          hui_put_text_at_window(window, buffer, bytes, row, 1); 
          row++;
          if (row >= window.height - 2) {
            row = 0;
            hui_clear_window();
          }
        }
      } else if (fd[1].revents & POLLHUP) {
        hui_put_text_at_window(window, "Stream close", 12, row, 1); 
        numberFds--;
      }

    }
  }
  return 0;
}

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
  char buffer[256];
  int numberFds = 2;

  while(1) {
    int retval = poll(fd, numberFds, 1000);
    printf("retval: %d\n", retval);
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
        ssize_t bytes = read(STDIN_FILENO, buffer, 256);
        if (buffer[bytes - 1] == '\n') buffer[bytes-1] = ' ';

        printf("[%.*s]\n", bytes, buffer);
      } else if (fd[1].revents & POLLHUP) {
        printf("Stream closed.\n");
        numberFds--;
      }

    }
  }
  return 0;
}
