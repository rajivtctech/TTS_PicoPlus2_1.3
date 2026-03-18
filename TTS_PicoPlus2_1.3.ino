/*
 * ==========================================================================
 *  TTS_PicoPlus2.ino
 *  Serial Text-to-Speech Module -- eSpeak-NG on Pimoroni Pico Plus 2
 *  Specification Reference: TTS-PPC2-SPEC-001 Rev 1.2
 * --------------------------------------------------------------------------
 *
 *  Target Board : Pimoroni Pico Plus 2 (RP2350B, 16 MB Flash, 8 MB PSRAM)
 *  Arduino Core : Earle Philhower arduino-pico v5.4.4
 *  Audio Library: BackgroundAudio v1.4.4 (includes eSpeak-NG)
 *
 *  IDE Settings:
 *    Board             : Pimoroni PicoPlus2
 *    CPU Architecture  : ARM Cortex-M33
 *    CPU Speed         : 150 MHz
 *    Optimize          : -O3 (recommended) or -Os
 *    USB Stack         : Pico SDK
 *    Flash Size        : 16 MB (no FS)
 *
 *  IMPORTANT: BackgroundAudio v1.4.4 allows only ONE language dictionary
 *  per build. To change language, swap the voice #include below and
 *  rebuild. Multiple voices within the same language are not supported
 *  by the current library version.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 * ==========================================================================
 */

#include <BackgroundAudioSpeech.h>
#include <I2S.h>
#include <PWMAudio.h>

/* -- Voice data include --
 *  Only ONE voice header may be included per build (each pulls in a
 *  language dictionary that defines the global __espeakng_dict symbol).
 *
 *  To change language, comment out the current line and uncomment another.
 *  Available voices are in:
 *    BackgroundAudio/src/libespeak-ng/voice/
 *
 *  Examples:
 *    #include <libespeak-ng/voice/en_gb_scotland.h>   // English (Scottish)
 *    #include <libespeak-ng/voice/es.h>               // Spanish
 *    #include <libespeak-ng/voice/fr.h>               // French
 *    #include <libespeak-ng/voice/de.h>               // German
 *    #include <libespeak-ng/voice/ja.h>               // Japanese
 *    #include <libespeak-ng/voice/ru.h>               // Russian
 */
#include <libespeak-ng/voice/en_gb_scotland.h>

/* Active voice pointer (set once at compile time) */
static BackgroundAudioVoice *currentVoice = &voice_en_gb_scotland;

/* ==========================================================================
 *  PIN DEFINITIONS  (per TTS-PPC2-SPEC-001 Rev 1.2, Section 3)
 * ========================================================================== */

#define PIN_UART_TX       0
#define PIN_UART_RX       1
#define UART_DEFAULT_BAUD 115200

#define PIN_MODE_SEL      2     // Input, pull-up: HIGH=I2S, LOW=PWM
#define PIN_READY         3     // HIGH when initialised
#define PIN_AUDIO_ACTIVE  6     // HIGH while audio playing
#define PIN_AMP_EN        7     // HIGH = amplifier on
#define PIN_DAC_MUTE      8     // LOW = DAC on, HIGH = muted

#define PIN_I2S_DOUT      16
#define PIN_I2S_BCLK      17
#define PIN_I2S_LRCLK     18    // auto-assigned as BCLK+1

#define PIN_STATUS_LED    25
#define PIN_USER_BUTTON   45

/* ==========================================================================
 *  CONFIGURATION
 * ========================================================================== */

#define SAMPLE_RATE       22050
#define MAX_LINE_LEN      1024
#define SPEECH_QUEUE_SIZE 8
#define FW_VERSION        "TTS-PPC2 v1.2.2"

/* ==========================================================================
 *  AUDIO OUTPUT OBJECTS
 * ========================================================================== */

I2S       i2sOut(OUTPUT);
PWMAudio  pwmOut(PIN_I2S_DOUT);

BackgroundAudioSpeech speechI2S(i2sOut);
BackgroundAudioSpeech speechPWM(pwmOut);

BackgroundAudioSpeech *speech = nullptr;

/* ==========================================================================
 *  SPEECH QUEUE
 * ========================================================================== */

static char speechQueue[SPEECH_QUEUE_SIZE][MAX_LINE_LEN + 1];
static volatile int queueHead = 0;
static volatile int queueTail = 0;
static volatile int queueCount = 0;

bool enqueueLine(const char *line) {
    if (queueCount >= SPEECH_QUEUE_SIZE) return false;
    strncpy(speechQueue[queueTail], line, MAX_LINE_LEN);
    speechQueue[queueTail][MAX_LINE_LEN] = '\0';
    queueTail = (queueTail + 1) % SPEECH_QUEUE_SIZE;
    queueCount++;
    return true;
}

const char *peekQueue() {
    if (queueCount == 0) return nullptr;
    return speechQueue[queueHead];
}

void dequeueOne() {
    if (queueCount == 0) return;
    queueHead = (queueHead + 1) % SPEECH_QUEUE_SIZE;
    queueCount--;
}

void flushQueue() {
    queueHead = queueTail = queueCount = 0;
}

/* ==========================================================================
 *  LINE ACCUMULATORS
 * ========================================================================== */

static char lineBufUSB[MAX_LINE_LEN + 1];
static int  linePosUSB = 0;

static char lineBufUART[MAX_LINE_LEN + 1];
static int  linePosUART = 0;

/* ==========================================================================
 *  STATE
 * ========================================================================== */

static bool     modeI2S      = true;
static bool     ampEnabled   = true;
static bool     dacEnabled   = true;
static bool     isReady      = false;
static bool     isSpeaking   = false;
static uint32_t uartBaud     = UART_DEFAULT_BAUD;

static uint32_t ledLastToggle = 0;
static bool     ledState      = false;
static bool     errorState    = false;

/* ==========================================================================
 *  FORWARD DECLARATIONS
 * ========================================================================== */

void processLine(const char *line);
void processCommand(const char *cmd);
void respondBoth(const char *msg);
void updateAudioActive();
void updateLED();
void setAmpEnable(bool en);
void setDacEnable(bool en);

/* ==========================================================================
 *  SETUP
 * ========================================================================== */

void setup() {
    // Step 1: Safe defaults -- outputs muted during boot
    pinMode(PIN_READY,        OUTPUT);
    pinMode(PIN_AUDIO_ACTIVE, OUTPUT);
    pinMode(PIN_AMP_EN,       OUTPUT);
    pinMode(PIN_DAC_MUTE,     OUTPUT);
    pinMode(PIN_STATUS_LED,   OUTPUT);

    digitalWrite(PIN_READY,        LOW);
    digitalWrite(PIN_AUDIO_ACTIVE, LOW);
    digitalWrite(PIN_AMP_EN,       LOW);    // Amp off
    digitalWrite(PIN_DAC_MUTE,     HIGH);   // DAC muted
    digitalWrite(PIN_STATUS_LED,   LOW);

    // Step 2: Read MODE_SEL jumper
    pinMode(PIN_MODE_SEL, INPUT_PULLUP);
    delay(5);
    modeI2S = (digitalRead(PIN_MODE_SEL) == HIGH);

    pinMode(PIN_USER_BUTTON, INPUT_PULLUP);

    // Step 3: Initialise serial
    Serial.begin(115200);
    Serial1.setTX(PIN_UART_TX);
    Serial1.setRX(PIN_UART_RX);
    Serial1.begin(uartBaud, SERIAL_8N1);

    // Step 4: Initialise audio output
    if (modeI2S) {
        i2sOut.setBCLK(PIN_I2S_BCLK);
        i2sOut.setDOUT(PIN_I2S_DOUT);
        i2sOut.setFrequency(SAMPLE_RATE);
        speech = &speechI2S;
    } else {
        pwmOut.setFrequency(SAMPLE_RATE);
        speech = &speechPWM;
    }

    // Step 5: Begin BackgroundAudio + eSpeak-NG
    if (!speech->begin()) {
        errorState = true;
        respondBoth("ERROR: eSpeak-NG init failed. Check PSRAM.");
        while (true) { updateLED(); delay(10); }
    }

    // Step 6: Set default voice
    speech->setVoice(*currentVoice);

    // Step 7: Enable audio outputs
    setAmpEnable(true);
    setDacEnable(true);

    // Step 8: Signal READY
    isReady = true;
    digitalWrite(PIN_READY, HIGH);

    char banner[128];
    snprintf(banner, sizeof(banner),
             "%s | Mode: %s | READY", FW_VERSION, modeI2S ? "I2S" : "PWM");
    respondBoth(banner);
    respondBoth("Send text lines to speak. Commands start with \\");

    // Startup announcement
    enqueueLine("Text to speech module ready.");
}

/* ==========================================================================
 *  MAIN LOOP (Core 0)
 * ========================================================================== */

void loop() {
    // Poll USB CDC
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (linePosUSB > 0) {
                lineBufUSB[linePosUSB] = '\0';
                processLine(lineBufUSB);
                linePosUSB = 0;
            }
        } else if (linePosUSB < MAX_LINE_LEN) {
            lineBufUSB[linePosUSB++] = c;
        }
    }

    // Poll UART0
    while (Serial1.available()) {
        char c = Serial1.read();
        if (c == '\n' || c == '\r') {
            if (linePosUART > 0) {
                lineBufUART[linePosUART] = '\0';
                processLine(lineBufUART);
                linePosUART = 0;
            }
        } else if (linePosUART < MAX_LINE_LEN) {
            lineBufUART[linePosUART++] = c;
        }
    }

    // Feed speech queue to engine
    if (speech && !speech->playing()) {
        const char *next = peekQueue();
        if (next) {
            speech->speak(next);
            dequeueOne();
            isSpeaking = true;
        } else {
            isSpeaking = false;
        }
    }

    updateAudioActive();
    updateLED();
    delay(1);
}

/* ==========================================================================
 *  LINE PROCESSOR
 * ========================================================================== */

void processLine(const char *line) {
    if (line[0] == '\0') return;
    if (line[0] == '\\') {
        processCommand(line + 1);
        return;
    }
    if (!enqueueLine(line)) {
        respondBoth("WARN: Speech queue full, text dropped.");
    }
}

/* ==========================================================================
 *  COMMAND PARSER  (per TTS-PPC2-SPEC-001 Rev 1.2, Section 6)
 * ========================================================================== */

void processCommand(const char *cmd) {
    char upper[MAX_LINE_LEN + 1];
    int i;
    for (i = 0; cmd[i] && i < MAX_LINE_LEN; i++) {
        upper[i] = toupper(cmd[i]);
    }
    upper[i] = '\0';

    char response[256];

    // SPEED=<wpm>
    if (strncmp(upper, "SPEED=", 6) == 0) {
        int wpm = atoi(upper + 6);
        if (wpm >= 80 && wpm <= 450) {
            speech->setRate(wpm);
            snprintf(response, sizeof(response), "OK SPEED=%d", wpm);
        } else {
            snprintf(response, sizeof(response), "ERR SPEED out of range (80-450)");
        }
        respondBoth(response);
        return;
    }

    // PITCH=<n>
    if (strncmp(upper, "PITCH=", 6) == 0) {
        int p = atoi(upper + 6);
        if (p >= 0 && p <= 100) {
            speech->setPitch(p);
            snprintf(response, sizeof(response), "OK PITCH=%d", p);
        } else {
            snprintf(response, sizeof(response), "ERR PITCH out of range (0-100)");
        }
        respondBoth(response);
        return;
    }

    // VOLUME  (not available in BackgroundAudio v1.4.4)
    if (strncmp(upper, "VOLUME=", 7) == 0) {
        respondBoth("ERR VOLUME not supported by BackgroundAudio v1.4.4");
        return;
    }

    // VOICE / LANG  (compile-time only in v1.4.4 -- one dict per build)
    if (strncmp(upper, "VOICE=", 6) == 0 || strncmp(upper, "LANG=", 5) == 0) {
        respondBoth("ERR Voice/language is set at compile time (one dictionary per build).");
        respondBoth("     Change the #include in the sketch and rebuild.");
        return;
    }

    // AMP=ON|OFF
    if (strncmp(upper, "AMP=", 4) == 0) {
        if (strcmp(upper + 4, "ON") == 0) {
            setAmpEnable(true);  respondBoth("OK AMP=ON");
        } else if (strcmp(upper + 4, "OFF") == 0) {
            setAmpEnable(false); respondBoth("OK AMP=OFF");
        } else {
            respondBoth("ERR AMP: use ON or OFF");
        }
        return;
    }

    // DAC=ON|OFF
    if (strncmp(upper, "DAC=", 4) == 0) {
        if (strcmp(upper + 4, "ON") == 0) {
            setDacEnable(true);  respondBoth("OK DAC=ON");
        } else if (strcmp(upper + 4, "OFF") == 0) {
            setDacEnable(false); respondBoth("OK DAC=OFF");
        } else {
            respondBoth("ERR DAC: use ON or OFF");
        }
        return;
    }

    // BAUD=<rate>
    if (strncmp(upper, "BAUD=", 5) == 0) {
        long baud = atol(upper + 5);
        if (baud >= 1200 && baud <= 921600) {
            uartBaud = (uint32_t)baud;
            Serial1.end();
            Serial1.begin(uartBaud, SERIAL_8N1);
            snprintf(response, sizeof(response), "OK BAUD=%lu", (unsigned long)uartBaud);
            respondBoth(response);
        } else {
            respondBoth("ERR BAUD out of range (1200-921600)");
        }
        return;
    }

    // STOP  (no stop() in v1.4.4; speak("") to interrupt)
    if (strcmp(upper, "STOP") == 0) {
        flushQueue();
        speech->speak("");
        isSpeaking = false;
        respondBoth("OK STOP");
        return;
    }

    // STATUS
    if (strcmp(upper, "STATUS") == 0) {
        snprintf(response, sizeof(response),
                 "%s | Mode:%s | Queue:%d/%d | AMP:%s | DAC:%s | Baud:%lu",
                 FW_VERSION, modeI2S ? "I2S" : "PWM",
                 queueCount, SPEECH_QUEUE_SIZE,
                 ampEnabled ? "ON" : "OFF",
                 dacEnabled ? "ON" : "OFF",
                 (unsigned long)uartBaud);
        respondBoth(response);
        return;
    }

    // RESET
    if (strcmp(upper, "RESET") == 0) {
        respondBoth("OK RESET -- rebooting...");
        delay(100);
        rp2040.reboot();
        return;
    }

    snprintf(response, sizeof(response), "ERR Unknown command: \\%s", cmd);
    respondBoth(response);
}

/* ==========================================================================
 *  HELPERS
 * ========================================================================== */

void respondBoth(const char *msg) {
    Serial.println(msg);
    Serial1.println(msg);
}

void updateAudioActive() {
    bool active = (speech != nullptr) && speech->playing();
    digitalWrite(PIN_AUDIO_ACTIVE, active ? HIGH : LOW);
}

void setAmpEnable(bool en) {
    ampEnabled = en;
    digitalWrite(PIN_AMP_EN, en ? HIGH : LOW);
}

void setDacEnable(bool en) {
    dacEnabled = en;
    digitalWrite(PIN_DAC_MUTE, en ? LOW : HIGH);
}

void updateLED() {
    uint32_t now = millis();
    uint32_t interval;

    if (errorState) {
        interval = 100;
    } else if (isSpeaking || (speech && speech->playing())) {
        if (!ledState) { ledState = true; digitalWrite(PIN_STATUS_LED, HIGH); }
        ledLastToggle = now;
        return;
    } else {
        interval = 500;
    }

    if (now - ledLastToggle >= interval) {
        ledLastToggle = now;
        ledState = !ledState;
        digitalWrite(PIN_STATUS_LED, ledState ? HIGH : LOW);
    }
}
