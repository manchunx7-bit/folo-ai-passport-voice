#pragma once

#include <cstddef>
#include <cstdint>

// The timer is intentionally session-only. After a power-on or reset it is off,
// so an old choice can never surprise the owner by shutting down a new session.
bool radio_sleep_timer_init();
void radio_sleep_timer_set_minutes(uint16_t minutes);
std::size_t radio_sleep_timer_selection();
uint32_t radio_sleep_timer_remaining_seconds();
