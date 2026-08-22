/* Race-owned identity-safe adapter for the stock numeric kick command. */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint16_t slot;
  uint64_t connection_id;
} race_kick_ticket_t;

typedef enum {
  RACE_KICK_BROKER_EXECUTE,
  RACE_KICK_BROKER_STALE,
  RACE_KICK_BROKER_INVALID
} race_kick_broker_result_t;

uint64_t Race_KickBrokerNextConnectionId(uint64_t *counter);

bool Race_KickBrokerCapture(uint16_t slot, size_t max_slots, bool in_use,
                            uint64_t *connection_id, uint64_t *counter,
                            race_kick_ticket_t *ticket);

race_kick_broker_result_t Race_KickBrokerValidate(
  race_kick_ticket_t ticket, size_t max_slots, bool in_use,
  uint64_t current_connection_id);

bool Race_KickBrokerFormatCommit(char *buffer, size_t size,
                                 const char *command,
                                 race_kick_ticket_t ticket);

bool Race_KickBrokerFormatStockKick(char *buffer, size_t size,
                                    race_kick_ticket_t ticket);
