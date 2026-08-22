#include "race_kick_broker.h"

#include <inttypes.h>
#include <stdio.h>

uint64_t Race_KickBrokerNextConnectionId(uint64_t *counter) {
  if (!counter || *counter == UINT64_MAX) {
    return 0u;
  }
  (*counter)++;
  return *counter;
}

bool Race_KickBrokerCapture(const uint16_t slot, const size_t max_slots,
                            const bool in_use, uint64_t *connection_id,
                            uint64_t *counter,
                            race_kick_ticket_t *ticket) {
  if (!connection_id || !counter || !ticket || !in_use ||
      slot >= max_slots || max_slots > UINT16_MAX) {
    return false;
  }
  if (!*connection_id) {
    *connection_id = Race_KickBrokerNextConnectionId(counter);
  }
  if (!*connection_id) {
    return false;
  }
  *ticket = (race_kick_ticket_t) {
    .slot = slot,
    .connection_id = *connection_id
  };
  return true;
}

race_kick_broker_result_t Race_KickBrokerValidate(
    const race_kick_ticket_t ticket, const size_t max_slots,
    const bool in_use, const uint64_t current_connection_id) {
  if (ticket.slot >= max_slots || max_slots > UINT16_MAX ||
      !ticket.connection_id) {
    return RACE_KICK_BROKER_INVALID;
  }
  if (!in_use || !current_connection_id ||
      current_connection_id != ticket.connection_id) {
    return RACE_KICK_BROKER_STALE;
  }
  return RACE_KICK_BROKER_EXECUTE;
}

bool Race_KickBrokerFormatCommit(char *buffer, const size_t size,
                                 const char *command,
                                 const race_kick_ticket_t ticket) {
  if (!buffer || !size || !command || !*command ||
      !ticket.connection_id) {
    return false;
  }
  const int32_t written = snprintf(buffer, size, "%s %u %" PRIu64 "\n",
                                   command, ticket.slot,
                                   ticket.connection_id);
  return written > 0 && (size_t) written < size;
}

bool Race_KickBrokerFormatStockKick(char *buffer, const size_t size,
                                    const race_kick_ticket_t ticket) {
  if (!buffer || !size || !ticket.connection_id) {
    return false;
  }
  const int32_t written = snprintf(buffer, size, "kick %u\n", ticket.slot);
  return written > 0 && (size_t) written < size;
}
