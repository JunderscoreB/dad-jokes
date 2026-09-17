/* SPDX-License-Identifier: Apache-2.0 */
#include <pebble.h>
#include "jokes.h"

#define PERSIST_KEY_ORDER  1
#define PERSIST_KEY_INDEX  2
#define PERSIST_KEY_CONFIG 3
#define WAKEUP_REASON      1337
#define SECONDS_IN_DAY     86400
#define SCROLL_STEP_PX     40
#define FOOTER_HEIGHT      24

static Window *s_main_window;
static ScrollLayer *s_scroll_layer;
static TextLayer *s_joke_text_layer;
static TextLayer *s_prompt_text_layer;

static uint8_t *s_joke_order = NULL;
static int32_t s_current_index = 0;

// Application configuration received from Pebble-Clay on the phone
typedef struct {
    int32_t mode;
    int32_t spec_hour;
    int32_t spec_minute;
    int32_t win_start;
    int32_t win_end;
    int32_t alert_style;
    int32_t sound_tune;
    int32_t font_size;
} AppConfig;

static AppConfig s_config = {
    .mode = 0,
    .spec_hour = 12,
    .spec_minute = 0,
    .win_start = 9,
    .win_end = 17,
    .alert_style = 0,
    .sound_tune = 0,
    .font_size = 2
};

// --- PCM Synthesizer Engine ---

static void write_tone_to_speaker(uint16_t freq_hz, uint16_t duration_ms) {
    uint32_t total_samples = (16000 * duration_ms) / 1000;
    uint32_t half_period_samples = 16000 / (freq_hz * 2);
    if (half_period_samples == 0) half_period_samples = 1;

    int16_t buffer[128];
    uint32_t samples_written = 0;
    uint32_t phase = 0;

    while (samples_written < total_samples) {
        uint32_t chunk_samples = total_samples - samples_written;
        if (chunk_samples > ARRAY_LENGTH(buffer)) chunk_samples = ARRAY_LENGTH(buffer);

        // Generate Square Wave
        for (uint32_t i = 0; i < chunk_samples; i++) {
            buffer[i] = ((phase / half_period_samples) % 2 == 0) ? 8000 : -8000;
            phase++;
        }

        uint32_t bytes_to_write = chunk_samples * sizeof(int16_t);
        uint32_t written_bytes = speaker_stream_write((const void*)buffer, bytes_to_write);

        if (written_bytes < bytes_to_write) {
            psleep(20);
        } else {
            samples_written += chunk_samples;
        }
    }
}

static void write_noise_to_speaker(uint16_t duration_ms) {
    uint32_t total_samples = (16000 * duration_ms) / 1000;
    int16_t buffer[128];
    uint32_t samples_written = 0;

    while (samples_written < total_samples) {
        uint32_t chunk_samples = total_samples - samples_written;
        if (chunk_samples > ARRAY_LENGTH(buffer)) chunk_samples = ARRAY_LENGTH(buffer);

        // Generate White Noise for snare/cymbal crashes
        for (uint32_t i = 0; i < chunk_samples; i++) {
            buffer[i] = (rand() % 16000) - 8000;
        }

        uint32_t bytes_to_write = chunk_samples * sizeof(int16_t);
        uint32_t written_bytes = speaker_stream_write((const void*)buffer, bytes_to_write);

        if (written_bytes < bytes_to_write) {
            psleep(20);
        } else {
            samples_written += chunk_samples;
        }
    }
}

// Generates a pitch-bending, noise-injected waveform to simulate flatulence
static void write_fart_to_speaker(uint16_t duration_ms) {
    uint32_t total_samples = (16000 * duration_ms) / 1000;
    int16_t buffer[128];
    uint32_t samples_written = 0;
    uint32_t phase = 0;
    uint32_t current_freq = 65; // Start at a low, bassy frequency

    while (samples_written < total_samples) {
        uint32_t chunk_samples = total_samples - samples_written;
        if (chunk_samples > ARRAY_LENGTH(buffer)) chunk_samples = ARRAY_LENGTH(buffer);

        for (uint32_t i = 0; i < chunk_samples; i++) {
            // Drop the frequency down to ~20Hz over time to create a pitch bend
            if ((samples_written + i) % 300 == 0 && current_freq > 20) {
                current_freq--;
            }

            uint32_t half_period = 16000 / (current_freq * 2);
            if (half_period == 0) half_period = 1;

            // Add mathematical jitter to the phase to simulate flapping
            uint32_t jitter = rand() % (half_period / 2 + 1);

            if (((phase + jitter) / half_period) % 2 == 0) {
                // Add noise to the peak amplitude for a "wet" texture
                buffer[i] = 12000 - (rand() % 4000);
            } else {
                buffer[i] = -12000 + (rand() % 4000);
            }
            phase++;
        }

        uint32_t bytes_to_write = chunk_samples * sizeof(int16_t);
        uint32_t written_bytes = speaker_stream_write((const void*)buffer, bytes_to_write);

        if (written_bytes < bytes_to_write) {
            psleep(20); // Yield to OS while DAC drains
        } else {
            samples_written += chunk_samples;
        }
    }
}

static void write_silence_to_speaker(uint16_t duration_ms) {
    uint32_t total_samples = (16000 * duration_ms) / 1000;
    int16_t buffer[128] = {0};
    uint32_t samples_written = 0;

    while (samples_written < total_samples) {
        uint32_t chunk_samples = total_samples - samples_written;
        if (chunk_samples > ARRAY_LENGTH(buffer)) chunk_samples = ARRAY_LENGTH(buffer);

        uint32_t bytes_to_write = chunk_samples * sizeof(int16_t);
        uint32_t written_bytes = speaker_stream_write((const void*)buffer, bytes_to_write);

        if (written_bytes < bytes_to_write) {
            psleep(20);
        } else {
            samples_written += chunk_samples;
        }
    }
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
    struct tm *t = localtime(&now);

    if (s_config.mode == 0) {
        t->tm_hour = s_config.spec_hour;
        t->tm_min = s_config.spec_minute;
        t->tm_sec = 0;
    } else {
        int range = s_config.win_end - s_config.win_start;
        if (range <= 0) range = 1;
        t->tm_hour = s_config.win_start + (rand() % range);
        t->tm_min = rand() % 60;
        t->tm_sec = 0;
    }

    time_t target = mktime(t);
    if (target <= now) {
        target += SECONDS_IN_DAY;
    }

    wakeup_cancel_all();
    wakeup_schedule(target, WAKEUP_REASON, false);
}

static void play_alert(void) {
    bool do_vibe = (s_config.alert_style == 0 || s_config.alert_style == 2);
    bool do_sound = (s_config.alert_style == 1 || s_config.alert_style == 2);

    if (do_vibe) {
        uint32_t segments[] = { 200, 100, 200 };
        VibePattern pat = {
            .durations = segments,
            .num_segments = ARRAY_LENGTH(segments)
        };
        vibes_enqueue_custom_pattern(pat);
    }

    if (do_sound) {
        WatchInfoModel model = watch_info_get_model();
        if (model == WATCH_INFO_MODEL_COREDEVICES_PT2 || model == WATCH_INFO_MODEL_PEBBLE_TIME_2) {

            if (speaker_is_muted()) return;

            if (speaker_stream_open(SpeakerPcmFormat_16kHz_16bit, 100)) {

                // Pre-feed silence to wake the DAC without clipping
                write_silence_to_speaker(50);

                if (s_config.sound_tune == 2) {
                    // Custom Fart Sound Option
                    write_fart_to_speaker(500);
                } else if (s_config.sound_tune == 1) {
                    // "Ba-Dum-Tss" comedic rimshot (4x Faster)
                    write_tone_to_speaker(200, 25);
                    write_silence_to_speaker(25);
                    write_tone_to_speaker(150, 25);
                    write_silence_to_speaker(50);
                    write_noise_to_speaker(62);
                } else {
                    // Classic Beep Alert
                    write_tone_to_speaker(880, 150);
                    write_silence_to_speaker(150);
                    write_tone_to_speaker(1046, 200);
                }

                psleep(100);
                speaker_stop();
                speaker_stream_close();
            }
        }
    }
}

// --- Data & Rollover Logic ---

static void shuffle_jokes(void) {
    for (uint16_t i = 0; i < NUM_JOKES; i++) {
        s_joke_order[i] = (uint8_t)i;
    }

    for (uint16_t i = NUM_JOKES - 1; i > 0; i--) {
        uint16_t j = rand() % (i + 1);
        uint8_t temp = s_joke_order[i];
        s_joke_order[i] = s_joke_order[j];
        s_joke_order[j] = temp;
    }

    s_current_index = 0;
    persist_write_data(PERSIST_KEY_ORDER, s_joke_order, NUM_JOKES);
    persist_write_int(PERSIST_KEY_INDEX, s_current_index);
}

static void load_current_joke(void) {
    if (s_current_index >= NUM_JOKES) {
        shuffle_jokes();
    }

    uint8_t joke_id = s_joke_order[s_current_index];
    const char *joke_text = JOKES[joke_id];

    GFont font = get_font_for_preference(s_config.font_size);
    text_layer_set_font(s_joke_text_layer, font);
    text_layer_set_text(s_joke_text_layer, joke_text);

    GRect full_bounds = layer_get_bounds(window_get_root_layer(s_main_window));
    int16_t visible_height = full_bounds.size.h - FOOTER_HEIGHT;

    GSize content_size = graphics_text_layout_get_content_size(
        joke_text,
        font,
        GRect(0, 0, full_bounds.size.w, 4000),
                                                               GTextOverflowModeWordWrap,
                                                               GTextAlignmentLeft
    );

    content_size.h += 16;
    if (content_size.h < visible_height) {
        content_size.h = visible_height;
    }

    text_layer_set_size(s_joke_text_layer, GSize(full_bounds.size.w, content_size.h));
    scroll_layer_set_content_size(s_scroll_layer, GSize(full_bounds.size.w, content_size.h));
    scroll_layer_set_content_offset(s_scroll_layer, GPointZero, false);
}

static void next_joke(void) {
    s_current_index++;
    if (s_current_index >= NUM_JOKES) {
        shuffle_jokes();
    }
    persist_write_int(PERSIST_KEY_INDEX, s_current_index);
    load_current_joke();
}

// --- Scrolling Helper ---

static void adjust_scroll_offset(int16_t delta_y, bool animated) {
    GPoint offset = scroll_layer_get_content_offset(s_scroll_layer);
    GRect full_bounds = layer_get_bounds(window_get_root_layer(s_main_window));
    GSize content_size = scroll_layer_get_content_size(s_scroll_layer);

    int16_t visible_height = full_bounds.size.h - FOOTER_HEIGHT;
    int16_t min_y = -(content_size.h - visible_height);
    if (min_y > 0) min_y = 0;

    offset.y += delta_y;
    if (offset.y > 0) offset.y = 0;
    if (offset.y < min_y) offset.y = min_y;

    scroll_layer_set_content_offset(s_scroll_layer, offset, animated);
}

// --- Interaction Handlers ---

static void dismiss_app_handler(ClickRecognizerRef recognizer, void *context) {
    window_stack_pop_all(true);
}

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
    adjust_scroll_offset(SCROLL_STEP_PX, true);
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
    adjust_scroll_offset(-SCROLL_STEP_PX, true);
}

static void next_joke_handler(ClickRecognizerRef recognizer, void *context) {
    next_joke();
    schedule_next_joke();
}

static void click_config_provider(void *context) {
    window_single_click_subscribe(BUTTON_ID_BACK, dismiss_app_handler);
    window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
    window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
    window_single_click_subscribe(BUTTON_ID_DOWN, next_joke_handler);
}

static void tap_handler(AccelAxisType axis, int32_t direction) {
    window_stack_pop_all(true);
}

// Touch event handler for the 2026 Pebble Time 2 (Emery Platform)
static void touch_handler(const TouchEvent *event, void *context) {
    static int16_t s_last_touch_y = 0;

    if (event->type == TouchEvent_Touchdown) {
        s_last_touch_y = event->y;
    } else if (event->type == TouchEvent_PositionUpdate) {
        int16_t delta_y = event->y - s_last_touch_y;
        adjust_scroll_offset(delta_y, false);
        s_last_touch_y = event->y;
    }
}

static void wakeup_handler(WakeupId id, int32_t reason) {
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

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
    Tuple *mode_t = dict_find(iter, MESSAGE_KEY_ScheduleMode);
    if (mode_t) s_config.mode = get_int_from_tuple(mode_t);

    Tuple *spec_hr_t = dict_find(iter, MESSAGE_KEY_SpecificHour);
    if (spec_hr_t) s_config.spec_hour = get_int_from_tuple(spec_hr_t);

    Tuple *spec_min_t = dict_find(iter, MESSAGE_KEY_SpecificMinute);
    if (spec_min_t) s_config.spec_minute = get_int_from_tuple(spec_min_t);

    Tuple *win_st_t = dict_find(iter, MESSAGE_KEY_WindowStartHour);
    if (win_st_t) s_config.win_start = get_int_from_tuple(win_st_t);

    Tuple *win_end_t = dict_find(iter, MESSAGE_KEY_WindowEndHour);
    if (win_end_t) s_config.win_end = get_int_from_tuple(win_end_t);

    Tuple *alert_t = dict_find(iter, MESSAGE_KEY_AlertStyle);
    if (alert_t) s_config.alert_style = get_int_from_tuple(alert_t);

    Tuple *sound_t = dict_find(iter, MESSAGE_KEY_SoundTune);
    if (sound_t) s_config.sound_tune = get_int_from_tuple(sound_t);

    Tuple *font_size_t = dict_find(iter, MESSAGE_KEY_FontSize);
    if (font_size_t) {
        s_config.font_size = get_int_from_tuple(font_size_t);
        load_current_joke();
    }

    persist_write_data(PERSIST_KEY_CONFIG, &s_config, sizeof(AppConfig));
    schedule_next_joke();
}

// --- Window Lifecycle ---

static void main_window_load(Window *window) {
    GRect full_bounds = layer_get_bounds(window_get_root_layer(window));

    GRect scroll_bounds = GRect(0, 0, full_bounds.size.w, full_bounds.size.h - FOOTER_HEIGHT);
    s_scroll_layer = scroll_layer_create(scroll_bounds);
    scroll_layer_set_click_config_onto_window(s_scroll_layer, window);

    ScrollLayerCallbacks callbacks = {
        .click_config_provider = click_config_provider
    };
    scroll_layer_set_callbacks(s_scroll_layer, callbacks);

    s_joke_text_layer = text_layer_create(GRect(0, 0, scroll_bounds.size.w, scroll_bounds.size.h));
    text_layer_set_text_alignment(s_joke_text_layer, GTextAlignmentLeft);
    text_layer_set_overflow_mode(s_joke_text_layer, GTextOverflowModeWordWrap);

    scroll_layer_add_child(s_scroll_layer, text_layer_get_layer(s_joke_text_layer));
    layer_add_child(window_get_root_layer(window), scroll_layer_get_layer(s_scroll_layer));

    s_prompt_text_layer = text_layer_create(GRect(0, full_bounds.size.h - 22, full_bounds.size.w - 5, 22));
    text_layer_set_text(s_prompt_text_layer, "Next Joke ->");
    text_layer_set_text_alignment(s_prompt_text_layer, GTextAlignmentRight);
    text_layer_set_font(s_prompt_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
    layer_add_child(window_get_root_layer(window), text_layer_get_layer(s_prompt_text_layer));

    if (touch_service_is_enabled()) {
        touch_service_subscribe(touch_handler, NULL);
    }

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
}

static void main_window_unload(Window *window) {
    if (touch_service_is_enabled()) {
        touch_service_unsubscribe();
    }
    text_layer_destroy(s_joke_text_layer);
    text_layer_destroy(s_prompt_text_layer);
    scroll_layer_destroy(s_scroll_layer);
}

// --- App Lifecycle ---

static void init(void) {
    srand(time(NULL));

    if (persist_exists(PERSIST_KEY_CONFIG)) {
        persist_read_data(PERSIST_KEY_CONFIG, &s_config, sizeof(AppConfig));
    }

    s_joke_order = malloc(NUM_JOKES * sizeof(uint8_t));
    if (!s_joke_order) {
        APP_LOG(APP_LOG_LEVEL_ERROR, "Failed to allocate memory for joke order array");
        return;
    }

    bool needs_shuffle = true;
    if (persist_exists(PERSIST_KEY_ORDER) && persist_exists(PERSIST_KEY_INDEX)) {
        int saved_size = persist_get_size(PERSIST_KEY_ORDER);
        if (saved_size == NUM_JOKES) {
            persist_read_data(PERSIST_KEY_ORDER, s_joke_order, NUM_JOKES);
            s_current_index = persist_read_int(PERSIST_KEY_INDEX);
            needs_shuffle = false;
        }
    }
    if (needs_shuffle) {
        shuffle_jokes();
    }

    app_message_register_inbox_received(inbox_received_handler);
    app_message_open(256, 256);

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

    if (s_joke_order) {
        free(s_joke_order);
        s_joke_order = NULL;
    }
}

int main(void) {
    init();
    app_event_loop();
    deinit();
}
