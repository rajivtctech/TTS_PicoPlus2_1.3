# TTS_PicoPlus2 -- Serial Text-to-Speech Module Firmware

**Specification Reference:** TTS-PPC2-SPEC-001 Rev 1.2
**Target Board:** Pimoroni Pico Plus 2 (RP2350B, 16 MB Flash, 8 MB PSRAM)
**Arduino Core:** Earle Philhower arduino-pico v5.4.4
**Audio Library:** BackgroundAudio v1.4.4

## Arduino IDE Settings

| Setting              | Value                               |
|----------------------|-------------------------------------|
| Board                | Pimoroni PicoPlus2                  |
| CPU Architecture     | ARM Cortex-M33                      |
| CPU Speed            | 150 MHz                             |
| Optimize             | -O3 (recommended) or -Os            |
| USB Stack            | Pico SDK                            |
| Flash Size           | 16 MB (no FS)                       |

## Required Libraries

Install via Arduino Library Manager:

- **BackgroundAudio** by Earle F. Philhower, III (v1.4.4)

The I2S and PWMAudio libraries are built into the arduino-pico core.

## Important: One Language Per Build

BackgroundAudio v1.4.4 compiles the eSpeak-NG language dictionary into
flash as a global symbol (`__espeakng_dict`). Only ONE voice header can
be included per build -- including two or more causes a linker
redefinition error.

To change language, edit the single `#include` line near the top of the
sketch:

```cpp
// English (default):
#include <libespeak-ng/voice/en_gb_scotland.h>

// To switch to Spanish, comment above and uncomment:
// #include <libespeak-ng/voice/es.h>

// French:
// #include <libespeak-ng/voice/fr.h>
```

Then update the `currentVoice` pointer to match:

```cpp
static BackgroundAudioVoice *currentVoice = &voice_en_gb_scotland;
// or: static BackgroundAudioVoice *currentVoice = &voice_es;
// or: static BackgroundAudioVoice *currentVoice = &voice_fr;
```

List all available voices with:
```bash
ls ~/Documents/Arduino/sketches/libraries/BackgroundAudio/src/libespeak-ng/voice/*.h
```

## Wiring Summary

### I2S Bus (shared by amplifier and DAC)

| Pico Plus 2 | MAX98357A | PCM5100  | Function       |
|-------------|-----------|----------|----------------|
| GP16        | DIN       | DIN      | I2S data       |
| GP17        | BCLK      | BCK      | I2S bit clock  |
| GP18        | LRC       | WSEL     | I2S word sel   |
| GP7         | SD        | --       | Amp enable     |
| GP8         | --        | MU       | DAC mute ctrl  |

### Control Pins

| GPIO | Signal       | Direction | Purpose                          |
|------|-------------|-----------|----------------------------------|
| GP0  | UART TX     | Out       | Debug/response output            |
| GP1  | UART RX     | In        | Text input from host             |
| GP2  | MODE_SEL    | In        | Jumper: open=I2S, GND=PWM        |
| GP3  | READY       | Out       | HIGH when accepting text         |
| GP6  | AUDIO_ACTIVE| Out       | HIGH while producing audio       |
| GP7  | AMP_EN      | Out       | HIGH = amplifier on              |
| GP8  | DAC_MUTE    | Out       | LOW = DAC on, HIGH = muted       |
| GP25 | LED         | Out       | Heartbeat / status               |

## Serial Command Reference

Send commands as text lines starting with `\` via USB CDC or UART:

| Command          | Description                              |
|-----------------|------------------------------------------|
| `\SPEED=175`    | Speaking rate 80-450 WPM                 |
| `\PITCH=60`     | Pitch 0-100 (default 50)                |
| `\AMP=ON`       | Enable I2S amplifier                     |
| `\AMP=OFF`      | Disable I2S amplifier (shutdown)         |
| `\DAC=ON`       | Enable I2S DAC line output               |
| `\DAC=OFF`      | Mute I2S DAC                             |
| `\BAUD=9600`    | Change UART baud rate                    |
| `\STOP`         | Abort speech and flush queue             |
| `\STATUS`       | Report firmware version, mode, queue     |
| `\RESET`        | Software reboot                          |

Commands not available in BackgroundAudio v1.4.4:
- `\VOLUME` -- no setVolume() method
- `\VOICE`, `\LANG` -- language is compile-time only (one dict per build)

Any other text line is queued for speech synthesis.

## Architecture

- **Core 0:** Main loop -- serial I/O, command parsing, GPIO, queue mgmt
- **Core 1:** BackgroundAudio IRQ -- eSpeak-NG synthesis, DMA audio output

## License

This project is licensed under the GNU General Public License v3.0 - see the [LICENSE](LICENSE) file for details.
This is required by the `eSpeak-NG` and `BackgroundAudio` dependencies.
