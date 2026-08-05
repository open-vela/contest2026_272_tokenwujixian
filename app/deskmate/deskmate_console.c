#include "deskmate_console.h"

#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define DESKMATE_CONSOLE_LINE_MAX 16

static char g_console_line[DESKMATE_CONSOLE_LINE_MAX];
static size_t g_console_length;

void deskmate_console_prompt(void)
{
  printf("DeskMate> ");
  fflush(stdout);
}

bool deskmate_console_handle_line(struct deskmate_controller_s *controller,
                                  char *line)
{
  enum deskmate_decision_e decision;
  size_t length;

  if (controller == NULL || line == NULL) {
    return false;
  }

  length = strlen(line);
  while (length > 0 && (line[length - 1] == '\n' ||
                        line[length - 1] == '\r')) {
    line[--length] = '\0';
  }

  if (length == 0) {
    return true;
  }

  if (strcmp(line, "once") == 0) {
    decision = DESKMATE_DECISION_ONCE;
  } else if (strcmp(line, "deny") == 0) {
    decision = DESKMATE_DECISION_DENY;
  } else {
    printf("DeskMate: commands are once or deny\n");
    return true;
  }

  if (!deskmate_controller_decide(controller, decision)) {
    printf("DeskMate: no pending approval\n");
  }

  return true;
}

void deskmate_console_poll(struct deskmate_controller_s *controller)
{
  struct pollfd pollfd = { .fd = STDIN_FILENO, .events = POLLIN };
  char input[DESKMATE_CONSOLE_LINE_MAX];
  ssize_t count;
  size_t index;

  if (poll(&pollfd, 1, 0) <= 0 || (pollfd.revents & POLLIN) == 0) {
    return;
  }

  count = read(STDIN_FILENO, input, sizeof(input) - 1);
  if (count <= 0) {
    return;
  }

  for (index = 0; index < (size_t)count; index++) {
    if (input[index] == '\n') {
      g_console_line[g_console_length] = '\0';
      deskmate_console_handle_line(controller, g_console_line);
      g_console_length = 0;
      deskmate_console_prompt();
    } else if (input[index] != '\r' &&
               g_console_length + 1 < sizeof(g_console_line)) {
      g_console_line[g_console_length++] = input[index];
    } else if (input[index] != '\r') {
      g_console_length = 0;
      printf("DeskMate: command is too long\n");
      deskmate_console_prompt();
    }
  }
}
