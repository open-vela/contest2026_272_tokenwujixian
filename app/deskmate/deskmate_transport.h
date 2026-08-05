#ifndef DESKMATE_TRANSPORT_H
#define DESKMATE_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>

struct deskmate_transport_ops_s {
  bool (*send_json)(void *context, const char *json, size_t length);
};

struct deskmate_transport_s {
  const struct deskmate_transport_ops_s *ops;
  void *context;
};

static inline bool deskmate_transport_send(struct deskmate_transport_s *transport,
                                           const char *json, size_t length)
{
  return transport != NULL && transport->ops != NULL &&
         transport->ops->send_json != NULL &&
         transport->ops->send_json(transport->context, json, length);
}

#endif
