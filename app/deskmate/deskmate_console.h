#ifndef DESKMATE_CONSOLE_H
#define DESKMATE_CONSOLE_H

#include "deskmate_controller.h"

bool deskmate_console_handle_line(struct deskmate_controller_s *controller,
                                  char *line);
void deskmate_console_prompt(void);
void deskmate_console_poll(struct deskmate_controller_s *controller);

#endif
