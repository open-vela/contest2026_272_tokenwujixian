#ifndef DESKMATE_MQTT_H
#define DESKMATE_MQTT_H

#include "deskmate_controller.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DESKMATE_MQTT_HOST_MAX 63
#define DESKMATE_MQTT_PORT_MAX 5
#define DESKMATE_MQTT_USER_MAX 32
#define DESKMATE_MQTT_PASSWORD_MAX 64
#define DESKMATE_MQTT_DEVICE_ID_MAX 32

struct deskmate_mqtt_config_s {
  const char *host;
  const char *port;
  const char *username;
  const char *password;
  const char *device_id;
};

struct deskmate_mqtt_s;

typedef void (*deskmate_input_poll_t)(void *context);

int deskmate_mqtt_run(const struct deskmate_mqtt_config_s *config,
                      struct deskmate_controller_s *controller,
                      deskmate_input_poll_t poll_input,
                      void *input_context);

#endif
