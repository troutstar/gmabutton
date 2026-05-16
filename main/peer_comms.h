#pragma once

#include "app_state.h"

/* Spawn the comms task that consumes g_ctx.comms_q. */
void peer_comms_start(void);

/* Convenience — enqueue an outgoing alert/dismiss. */
void peer_send(comms_kind_t k);
