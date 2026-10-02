# AI Passport Voice

Use a FoloToy AI Passport as a Wi-Fi microphone and voice-input shortcut for a Windows PC.
Hold the device's upper button to speak and release it to finish. Recognition is performed by the PC input method.

**0.2.0-rc.1 is a prerelease.** On 2026-10-02, the maintainer confirmed successful dictation and accurate recognition using the firmware and Windows program from the published GitHub package. This is one tested device/PC/WeType setup; error rates and compatibility across other computers have not been measured.

- [Download the complete package](https://github.com/manchunx7-bit/folo-ai-passport-voice/releases/tag/v0.2.0-rc.1): choose `ai-passport-voice-0.2.0-rc.1.zip`, not GitHub's automatic source archive.
- [Chinese setup guide](docs/QUICKSTART.zh-CN.md): Wi-Fi, VB-CABLE, WeType, hotkey configuration and first dictation.
- Set **Windows Settings → System → Sound → Input** to **CABLE Output (VB-Audio Virtual Cable)** after installing VB-CABLE and rebooting. Select the same microphone in WeType if it offers its own selector.
- The voice forwarder writes to **CABLE Input**. Keep the Windows playback/output device set to your normal speakers or headphones.
- Launch `windows/start.cmd` after extracting the complete package. Python is not required for the EXE.
- Target: Windows 10/11 x64 and the matching ESP32-C3 / 8 MB FoloToy AI Passport hardware.
- Firmware and Windows source are included here. See [development guide](docs/DEVELOPMENT.zh-CN.md).
- The LAN protocol is unauthenticated and unencrypted. Use a trusted LAN only.

See [中文说明](README.md), [validation](docs/VALIDATION.md), [LICENSE](LICENSE) and [third-party notices](THIRD_PARTY_NOTICES.md).
