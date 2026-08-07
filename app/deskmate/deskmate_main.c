#include "deskmate_console.h"
#include "deskmate_controller.h"
#include "deskmate_mqtt.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int make_nonce(char *out, size_t out_size)
{
  unsigned char bytes[18];
  size_t index;
  int fd;

  if (out_size < sizeof(bytes) * 2 + 1) {
    return -1;
  }

  fd = open("/dev/urandom", O_RDONLY);
  if (fd < 0 || read(fd, bytes, sizeof(bytes)) != sizeof(bytes)) {
    if (fd >= 0) {
      close(fd);
    }
    return -1;
  }
  close(fd);

  for (index = 0; index < sizeof(bytes); index++) {
    snprintf(&out[index * 2], 3, "%02x", bytes[index]);
  }
  return 0;
}

static void usage(const char *program)
{
  printf("Usage: %s -h HOST -p PORT -u USER -w PASSWORD -d DEVICE_ID\n", program);
}

static void poll_console(void *context)
{
  deskmate_console_poll(context);
}

int main(int argc, char *argv[])
{
  static struct deskmate_controller_s controller;
  struct deskmate_mqtt_config_s config = { .port = "1883" };
  char nonce[DESKMATE_NONCE_MAX + 1];
  int option;

  while ((option = getopt(argc, argv, "h:p:u:w:d:")) != -1) {
    switch (option) {
      case 'h': config.host = optarg; break;
      case 'p': config.port = optarg; break;
      case 'u': config.username = optarg; break;
      case 'w': config.password = optarg; break;
      case 'd': config.device_id = optarg; break;
      default: usage(argv[0]); return 2;
    }
  }

  if (config.host == NULL || config.username == NULL ||
      config.password == NULL || config.device_id == NULL || make_nonce(nonce, sizeof(nonce)) < 0) {
    usage(argv[0]);
    return 2;
  }

  deskmate_controller_init(&controller, nonce);
  printf("DeskMate client started: %s\n",
         deskmate_state_name(controller.client.state));
  printf("DeskMate: type help for commands\n");
  deskmate_console_prompt();
  return deskmate_mqtt_run(&config, &controller, poll_console, &controller) == 0 ? 0 : 1;
}
