#pragma once

#include "radio_player.h"

#include <cstddef>

struct RadioLocation {
    char country_code[4];
    char region[48];
    char city[48];
};

// Locates the radio from its public network address and loads playable MP3
// stations for the detected city. If the directory has no city result, it
// automatically falls back to the surrounding region/province.
std::size_t radio_catalog_discover(RadioStation *stations, std::size_t capacity,
                                   RadioLocation *location);

// Loads the most popular playable stations for a user-selected Chinese city.
// This skips IP geolocation and is used by the on-device city selector.
std::size_t radio_catalog_discover_city(const char *city, RadioStation *stations,
                                        std::size_t capacity,
                                        RadioLocation *location);
