#pragma once

// The optional resource is stored in a checked free-flash window. The app
// remains fully functional when the resource package is absent.
bool radio_easter_egg_start();
void radio_easter_egg_stop();
bool radio_easter_egg_is_playing();

