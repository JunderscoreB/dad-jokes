/* SPDX-License-Identifier: MIT */
#include <pebble.h>
#include "jokes.h"

#define PERSIST_KEY_SEED           1
#define PERSIST_KEY_INDEX          2
#define PERSIST_KEY_CONFIG         3
#define PERSIST_KEY_TUNE_HISTORY   4
#define WAKEUP_REASON              1337
#define SECONDS_IN_DAY             86400
#define SCROLL_STEP_PX             40

// --- Polyphonic Chiptune Definitions ---
#define REST 0
#define D3   147
#define F3   175
#define G3   196
#define AS3  233
#define D4   294
#define E4   330
#define F4   349
#define FS4  370
#define G4   392
#define A4   440
#define B4   494
#define C5   523
#define D5   587
#define DS5  622
#define E5   659
#define F5   698
#define FS5  740
#define G5   784
#define GS5  831
#define A5   880
#define AS5  932
#define B5   988
#define C6   1046

#define EIGHTH        75
#define FAST_BEAT     100
#define BEAT_125      125
#define QUARTER       150
#define FAST_BEAT_X2  200
#define BEAT_250      250
#define HALF          300

#define ZELDA_EIGHTH     75
#define ZELDA_FAST_BEAT  100
#define ZELDA_BEAT_250   250
#define ZELDA_HALF       500

typedef struct {
    uint32_t freq_melody;
    uint32_t freq_harmony;
    uint32_t duration_ms;
    bool is_accented;
} PolyNote;

static const PolyNote s_mario_theme[] = {
    {E5, FS4, QUARTER, true}, {E5, FS4, QUARTER, true}, {REST, REST, QUARTER, false}, {E5, FS4, QUARTER, true},
    {REST, REST, QUARTER, false}, {C5, FS4, QUARTER, true}, {E5, FS4, HALF, true},
    {G5, B4, HALF, true}, {REST, REST, HALF, false},
    {G4, G3, HALF, true}, {REST, REST, HALF, false}
};

static const PolyNote s_hat_dance_theme[] = {
    {G5, E5, FAST_BEAT, false}, {FS5, DS5, FAST_BEAT, false}, {G5, E5, FAST_BEAT, true},
    {E5, C5, FAST_BEAT, false}, {DS5, B4, FAST_BEAT, false},  {D5, C5, FAST_BEAT, true},
    {C5, G4, FAST_BEAT, false}, {B5, G4, FAST_BEAT, false},   {C5, G4, FAST_BEAT, true},
    {G4, E4, FAST_BEAT_X2, true}
};

static const PolyNote s_circus_theme[] = {
    {C6, C5, BEAT_250, true}, {B5, G4, BEAT_250, true}, {AS5, C5, BEAT_125, false},
    {B5, G4, BEAT_125, false}, {AS5, C5, BEAT_125, false}, {A5, F5, BEAT_125, false},
    {GS5, C5, BEAT_250, true}, {G5, G4, BEAT_250, true}, {FS5, C5, BEAT_250, true},
    {G5, G4, BEAT_250, true}
};

static const PolyNote s_fart_theme[] = {
    {60, 55, 60, true}, {45, 40, 80, true}, {REST, REST, 15, false},
    {35, 30, 120, true}, {REST, REST, 20, false}, {25, 20, 200, true},
    {15, 12, 400, false}
};

static const PolyNote s_zelda_theme[] = {
    {G4, G3, ZELDA_BEAT_250, true}, {D4, D3, ZELDA_HALF, false},
    {G4, G3, ZELDA_FAST_BEAT, true}, {G4, REST, ZELDA_EIGHTH, false},
    {A4, REST, ZELDA_EIGHTH, false}, {B4, REST, ZELDA_EIGHTH, false},
    {C5, REST, ZELDA_EIGHTH, false}, {D5, G3, ZELDA_HALF, true}
};

static Window *s_main_window;
static ScrollLayer *s_scroll_layer;

static Layer *s_joke_layer;
static TextLayer *s_prompt_text_layer;
static Layer *s_up_arrow_layer;
static Layer *s_down_arrow_layer;

static AppTimer *s_timeout_timer = NULL;
static AppTimer *s_deferred_joke_timer = NULL;
static bool s_is_phone_ready = false;

static char s_current_joke_buffer[2048];
static uint16_t *s_joke_order = NULL;
static uint16_t s_current_index = 0;
static uint32_t s_shuffle_seed = 0;

typedef struct {
    int32_t mode;
    int32_t spec_hour;
    int32_t spec_minute;
    int32_t win_start;
    int32_t win_end;
    int32_t jokes_per_hour;
    int32_t timeout_sec;
    int32_t override_volume;
    int32_t alert_volume;
    int32_t alert_style;
    int32_t sound_tune;
    int32_t font_size;
    int32_t total_joke_count;
    int32_t dark_mode;
    int32_t flick_to_dismiss;
    int32_t enable_timeline;
    int32_t custom_joke_mode; // Maintained backward compatibility
} AppConfig;

static AppConfig s_config = {
    .mode = 0,
    .spec_hour = 12,
    .spec_minute = 0,
    .win_start = 9,
    .win_end = 17,
    .jokes_per_hour = 1,
    .timeout_sec = 30,
    .override_volume = 0,
    .alert_volume = 100,
    .alert_style = 0,
    .sound_tune = 1,
    .font_size = 2,
    .total_joke_count = 0,
    .dark_mode = 0,
    .flick_to_dismiss = 1,
    .enable_timeline = 1,
    .custom_joke_mode = 0
};

// Forward declarations
static void load_current_joke(void);
static void next_joke(void);
static void schedule_next_joke(void);
static void display_joke(const char *joke_text);
static GFont get_font_for_preference(int32_t font_pref);
static void format_and_sanitize_text(char *buffer);
static void play_alert(void);

// --- Theme Application ---

static void update_ui_colors(void) {
    GColor bg_color = s_config.dark_mode ? GColorBlack : GColorWhite;
    GColor fg_color = s_config.dark_mode ? GColorWhite : GColorBlack;

    window_set_background_color(s_main_window, bg_color);
    text_layer_set_background_color(s_prompt_text_layer, GColorClear);
    text_layer_set_text_color(s_prompt_text_layer, fg_color);
    scroll_layer_set_shadow_hidden(s_scroll_layer, true);

    ContentIndicator *indicator = scroll_layer_get_content_indicator(s_scroll_layer);
    GColor indicator_bg = s_config.dark_mode ? GColorBlack : GColorWhite;
    GColor indicator_fg = s_config.dark_mode ? GColorWhite : GColorBlack;

    ContentIndicatorConfig up_config = (ContentIndicatorConfig) {
        .layer = s_up_arrow_layer,
        .times_out = false,
        .alignment = PBL_IF_ROUND_ELSE(GAlignTop, GAlignCenter),
        .colors = { .foreground = indicator_fg, .background = indicator_bg }
    };
    content_indicator_configure_direction(indicator, ContentIndicatorDirectionUp, &up_config);

    ContentIndicatorConfig down_config = (ContentIndicatorConfig) {
        .layer = s_down_arrow_layer,
        .times_out = false,
        .alignment = PBL_IF_ROUND_ELSE(GAlignBottom, GAlignCenter),
        .colors = { .foreground = indicator_fg, .background = indicator_bg }
    };
    content_indicator_configure_direction(indicator, ContentIndicatorDirectionDown, &down_config);

    if (s_joke_layer) layer_mark_dirty(s_joke_layer);
}

// --- Custom Graphics Renderer ---

static void joke_layer_update_proc(Layer *layer, GContext *ctx) {
    GColor fg_color = s_config.dark_mode ? GColorWhite : GColorBlack;
    graphics_context_set_text_color(ctx, fg_color);

    GRect bounds = layer_get_bounds(layer);
    GFont font = get_font_for_preference(s_config.font_size);

    graphics_draw_text(ctx, s_current_joke_buffer, font, bounds,
                       GTextOverflowModeWordWrap,
                       PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft),
                       NULL);
}

// --- Timeout Engine ---

static void dismiss_app_handler(void *context) {
    s_timeout_timer = NULL;
    window_stack_pop_all(true);
}

static void reset_timeout_timer(void) {
    if (s_timeout_timer) {
        app_timer_cancel(s_timeout_timer);
        s_timeout_timer = NULL;
    }
    if (s_config.timeout_sec > 0) {
        s_timeout_timer = app_timer_register(s_config.timeout_sec * 1000, dismiss_app_handler, NULL);
    }
}

// --- PCM Synthesizer Engine ---

static void write_tone_to_speaker(uint16_t freq_hz, uint16_t duration_ms) {
    uint32_t total_samples = (16000 * duration_ms) / 1000;
    uint32_t half_period_samples = 16000 / (freq_hz * 2);
    if (half_period_samples == 0) half_period_samples = 1;

    int16_t buffer[256];
    uint32_t samples_written = 0;

    while (samples_written < total_samples) {
        uint32_t chunk_samples = total_samples - samples_written;
        if (chunk_samples > ARRAY_LENGTH(buffer)) chunk_samples = ARRAY_LENGTH(buffer);

        for (uint32_t i = 0; i < chunk_samples; i++) {
            buffer[i] = (((samples_written + i) / half_period_samples) % 2 == 0) ? 8000 : -8000;
        }

        uint32_t bytes_to_write = chunk_samples * sizeof(int16_t);
        uint32_t written_bytes = speaker_stream_write((const void*)buffer, bytes_to_write);

        if (written_bytes > 0) {
            samples_written += (written_bytes / sizeof(int16_t));
        } else {
            psleep(10);
        }
    }
}

static void write_mixed_cymbal_to_speaker(uint16_t freq_hz, uint16_t duration_ms) {
    uint32_t total_samples = (16000 * duration_ms) / 1000;
    uint32_t half_period_samples = 16000 / (freq_hz * 2);
    if (half_period_samples == 0) half_period_samples = 1;

    int16_t buffer[256];
    uint32_t samples_written = 0;

    while (samples_written < total_samples) {
        uint32_t chunk_samples = total_samples - samples_written;
        if (chunk_samples > ARRAY_LENGTH(buffer)) chunk_samples = ARRAY_LENGTH(buffer);

        for (uint32_t i = 0; i < chunk_samples; i++) {
            int16_t tone = (((samples_written + i) / half_period_samples) % 2 == 0) ? 5000 : -5000;
            int16_t noise = (rand() % 10000) - 5000;
            buffer[i] = tone + noise;
        }

        uint32_t bytes_to_write = chunk_samples * sizeof(int16_t);
        uint32_t written_bytes = speaker_stream_write((const void*)buffer, bytes_to_write);

        if (written_bytes > 0) {
            samples_written += (written_bytes / sizeof(int16_t));
        } else {
            psleep(10);
        }
    }
}

static void write_silence_to_speaker(uint16_t duration_ms) {
    uint32_t total_samples = (16000 * duration_ms) / 1000;
    int16_t buffer[256] = {0};
    uint32_t samples_written = 0;

    while (samples_written < total_samples) {
        uint32_t chunk_samples = total_samples - samples_written;
        if (chunk_samples > ARRAY_LENGTH(buffer)) chunk_samples = ARRAY_LENGTH(buffer);

        uint32_t bytes_to_write = chunk_samples * sizeof(int16_t);
        uint32_t written_bytes = speaker_stream_write((const void*)buffer, bytes_to_write);

        if (written_bytes > 0) {
            samples_written += (written_bytes / sizeof(int16_t));
        } else {
            psleep(10);
        }
    }
}

static void write_polyphonic_chiptune_tone(uint16_t freq_melody, uint16_t freq_harmony, uint16_t duration_ms, bool is_accented) {
    if (freq_melody == REST && freq_harmony == REST) {
        write_silence_to_speaker(duration_ms);
        return;
    }

    uint32_t total_samples = (16000 * duration_ms) / 1000;

    // Protect against divide-by-zero for resting channels
    uint32_t period1 = (freq_melody > 0) ? (16000 / freq_melody) : 1;
    uint32_t period2 = (freq_harmony > 0) ? (16000 / freq_harmony) : 1;

    // Channel 1: 25% duty cycle for bright lead
    uint32_t duty_thresh1 = period1 / 4;
    // Channel 2: 50% duty cycle for warmer harmony/bass
    uint32_t duty_thresh2 = period2 / 2;

    int16_t buffer[256];
    uint32_t samples_written = 0;

    // Dynamic starting amplitude based on accent status
    int16_t max_amplitude = is_accented ? 6000 : 2500;

    while (samples_written < total_samples) {
        uint32_t chunk_samples = total_samples - samples_written;
        if (chunk_samples > ARRAY_LENGTH(buffer)) {
            chunk_samples = ARRAY_LENGTH(buffer);
        }

        for (uint32_t i = 0; i < chunk_samples; i++) {
            uint32_t current_sample = samples_written + i;

            // Linear decay envelop applies to both channels down to 40% of starting volume
            int16_t current_amp = max_amplitude - ((max_amplitude * 6 / 10) * current_sample / total_samples);

            int16_t sample1 = 0;
            if (freq_melody > 0) {
                uint32_t pos1 = current_sample % period1;
                sample1 = (pos1 < duty_thresh1) ? current_amp : -current_amp;
            }

            int16_t sample2 = 0;
            if (freq_harmony > 0) {
                uint32_t pos2 = current_sample % period2;
                sample2 = (pos2 < duty_thresh2) ? current_amp : -current_amp;
            }

            // Mix the two waveforms
            buffer[i] = sample1 + sample2;
        }

        uint32_t bytes_to_write = chunk_samples * sizeof(int16_t);
        uint32_t written_bytes = speaker_stream_write((const void*)buffer, bytes_to_write);

        if (written_bytes > 0) {
            samples_written += (written_bytes / sizeof(int16_t));
        } else {
            psleep(10);
        }
    }
}

// --- Text Sanitization Engine ---

static void format_and_sanitize_text(char *buffer) {
    char *read_ptr = buffer;
    char *write_ptr = buffer;

    while (*read_ptr) {
        if (*read_ptr == '\\' && *(read_ptr + 1) == 'n') {
            *write_ptr++ = '\n';
            read_ptr += 2;
        }
        else if ((unsigned char)read_ptr[0] == 0xE2 && (unsigned char)read_ptr[1] == 0x80) {
            unsigned char byte3 = (unsigned char)read_ptr[2];

            if (byte3 == 0x98 || byte3 == 0x99) {
                *write_ptr++ = '\'';
                read_ptr += 3;
            } else if (byte3 == 0x9C || byte3 == 0x9D) {
                *write_ptr++ = '"';
                read_ptr += 3;
            } else if (byte3 == 0xA6) {
                *write_ptr++ = '.';
                *write_ptr++ = '.';
                *write_ptr++ = '.';
                read_ptr += 3;
            } else {
                *write_ptr++ = *read_ptr++;
            }
        }
        else {
            *write_ptr++ = *read_ptr++;
        }
    }

    while (write_ptr > buffer && (*(write_ptr - 1) == '\n' || *(write_ptr - 1) == '\r' || *(write_ptr - 1) == ' ')) {
        write_ptr--;
    }
    *write_ptr = '\0';
}

// --- Font Selection Helper ---

static GFont get_font_for_preference(int32_t font_pref) {
    switch (font_pref) {
        case 0: return fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
        case 1: return fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
        case 2: return fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK);
        case 3: default: return fonts_get_system_font(FONT_KEY_BITHAM_42_LIGHT);
    }
}

// --- Scheduling & Alert Logic ---

static void schedule_next_joke(void) {
    time_t now = time(NULL);
    time_t midnight = time_start_of_today();
    time_t target;

    if (s_config.mode == 0) {
        target = midnight + (s_config.spec_hour * 3600) + (s_config.spec_minute * 60);

        if (target <= now) {
            target += SECONDS_IN_DAY;
        }
    } else {
        int32_t window_start_sec = s_config.win_start * 3600;
        int32_t window_end_sec = (s_config.win_end + 1) * 3600;
        if (window_end_sec <= window_start_sec) window_end_sec = window_start_sec + 3600;

        int32_t jokes_per_hour = s_config.jokes_per_hour > 0 ? s_config.jokes_per_hour : 1;
        int32_t interval = 3600 / jokes_per_hour;

        int32_t min_gap = interval / 2;
        if (min_gap < 65) {
            min_gap = 65;
        }

        time_t start_time = midnight + window_start_sec;
        time_t end_time = midnight + window_end_sec;

        if (now < start_time) {
            target = start_time + (rand() % interval);
        } else if (now >= end_time) {
            target = start_time + SECONDS_IN_DAY + (rand() % interval);
        } else {
            target = now + min_gap + (rand() % interval);
            if (target > end_time) {
                target = start_time + SECONDS_IN_DAY + (rand() % interval);
            }
        }
    }

    wakeup_cancel_all();

    WakeupId id;
    int retries = 0;
    do {
        id = wakeup_schedule(target, WAKEUP_REASON, false);
        if (id == E_RANGE) {
            target += 60;
        }
        retries++;
    } while (id == E_RANGE && retries < 15);
}

static void play_alert(void) {
    if (quiet_time_is_active()) return;

    bool do_vibe = (s_config.alert_style == 0 || s_config.alert_style == 2);
    bool do_sound = (s_config.alert_style == 1 || s_config.alert_style == 2);

    if (do_vibe) {
        uint32_t segments[] = { 200, 100, 200 };
        VibePattern pat = { .durations = segments, .num_segments = ARRAY_LENGTH(segments) };
        vibes_enqueue_custom_pattern(pat);
    }

    if (do_sound) {
        WatchInfoModel model = watch_info_get_model();
        if (model == WATCH_INFO_MODEL_COREDEVICES_PT2 || model == WATCH_INFO_MODEL_PEBBLE_TIME_2) {
            if (speaker_is_muted()) return;

            uint8_t current_volume = s_config.override_volume ? s_config.alert_volume : 100;
            if (current_volume > 100) current_volume = 100;

            if (speaker_stream_open(SpeakerPcmFormat_16kHz_16bit, current_volume)) {
                write_silence_to_speaker(250);

                int32_t active_tune = s_config.sound_tune;

                if (active_tune == 8) {
                    time_t now = time(NULL);
                    srand(now);

                    time_t tune_history[9][2] = {{0}};
                    if (persist_exists(PERSIST_KEY_TUNE_HISTORY)) {
                        persist_read_data(PERSIST_KEY_TUNE_HISTORY, tune_history, sizeof(tune_history));
                    }

                    int32_t next_tune = (rand() % 7) + 1;
                    int attempts = 0;

                    while (attempts < 7) {
                        if (tune_history[next_tune][1] == 0 || (now - tune_history[next_tune][1]) >= 3600) {
                            break;
                        }
                        next_tune = (next_tune % 7) + 1;
                        attempts++;
                    }

                    tune_history[next_tune][1] = tune_history[next_tune][0];
                    tune_history[next_tune][0] = now;
                    persist_write_data(PERSIST_KEY_TUNE_HISTORY, tune_history, sizeof(tune_history));

                    active_tune = next_tune;
                }

                if (active_tune == 7) {
                    for (uint32_t i = 0; i < ARRAY_LENGTH(s_zelda_theme); i++) {
                        write_polyphonic_chiptune_tone(s_zelda_theme[i].freq_melody,
                                                       s_zelda_theme[i].freq_harmony,
                                                       s_zelda_theme[i].duration_ms,
                                                       s_zelda_theme[i].is_accented);
                    }
                } else if (active_tune == 6) {
                    for (uint32_t i = 0; i < ARRAY_LENGTH(s_hat_dance_theme); i++) {
                        write_polyphonic_chiptune_tone(s_hat_dance_theme[i].freq_melody,
                                                       s_hat_dance_theme[i].freq_harmony,
                                                       s_hat_dance_theme[i].duration_ms,
                                                       s_hat_dance_theme[i].is_accented);
                    }
                } else if (active_tune == 5) {
                    for (uint32_t i = 0; i < ARRAY_LENGTH(s_mario_theme); i++) {
                        write_polyphonic_chiptune_tone(s_mario_theme[i].freq_melody,
                                                       s_mario_theme[i].freq_harmony,
                                                       s_mario_theme[i].duration_ms,
                                                       s_mario_theme[i].is_accented);
                    }
                } else if (active_tune == 4) {
                    for (uint32_t i = 0; i < ARRAY_LENGTH(s_circus_theme); i++) {
                        write_polyphonic_chiptune_tone(s_circus_theme[i].freq_melody,
                                                       s_circus_theme[i].freq_harmony,
                                                       s_circus_theme[i].duration_ms,
                                                       s_circus_theme[i].is_accented);
                    }
                } else if (active_tune == 3) {
                    for (uint32_t i = 0; i < ARRAY_LENGTH(s_fart_theme); i++) {
                        write_polyphonic_chiptune_tone(s_fart_theme[i].freq_melody,
                                                       s_fart_theme[i].freq_harmony,
                                                       s_fart_theme[i].duration_ms,
                                                       s_fart_theme[i].is_accented);
                    }
                } else if (active_tune == 2) {
                    write_tone_to_speaker(392, 60); write_silence_to_speaker(30);
                    write_tone_to_speaker(440, 125); write_silence_to_speaker(30);
                    write_mixed_cymbal_to_speaker(523, 125);
                } else if (active_tune == 1) {
                    write_tone_to_speaker(4096, 125); write_silence_to_speaker(125);
                    write_tone_to_speaker(4096, 125);
                }

                write_silence_to_speaker(400);
                speaker_stop();
                speaker_stream_close();
            }
        }
    }
}

// --- Data & Rollover Logic ---

static void shuffle_jokes(void) {
    if (s_config.total_joke_count == 0) return;

    if (s_joke_order) free(s_joke_order);
    s_joke_order = malloc(s_config.total_joke_count * sizeof(uint16_t));
    if (!s_joke_order) return;

    srand(s_shuffle_seed);
    for (uint16_t i = 0; i < s_config.total_joke_count; i++) {
        s_joke_order[i] = i;
    }

    for (uint16_t i = s_config.total_joke_count - 1; i > 0; i--) {
        uint16_t j = rand() % (i + 1);
        uint16_t temp = s_joke_order[i];
        s_joke_order[i] = s_joke_order[j];
        s_joke_order[j] = temp;
    }
}

static void display_joke(const char *joke_text) {
    bool is_gabbro = PBL_PLATFORM_SWITCH(PBL_PLATFORM_TYPE_CURRENT, false, false, false, false, false, false, true);
    uint8_t buffer_offset = is_gabbro ? 1 : 0;

    if (joke_text != s_current_joke_buffer) {
        strncpy(s_current_joke_buffer + buffer_offset, joke_text, sizeof(s_current_joke_buffer) - 1 - buffer_offset);
        s_current_joke_buffer[sizeof(s_current_joke_buffer) - 1] = '\0';

        format_and_sanitize_text(s_current_joke_buffer + buffer_offset);

        if (is_gabbro) {
            s_current_joke_buffer[0] = '\n';
        }
    }

    GFont font = get_font_for_preference(s_config.font_size);
    GRect full_bounds = layer_get_bounds(window_get_root_layer(s_main_window));

    int16_t horizontal_margin = PBL_IF_ROUND_ELSE(16, 10);
    int16_t text_width = full_bounds.size.w - (horizontal_margin * 2);

    GSize content_size = graphics_text_layout_get_content_size(
        s_current_joke_buffer, font, GRect(0, 0, text_width, 10000),
                                                               GTextOverflowModeWordWrap,
                                                               PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft));

    int16_t top_margin = PBL_IF_ROUND_ELSE(20, 5);
    int16_t bottom_margin = PBL_IF_ROUND_ELSE(40, 24);
    int16_t text_layer_height = content_size.h + 10;

    layer_set_frame(s_joke_layer, GRect(horizontal_margin, top_margin, text_width, text_layer_height));

    int16_t total_scroll_height = top_margin + text_layer_height + bottom_margin;
    if (total_scroll_height < full_bounds.size.h) {
        total_scroll_height = full_bounds.size.h;
    }

    scroll_layer_set_content_size(s_scroll_layer, GSize(full_bounds.size.w, total_scroll_height));
    scroll_layer_set_content_offset(s_scroll_layer, GPointZero, false);

    layer_mark_dirty(s_joke_layer);
}

// Extract a specific joke directly from the .txt resource in flash memory
static void load_builtin_joke(uint16_t joke_id) {
    ResHandle handle = resource_get_handle(RESOURCE_ID_BUILT_IN_JOKES);
    size_t res_size = resource_size(handle);

    uint16_t current_joke = 0;
    uint32_t offset = 0;
    char buffer[1024];
    uint16_t buf_idx = 0;
    uint8_t chunk[64];

    // Read incrementally to minimize RAM usage
    while (offset < res_size) {
        size_t bytes_to_read = res_size - offset;
        if (bytes_to_read > sizeof(chunk)) bytes_to_read = sizeof(chunk);
        resource_load_byte_range(handle, offset, chunk, bytes_to_read);

        for (size_t i = 0; i < bytes_to_read; i++) {
            if (current_joke == joke_id) {
                if (chunk[i] == '\n' || chunk[i] == '\0') {
                    buffer[buf_idx] = '\0';
                    display_joke(buffer);
                    return;
                }
                if (buf_idx < sizeof(buffer) - 1) {
                    buffer[buf_idx++] = chunk[i];
                }
            } else {
                if (chunk[i] == '\n' || chunk[i] == '\0') {
                    current_joke++;
                }
            }
        }
        offset += bytes_to_read;
    }
    display_joke("Joke not found.");
}

static void deferred_joke_request_cb(void *context) {
    s_deferred_joke_timer = NULL;
    load_current_joke();
    schedule_next_joke();
}

static void load_current_joke(void) {
    if (s_config.total_joke_count == 0) {
        if (!s_is_phone_ready && s_config.custom_joke_mode == 0) {
            s_config.total_joke_count = NUM_BUILT_IN_JOKES;
            shuffle_jokes();
        } else if (!s_is_phone_ready) {
            display_joke("Connecting to phone...\n\nPlease wait.");
            return;
        } else {
            display_joke("No jokes found.\n\nPlease add some in the Pebble app settings.");
            return;
        }
    }

    if (s_current_index >= s_config.total_joke_count) {
        s_current_index = 0;
    }

    uint16_t joke_id = s_joke_order[s_current_index];

    bool is_built_in = false;
    if (s_config.custom_joke_mode == 0) {
        is_built_in = true;
    } else if (s_config.custom_joke_mode == 1 && joke_id < NUM_BUILT_IN_JOKES) {
        is_built_in = true;
    }

    // Read directly from watch flash storage
    if (is_built_in) {
        if (joke_id < NUM_BUILT_IN_JOKES) {
            load_builtin_joke(joke_id);
        } else {
            display_joke("Joke ID out of bounds.");
        }
        return;
    }

    // Phone is only queried for custom overrides
    if (!connection_service_peek_pebble_app_connection()) {
        display_joke("Phone disconnected.\n\nPlease reconnect to load a custom joke.");
        return;
    }

    DictionaryIterator *iter;
    AppMessageResult outbox_res = app_message_outbox_begin(&iter);
    if (outbox_res == APP_MSG_OK) {
        dict_write_uint32(iter, MESSAGE_KEY_RequestJokeIdx, joke_id);
        if (app_message_outbox_send() == APP_MSG_OK) {
            return;
        }
    } else if (outbox_res == APP_MSG_BUSY) {
        if (s_deferred_joke_timer) app_timer_cancel(s_deferred_joke_timer);
        s_deferred_joke_timer = app_timer_register(150, deferred_joke_request_cb, NULL);
        return;
    }

    display_joke("Failed to request joke.\n\nRetrying...");
}

static void next_joke(void) {
    if (s_config.total_joke_count == 0) {
        load_current_joke();
        return;
    }

    s_current_index++;
    if (s_current_index >= s_config.total_joke_count) {
        s_shuffle_seed = time(NULL);
        s_current_index = 0;
        persist_write_int(PERSIST_KEY_SEED, s_shuffle_seed);
        shuffle_jokes();
    }
    persist_write_int(PERSIST_KEY_INDEX, s_current_index);
    load_current_joke();
}

// --- Scrolling Helper ---

static void adjust_scroll_offset(int16_t delta_y, bool animated) {
    reset_timeout_timer();
    GPoint offset = scroll_layer_get_content_offset(s_scroll_layer);
    GRect full_bounds = layer_get_bounds(window_get_root_layer(s_main_window));
    GSize content_size = scroll_layer_get_content_size(s_scroll_layer);

    int16_t visible_height = full_bounds.size.h;
    int16_t min_y = -(content_size.h - visible_height);
    if (min_y > 0) min_y = 0;

    offset.y += delta_y;
    if (offset.y > 0) offset.y = 0;
    if (offset.y < min_y) offset.y = min_y;

    scroll_layer_set_content_offset(s_scroll_layer, offset, animated);
}

// --- Interaction Handlers ---

static void button_dismiss_handler(ClickRecognizerRef recognizer, void *context) {
    window_stack_pop_all(true);
}

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
    adjust_scroll_offset(SCROLL_STEP_PX, true);
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
    adjust_scroll_offset(-SCROLL_STEP_PX, true);
}

static void next_joke_handler(ClickRecognizerRef recognizer, void *context) {
    reset_timeout_timer();
    next_joke();
    schedule_next_joke();
}

static void cycle_tune_up_handler(ClickRecognizerRef recognizer, void *context) {
    s_config.sound_tune++;
    if (s_config.sound_tune > 8) s_config.sound_tune = 0;
    persist_write_data(PERSIST_KEY_CONFIG, &s_config, sizeof(AppConfig));
    play_alert();
}

static void cycle_tune_down_handler(ClickRecognizerRef recognizer, void *context) {
    s_config.sound_tune--;
    if (s_config.sound_tune < 0) s_config.sound_tune = 8;
    persist_write_data(PERSIST_KEY_CONFIG, &s_config, sizeof(AppConfig));
    play_alert();
}

static void click_config_provider(void *context) {
    window_single_click_subscribe(BUTTON_ID_BACK, button_dismiss_handler);
    window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
    window_single_click_subscribe(BUTTON_ID_SELECT, next_joke_handler);
    window_single_click_subscribe(BUTTON_ID_DOWN, down_click_handler);

    window_long_click_subscribe(BUTTON_ID_UP, 500, cycle_tune_up_handler, NULL);
    window_long_click_subscribe(BUTTON_ID_DOWN, 500, cycle_tune_down_handler, NULL);
}

static void tap_handler(AccelAxisType axis, int32_t direction) {
    if (s_config.flick_to_dismiss) {
        window_stack_pop_all(true);
    }
}

static void touch_handler(const TouchEvent *event, void *context) {
    static int16_t s_last_touch_y = 0;
    reset_timeout_timer();

    if (event->type == TouchEvent_Touchdown) {
        s_last_touch_y = event->y;
    } else if (event->type == TouchEvent_PositionUpdate) {
        int16_t delta_y = event->y - s_last_touch_y;
        adjust_scroll_offset(delta_y, false);
        s_last_touch_y = event->y;
    }
}

static void wakeup_handler(WakeupId id, int32_t reason) {
    reset_timeout_timer();
    play_alert();
    next_joke();
    schedule_next_joke();
}

// --- AppMessage Communications ---

static int32_t get_int_from_tuple(Tuple *t) {
    if (!t) return 0;
    if (t->type == TUPLE_CSTRING) return atoi(t->value->cstring);
    if (t->type == TUPLE_UINT) return t->value->uint32;
    if (t->type == TUPLE_INT) return t->value->int32;
    return 0;
}

static void outbox_sent_handler(DictionaryIterator *iterator, void *context) {
}

static void outbox_failed_handler(DictionaryIterator *iterator, AppMessageResult reason, void *context) {
    display_joke("Failed to fetch joke from phone.\n\nCheck Bluetooth connection.");
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
    Tuple *deliver_t = dict_find(iter, MESSAGE_KEY_DeliverJokeText);
    if (deliver_t) {
        bool is_gabbro = PBL_PLATFORM_SWITCH(PBL_PLATFORM_TYPE_CURRENT, false, false, false, false, false, false, true);
        uint8_t buffer_offset = is_gabbro ? 1 : 0;

        size_t max_len = sizeof(s_current_joke_buffer) - 1 - buffer_offset;

        strncpy(s_current_joke_buffer + buffer_offset, deliver_t->value->cstring, max_len);
        s_current_joke_buffer[max_len + buffer_offset] = '\0';

        format_and_sanitize_text(s_current_joke_buffer + buffer_offset);

        if (is_gabbro) {
            s_current_joke_buffer[0] = '\n';
        }

        display_joke(s_current_joke_buffer);
        return;
    }

    bool should_reshuffle = false;
    bool should_update_colors = false;

    Tuple *mode_t = dict_find(iter, MESSAGE_KEY_ScheduleMode);
    if (mode_t) s_config.mode = get_int_from_tuple(mode_t);

    Tuple *custom_mode_t = dict_find(iter, MESSAGE_KEY_CustomJokeMode);
    if (custom_mode_t) s_config.custom_joke_mode = get_int_from_tuple(custom_mode_t);

    Tuple *spec_hr_t = dict_find(iter, MESSAGE_KEY_SpecificHour);
    if (spec_hr_t) s_config.spec_hour = get_int_from_tuple(spec_hr_t);

    Tuple *spec_min_t = dict_find(iter, MESSAGE_KEY_SpecificMinute);
    if (spec_min_t) s_config.spec_minute = get_int_from_tuple(spec_min_t);

    Tuple *win_st_t = dict_find(iter, MESSAGE_KEY_WindowStartHour);
    if (win_st_t) s_config.win_start = get_int_from_tuple(win_st_t);

    Tuple *win_end_t = dict_find(iter, MESSAGE_KEY_WindowEndHour);
    if (win_end_t) s_config.win_end = get_int_from_tuple(win_end_t);

    Tuple *jokes_t = dict_find(iter, MESSAGE_KEY_JokesPerHour);
    if (jokes_t) s_config.jokes_per_hour = get_int_from_tuple(jokes_t);

    Tuple *timeout_t = dict_find(iter, MESSAGE_KEY_TimeoutSec);
    if (timeout_t) {
        s_config.timeout_sec = get_int_from_tuple(timeout_t);
        if (s_config.timeout_sec > 0 && s_config.timeout_sec < 15) {
            s_config.timeout_sec = 15;
        }
        reset_timeout_timer();
    }

    Tuple *alert_t = dict_find(iter, MESSAGE_KEY_AlertStyle);
    if (alert_t) s_config.alert_style = get_int_from_tuple(alert_t);

    Tuple *override_vol_t = dict_find(iter, MESSAGE_KEY_OverrideVolume);
    if (override_vol_t) s_config.override_volume = get_int_from_tuple(override_vol_t);

    Tuple *vol_t = dict_find(iter, MESSAGE_KEY_AlertVolume);
    if (vol_t) s_config.alert_volume = get_int_from_tuple(vol_t);

    Tuple *sound_t = dict_find(iter, MESSAGE_KEY_SoundTune);
    if (sound_t) s_config.sound_tune = get_int_from_tuple(sound_t);

    Tuple *font_size_t = dict_find(iter, MESSAGE_KEY_FontSize);
    if (font_size_t) s_config.font_size = get_int_from_tuple(font_size_t);

    Tuple *dark_mode_t = dict_find(iter, MESSAGE_KEY_DarkMode);
    if (dark_mode_t) {
        int32_t new_dark_mode = get_int_from_tuple(dark_mode_t);
        if (s_config.dark_mode != new_dark_mode) {
            s_config.dark_mode = new_dark_mode;
            should_update_colors = true;
        }
    }

    Tuple *flick_t = dict_find(iter, MESSAGE_KEY_FlickToDismiss);
    if (flick_t) {
        s_config.flick_to_dismiss = get_int_from_tuple(flick_t);
    }

    Tuple *timeline_t = dict_find(iter, MESSAGE_KEY_EnableTimeline);
    if (timeline_t) {
        s_config.enable_timeline = get_int_from_tuple(timeline_t);
    }

    Tuple *count_t = dict_find(iter, MESSAGE_KEY_TotalJokeCount);
    if (count_t) {
        s_is_phone_ready = true;
        int32_t new_count = get_int_from_tuple(count_t);
        if (s_config.total_joke_count != new_count) {
            s_config.total_joke_count = new_count;
            should_reshuffle = true;
        }
    }

    persist_write_data(PERSIST_KEY_CONFIG, &s_config, sizeof(AppConfig));

    if (should_update_colors) {
        update_ui_colors();
    }

    if (should_reshuffle) {
        s_shuffle_seed = time(NULL);
        s_current_index = 0;
        persist_write_int(PERSIST_KEY_SEED, s_shuffle_seed);
        persist_write_int(PERSIST_KEY_INDEX, s_current_index);
        shuffle_jokes();
    }

    load_current_joke();
    schedule_next_joke();
}

// --- Window Lifecycle ---

static void main_window_load(Window *window) {
    GRect full_bounds = layer_get_bounds(window_get_root_layer(window));

    s_scroll_layer = scroll_layer_create(full_bounds);
    scroll_layer_set_click_config_onto_window(s_scroll_layer, window);

    ScrollLayerCallbacks callbacks = {
        .click_config_provider = click_config_provider
    };
    scroll_layer_set_callbacks(s_scroll_layer, callbacks);

    s_joke_layer = layer_create(full_bounds);
    layer_set_update_proc(s_joke_layer, joke_layer_update_proc);

    scroll_layer_add_child(s_scroll_layer, s_joke_layer);
    layer_add_child(window_get_root_layer(window), scroll_layer_get_layer(s_scroll_layer));

    s_prompt_text_layer = text_layer_create(GRect(full_bounds.size.w - PBL_IF_ROUND_ELSE(35, 30), (full_bounds.size.h / 2) - 10, PBL_IF_ROUND_ELSE(30, 28), 20));
    text_layer_set_text(s_prompt_text_layer, "next");
    text_layer_set_text_alignment(s_prompt_text_layer, GTextAlignmentRight);
    text_layer_set_font(s_prompt_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
    layer_add_child(window_get_root_layer(window), text_layer_get_layer(s_prompt_text_layer));

    s_up_arrow_layer = layer_create(PBL_IF_ROUND_ELSE(
        GRect(0, 0, full_bounds.size.w, 15),
                                                      GRect(full_bounds.size.w - 20, 0, 20, 20)
    ));
    layer_add_child(window_get_root_layer(window), s_up_arrow_layer);

    s_down_arrow_layer = layer_create(PBL_IF_ROUND_ELSE(
        GRect(0, full_bounds.size.h - 15, full_bounds.size.w, 15),
                                                        GRect(full_bounds.size.w - 20, full_bounds.size.h - 20, 20, 20)
    ));
    layer_add_child(window_get_root_layer(window), s_down_arrow_layer);

    if (touch_service_is_enabled()) {
        touch_service_subscribe(touch_handler, NULL);
    }

    update_ui_colors();

    if (launch_reason() == APP_LAUNCH_WAKEUP) {
        WakeupId id = 0;
        int32_t reason = 0;
        wakeup_get_launch_event(&id, &reason);
        play_alert();
        next_joke();
        schedule_next_joke();
    } else {
        load_current_joke();
    }

    reset_timeout_timer();
}

static void main_window_unload(Window *window) {
    if (touch_service_is_enabled()) {
        touch_service_unsubscribe();
    }
    if (s_timeout_timer) {
        app_timer_cancel(s_timeout_timer);
        s_timeout_timer = NULL;
    }
    if (s_deferred_joke_timer) {
        app_timer_cancel(s_deferred_joke_timer);
        s_deferred_joke_timer = NULL;
    }
    layer_destroy(s_joke_layer);
    text_layer_destroy(s_prompt_text_layer);
    layer_destroy(s_up_arrow_layer);
    layer_destroy(s_down_arrow_layer);
    scroll_layer_destroy(s_scroll_layer);
}

// --- App Lifecycle ---

static void init(void) {
    srand(time(NULL));

    if (persist_exists(PERSIST_KEY_CONFIG)) {
        size_t read_size = persist_get_size(PERSIST_KEY_CONFIG);
        if (read_size <= sizeof(AppConfig)) {
            persist_read_data(PERSIST_KEY_CONFIG, &s_config, read_size);
            if (s_config.timeout_sec > 0 && s_config.timeout_sec < 15) {
                s_config.timeout_sec = 15;
            }
        }
    }

    if (s_config.total_joke_count == 0 && s_config.custom_joke_mode == 0) {
        s_config.total_joke_count = NUM_BUILT_IN_JOKES;
    }

    if (s_config.total_joke_count > 0) {
        if (persist_exists(PERSIST_KEY_SEED) && persist_exists(PERSIST_KEY_INDEX)) {
            s_shuffle_seed = persist_read_int(PERSIST_KEY_SEED);
            s_current_index = persist_read_int(PERSIST_KEY_INDEX);
        } else {
            s_shuffle_seed = time(NULL);
            s_current_index = 0;
            persist_write_int(PERSIST_KEY_SEED, s_shuffle_seed);
            persist_write_int(PERSIST_KEY_INDEX, s_current_index);
        }
        shuffle_jokes();
    }

    app_message_register_inbox_received(inbox_received_handler);
    app_message_register_outbox_sent(outbox_sent_handler);
    app_message_register_outbox_failed(outbox_failed_handler);

    app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());

    wakeup_service_subscribe(wakeup_handler);
    accel_tap_service_subscribe(tap_handler);

    s_main_window = window_create();
    window_set_window_handlers(s_main_window, (WindowHandlers) {
        .load = main_window_load,
        .unload = main_window_unload
    });

    window_stack_push(s_main_window, true);
}

static void deinit(void) {
    app_message_deregister_callbacks();
    accel_tap_service_unsubscribe();
    window_destroy(s_main_window);

    if (s_joke_order) free(s_joke_order);
}

int main(void) {
    init();
    app_event_loop();
    deinit();
}
