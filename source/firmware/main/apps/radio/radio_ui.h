#pragma once

#include "radio_player.h"

#include <cstddef>
#include <cstdint>

struct WifiNetworkView {
    char ssid[33];
    int8_t rssi;
    bool secure;
};

#include "launcher/wifi_keyboard.h"

enum class RadioSettingsPage {
    Menu,
    Volume,
    City,
    SleepTimer,
    Display,
    WifiMenu,
    SavedWifi,
    WifiAction,
    WifiDeleteConfirm,
};

bool radio_ui_init();
void radio_ui_show_main();
void radio_ui_hide();
void radio_ui_show_config(const char *ssid, const char *url);
void radio_ui_show_wifi_list(const WifiNetworkView *networks, std::size_t count,
                             std::size_t selected, bool scanning,
                             const char *setup_ap_ssid, bool can_cancel = false);
void radio_ui_show_wifi_keyboard(const char *ssid, std::size_t password_length,
                                 WifiKeyboardPage page, std::size_t selected,
                                 const char *status = nullptr);
void radio_ui_show_wifi_connecting(const char *ssid, const char *detail);
const char *radio_ui_wifi_key(WifiKeyboardPage page, std::size_t index);
std::size_t radio_ui_wifi_key_count(WifiKeyboardPage page);
void radio_ui_set_network(bool connected, const char *detail);
void radio_ui_set_location(const char *location);
void radio_ui_set_station(std::size_t index, std::size_t count,
                          const RadioStation &station);
void radio_ui_set_playback(RadioPlaybackState state, const char *detail = nullptr);
void radio_ui_set_audio_levels(const uint8_t *levels, std::size_t count);
void radio_ui_show_settings(RadioSettingsPage page, std::size_t selected,
                            uint8_t volume);
void radio_ui_show_saved_wifi(const char *const *ssids, std::size_t count,
                              std::size_t selected);
void radio_ui_show_wifi_action(const char *ssid, std::size_t selected);
void radio_ui_show_wifi_delete_confirm(const char *ssid, std::size_t selected);
void radio_ui_show_easter_egg(const char *message);
bool radio_ui_show_easter_egg_frame(const uint8_t *jpeg, std::size_t size,
                                    uint16_t width, uint16_t height);
void radio_ui_finish_easter_egg();
void radio_ui_set_volume(uint8_t volume);
