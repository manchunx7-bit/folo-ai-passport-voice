#pragma once

#include <cstddef>
#include <cstdint>

// Screen preferences are persisted in NVS. A key press which wakes a blanked
// screen is consumed by the caller, preventing accidental station changes.
bool radio_display_settings_init();
bool radio_display_note_activity();

uint8_t radio_display_brightness();
std::size_t radio_display_brightness_selection();
void radio_display_set_brightness_selection(std::size_t selection);

uint16_t radio_display_timeout_seconds();
std::size_t radio_display_timeout_selection();
void radio_display_set_timeout_selection(std::size_t selection);

