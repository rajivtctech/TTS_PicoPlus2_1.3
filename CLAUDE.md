# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Serial Text-to-Speech module firmware for the **Pimoroni Pico Plus 2** (RP2350B, 16 MB Flash, 8 MB PSRAM). Spec reference: `TTS-PPC2-SPEC-001 Rev 1.2`.

## Build

**Arduino IDE settings (required):**
- Board: `Pimoroni PicoPlus2`
- Core: Earle Philhower arduino-pico v5.4.4
- CPU Speed: 150 MHz
- Optimize: `-O3`
- USB Stack: Pico SDK
- Flash Size: 16 MB (no FS)

**Required library:** `BackgroundAudio` v1.4.4 (via Arduino Library Manager)

```bash
# Using Arduino CLI
arduino-cli compile --fqbn rp2040:rp2040:pimoroni_pico_plus2 TTS_PicoPlus2_1.3/
arduino-cli upload --fqbn rp2040:rp2040:pimoroni_pico_plus2 --port /dev/ttyACM0 TTS_PicoPlus2_1.3/
```

## Critical: One Language Per Build

`BackgroundAudio v1.4.4` compiles one eSpeak-NG language dictionary into flash as the global `__espeakng_dict` symbol. Including two voice headers causes a **linker redefinition error**.

To change language, edit the single `#include` near the top of `TTS_PicoPlus2_1.3.ino` and update `currentVoice`:

```cpp
// Only ONE of these may be active at a time:
#include <libespeak-ng/voice/en_gb_scotland.h>   // English (Scottish)
// #include <libespeak-ng/voice/es.h>            // Spanish
// #include <libespeak-ng/voice/fr.h>            // French

static BackgroundAudioVoice *currentVoice = &voice_en_gb_scotland;
```

List available voices:
```bash
ls ~/Documents/Arduino/sketches/libraries/BackgroundAudio/src/libespeak-ng/voice/*.h
```

## Architecture

The firmware is a single `.ino` file with dual-core operation:

- **Core 0** (`loop()`): Polls USB CDC (`Serial`) and UART0 (`Serial1` on GP0/GP1), accumulates characters into line buffers, dispatches to `processLine()`, feeds the speech queue to the engine, and manages GPIO/LED state.
- **Core 1**: Runs the `BackgroundAudio` IRQ — eSpeak-NG synthesis with DMA audio output. No user code runs here; it is managed entirely by the library.

**Audio mode** is selected at boot by reading the `MODE_SEL` jumper (GP2):
- Pull-up HIGH → I2S output (MAX98357A amp + PCM5100 DAC on GP16/17/18)
- Tied LOW → PWM audio output (on GP16)

**Speech queue**: A fixed circular buffer (`SPEECH_QUEUE_SIZE=8`, `MAX_LINE_LEN=1024`) on Core 0. `loop()` checks `speech->playing()` and calls `speech->speak()` on the next queued item.

**Commands** arrive as `\COMMAND` lines via either serial interface. `processCommand()` handles `SPEED`, `PITCH`, `AMP`, `DAC`, `BAUD`, `STOP`, `STATUS`, `RESET`. Responses are echoed to both interfaces via `respondBoth()`.

**Limitations of BackgroundAudio v1.4.4:**
- No `setVolume()` — `\VOLUME` command is unsupported
- No runtime voice/language switching — `\VOICE` and `\LANG` are unsupported
- No `stop()` method — `\STOP` uses `speech->speak("")` to interrupt

## Pin Reference

| GPIO | Signal       | Direction | Notes                        |
|------|-------------|-----------|------------------------------|
| GP0  | UART TX     | Out       | Serial1 TX                   |
| GP1  | UART RX     | In        | Serial1 RX                   |
| GP2  | MODE_SEL    | In        | Pull-up: HIGH=I2S, GND=PWM   |
| GP3  | READY       | Out       | HIGH after init              |
| GP6  | AUDIO_ACTIVE| Out       | HIGH while audio playing     |
| GP7  | AMP_EN      | Out       | HIGH = MAX98357A on          |
| GP8  | DAC_MUTE    | Out       | LOW = PCM5100 on             |
| GP16 | I2S DOUT    | Out       | Also PWMAudio pin            |
| GP17 | I2S BCLK   | Out       |                              |
| GP18 | I2S LRCLK  | Out       | Auto-assigned as BCLK+1      |
| GP25 | LED         | Out       | Heartbeat/status             |
| GP45 | USER_BUTTON | In        | Pull-up                      |

**LED behaviour**: fast blink (100 ms) = error; solid = speaking; slow blink (500 ms) = idle.
