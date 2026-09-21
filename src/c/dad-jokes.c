/* SPDX-License-Identifier: MIT */
#include <pebble.h>

#define PERSIST_KEY_SEED   1
#define PERSIST_KEY_INDEX  2
#define PERSIST_KEY_CONFIG 3
#define WAKEUP_REASON      1337
#define SECONDS_IN_DAY     86400
#define SCROLL_STEP_PX     40

static Window *s_main_window;
static ScrollLayer *s_scroll_layer;
static TextLayer *s_joke_text_layer;
static TextLayer *s_prompt_text_layer;

static Layer *s_up_arrow_layer;
static Layer *s_down_arrow_layer;

static AppTimer *s_timeout_timer = NULL;
static AppTimer *s_deferred_joke_timer = NULL;
static bool s_is_phone_ready = false;

static char s_current_joke_buffer[512];
static uint32_t *s_joke_offsets = NULL;
static uint16_t *s_joke_order = NULL;
static uint16_t s_num_builtin_jokes = 0;
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
    int32_t alert_volume;
    int32_t alert_style;
    int32_t sound_tune;
    int32_t font_size;
    int32_t joke_mode; // 0 = Built-In, 1 = Mixed, 2 = Custom Only
    int32_t custom_joke_count;
    int32_t dark_mode; // 0 = Light, 1 = Dark
    int32_t flick_to_dismiss; // 0 = Disabled, 1 = Enabled
} AppConfig;

static AppConfig s_config = {
    .mode = 0,
    .spec_hour = 12,
    .spec_minute = 0,
    .win_start = 9,
    .win_end = 17,
    .jokes_per_hour = 1,
    .timeout_sec = 30,
    .alert_volume = 100,
    .alert_style = 0,
    .sound_tune = 0,
    .font_size = 2,
    .joke_mode = 0,
    .custom_joke_count = 0,
    .dark_mode = 0,
    .flick_to_dismiss = 1 // Enabled by default
};

// Forward declaration
static void load_builtin_joke(void);
static void load_current_joke(void);
static void next_joke(void);
static void schedule_next_joke(void);
static void display_joke(const char *joke_text);

// --- Theme Application ---

static void update_ui_colors(void) {
    GColor bg_color = s_config.dark_mode ? GColorBlack : GColorWhite;
    GColor fg_color = s_config.dark_mode ? GColorWhite : GColorBlack;

    window_set_background_color(s_main_window, bg_color);

    text_layer_set_background_color(s_joke_text_layer, GColorClear);
    text_layer_set_background_color(s_prompt_text_layer, GColorClear);

    text_layer_set_text_color(s_joke_text_layer, fg_color);
    text_layer_set_text_color(s_prompt_text_layer, fg_color);

    scroll_layer_set_shadow_hidden(s_scroll_layer, s_config.dark_mode != 0);

    ContentIndicator *indicator = scroll_layer_get_content_indicator(s_scroll_layer);

    GColor indicator_bg;
    GColor indicator_fg;

    if (s_config.dark_mode) {
        indicator_bg = GColorBlack;
        indicator_fg = GColorWhite;
    } else {
        indicator_bg = GColorWhite;
        indicator_fg = GColorBlack;
    }

    ContentIndicatorConfig up_config = (ContentIndicatorConfig) {
        .layer = s_up_arrow_layer,
        .times_out = false,
        .alignment = PBL_IF_ROUND_ELSE(GAlignTop, GAlignCenter),
        .colors = {
            .foreground = indicator_fg,
            .background = indicator_bg
        }
    };
    content_indicator_configure_direction(indicator, ContentIndicatorDirectionUp, &up_config);

    ContentIndicatorConfig down_config = (ContentIndicatorConfig) {
        .layer = s_down_arrow_layer,
        .times_out = false,
        .alignment = PBL_IF_ROUND_ELSE(GAlignBottom, GAlignCenter),
        .colors = {
            .foreground = indicator_fg,
            .background = indicator_bg
        }
    };
    content_indicator_configure_direction(indicator, ContentIndicatorDirectionDown, &down_config);
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

static void write_fart_to_speaker(uint16_t duration_ms) {
    uint32_t total_samples = (16000 * duration_ms) / 1000;
    int16_t buffer[256];
    uint32_t samples_written = 0;

    while (samples_written < total_samples) {
        uint32_t chunk_samples = total_samples - samples_written;
        if (chunk_samples > ARRAY_LENGTH(buffer)) chunk_samples = ARRAY_LENGTH(buffer);

        for (uint32_t i = 0; i < chunk_samples; i++) {
            uint32_t abs_sample = samples_written + i;
            uint32_t freq = 45 - (abs_sample / 400);
            if (freq < 10) freq = 10;

            uint32_t half_period = 16000 / (freq * 2);
            if (half_period == 0) half_period = 1;
            uint32_t jitter = rand() % (half_period / 3 + 1);

            if (((abs_sample + jitter) / half_period) % 2 == 0) {
                buffer[i] = 14000 - (rand() % 6000);
            } else {
                buffer[i] = -14000 + (rand() % 6000);
            }
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

// --- Text Parsing Engine (One Joke Per Line Format) ---
// This parser correctly interprets jokes.txt files where each joke
// occupies a single line. Blank lines are NOT required.
// Internal line breaks for multi-line jokes are encoded as literal '\n' characters.

static void load_builtin_jokes_from_resource(void) {
    ResHandle handle = resource_get_handle(RESOURCE_ID_JOKES_TXT);
    size_t res_size = resource_size(handle);

    s_num_builtin_jokes = 0;
    uint8_t chunk[256];
    bool in_joke = false;

    for (uint32_t offset = 0; offset < res_size; offset += sizeof(chunk)) {
        size_t bytes_to_read = res_size - offset;
        if (bytes_to_read > sizeof(chunk)) bytes_to_read = sizeof(chunk);

        resource_load_byte_range(handle, offset, chunk, bytes_to_read);

        for (size_t i = 0; i < bytes_to_read; i++) {
            char c = chunk[i];
            if (c == '\r' || c == '\n') {
                in_joke = false;
            } else if (!in_joke) {
                s_num_builtin_jokes++;
                in_joke = true;
            }
        }
    }

    if (s_num_builtin_jokes == 0) return;

    s_joke_offsets = malloc(s_num_builtin_jokes * sizeof(uint32_t));
    if (!s_joke_offsets) {
        s_num_builtin_jokes = 0;
        return;
    }

    uint16_t current_idx = 0;
    in_joke = false;

    for (uint32_t offset = 0; offset < res_size; offset += sizeof(chunk)) {
        size_t bytes_to_read = res_size - offset;
        if (bytes_to_read > sizeof(chunk)) bytes_to_read = sizeof(chunk);

        resource_load_byte_range(handle, offset, chunk, bytes_to_read);

        for (size_t i = 0; i < bytes_to_read; i++) {
            char c = chunk[i];
            if (c == '\r' || c == '\n') {
                in_joke = false;
            } else if (!in_joke) {
                if (current_idx < s_num_builtin_jokes) {
                    s_joke_offsets[current_idx++] = offset + i;
                }
                in_joke = true;
            }
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
    time_t midnight = time_start_of_today();
    time_t target;

    if (s_config.mode == 0) {
        target = midnight + (s_config.spec_hour * 3600) + (s_config.spec_minute * 60);

        if (target <= now) {
            target += SECONDS_IN_DAY;
        }
    } else {
        int32_t window_start_sec = s_config.win_start * 3600;
        int32_t window_end_sec = s_config.win_end * 3600;
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
    if (quiet_time_is_active()) {
        return;
    }

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

            uint8_t current_volume = s_config.alert_volume;
            if (current_volume > 100) current_volume = 100;

            if (speaker_stream_open(SpeakerPcmFormat_16kHz_16bit, current_volume)) {

                write_silence_to_speaker(250);

                if (s_config.sound_tune == 3) {
                    write_tone_to_speaker(523, 250);
                    write_silence_to_speaker(30);
                    write_tone_to_speaker(494, 250);
                    write_silence_to_speaker(30);
                    write_tone_to_speaker(466, 125);
                    write_silence_to_speaker(30);
                    write_tone_to_speaker(494, 125);
                    write_silence_to_speaker(30);
                    write_tone_to_speaker(466, 125);
                    write_silence_to_speaker(30);
                    write_tone_to_speaker(440, 125);
                } else if (s_config.sound_tune == 2) {
                    write_fart_to_speaker(500);
                } else if (s_config.sound_tune == 1) {
                    write_tone_to_speaker(392, 60);
                    write_silence_to_speaker(30);
                    write_tone_to_speaker(440, 125);
                    write_silence_to_speaker(30);
                    write_mixed_cymbal_to_speaker(523, 125);
                } else {
                    write_tone_to_speaker(4096, 125);
                    write_silence_to_speaker(125);
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
    uint16_t total_jokes = 0;
    if (s_config.joke_mode == 0) {
        total_jokes = s_num_builtin_jokes;
    } else if (s_config.joke_mode == 1) {
        total_jokes = s_num_builtin_jokes + s_config.custom_joke_count;
    } else if (s_config.joke_mode == 2) {
        total_jokes = s_config.custom_joke_count;
    }

    if (total_jokes == 0) return;

    if (s_joke_order) free(s_joke_order);
    s_joke_order = malloc(total_jokes * sizeof(uint16_t));
    if (!s_joke_order) return;

    srand(s_shuffle_seed);
    for (uint16_t i = 0; i < total_jokes; i++) {
        s_joke_order[i] = i;
    }

    for (uint16_t i = total_jokes - 1; i > 0; i--) {
        uint16_t j = rand() % (i + 1);
        uint16_t temp = s_joke_order[i];
        s_joke_order[i] = s_joke_order[j];
        s_joke_order[j] = temp;
    }
}

static void display_joke(const char *joke_text) {
    GFont font = get_font_for_preference(s_config.font_size);
    text_layer_set_font(s_joke_text_layer, font);

    text_layer_set_text(s_joke_text_layer, joke_text);

    GRect full_bounds = layer_get_bounds(window_get_root_layer(s_main_window));
    int16_t text_width = PBL_IF_ROUND_ELSE(full_bounds.size.w, full_bounds.size.w - 20);
    int16_t visible_height = full_bounds.size.h;

    text_layer_set_size(s_joke_text_layer, GSize(text_width, 4000));
    GSize content_size = text_layer_get_content_size(s_joke_text_layer);

    int16_t top_margin = PBL_IF_ROUND_ELSE(18, 0);
    int16_t bottom_margin = 40;

    text_layer_set_size(s_joke_text_layer, GSize(text_width, content_size.h + bottom_margin));

    int16_t total_scroll_height = top_margin + content_size.h + bottom_margin;
    if (total_scroll_height < visible_height) {
        total_scroll_height = visible_height;
    }

    scroll_layer_set_content_size(s_scroll_layer, GSize(text_width, total_scroll_height));
    scroll_layer_set_content_offset(s_scroll_layer, GPointZero, false);
}

static void load_builtin_joke(void) {
    if (s_num_builtin_jokes == 0 || !s_joke_offsets) return;

    uint16_t joke_id = s_joke_order[s_current_index];

    joke_id = joke_id % s_num_builtin_jokes;

    uint32_t start_offset = s_joke_offsets[joke_id];
    ResHandle handle = resource_get_handle(RESOURCE_ID_JOKES_TXT);
    size_t res_size = resource_size(handle);

    size_t max_allowed = sizeof(s_current_joke_buffer) - 1;
    if (PBL_IF_ROUND_ELSE(true, false)) {
        max_allowed -= 2;
    }

    size_t read_size = res_size - start_offset;
    if (read_size > max_allowed) {
        read_size = max_allowed;
    }

    resource_load_byte_range(handle, start_offset, (uint8_t*)s_current_joke_buffer, read_size);

    for (size_t i = 0; i < read_size; i++) {
        if (s_current_joke_buffer[i] == '\r' || s_current_joke_buffer[i] == '\n') {
            s_current_joke_buffer[i] = '\0';
            break;
        }
    }
    s_current_joke_buffer[read_size] = '\0';

    char *read_ptr = s_current_joke_buffer;
    char *write_ptr = s_current_joke_buffer;
    while (*read_ptr) {
        if (*read_ptr == '\\' && *(read_ptr + 1) == 'n') {
            *write_ptr++ = '\n';
            read_ptr += 2;
        } else {
            *write_ptr++ = *read_ptr++;
        }
    }

    if (PBL_IF_ROUND_ELSE(true, false)) {
        *write_ptr++ = '\n';
        *write_ptr++ = '\n';
    }
    *write_ptr = '\0';

    display_joke(s_current_joke_buffer);
}

static void deferred_joke_request_cb(void *context) {
    s_deferred_joke_timer = NULL;
    load_current_joke();
    schedule_next_joke();
}

static void load_current_joke(void) {
    if (!s_is_phone_ready && s_config.joke_mode != 0) {
        if (s_config.joke_mode == 2) {
            display_joke("Loading custom jokes...\n\nPlease wait.");
        } else {
            load_builtin_joke();
        }
        return;
    }

    uint16_t total_jokes = 0;
    if (s_config.joke_mode == 0) {
        total_jokes = s_num_builtin_jokes;
    } else if (s_config.joke_mode == 1) {
        total_jokes = s_num_builtin_jokes + s_config.custom_joke_count;
    } else if (s_config.joke_mode == 2) {
        total_jokes = s_config.custom_joke_count;
    }

    if (s_config.joke_mode == 2 && total_jokes == 0) {
        display_joke("Your custom joke list is empty.\n\nPlease add jokes in the Pebble app settings.");
        return;
    }

    if (total_jokes == 0 || !s_joke_order) {
        load_builtin_joke();
        return;
    }

    if (s_current_index >= total_jokes) {
        s_current_index = 0;
    }

    uint16_t joke_id = s_joke_order[s_current_index];
    bool fetch_from_phone = false;
    uint32_t phone_idx = 0;

    if (s_config.joke_mode == 2) {
        fetch_from_phone = true;
        phone_idx = joke_id;
    } else if (s_config.joke_mode == 1 && joke_id >= s_num_builtin_jokes) {
        fetch_from_phone = true;
        phone_idx = joke_id - s_num_builtin_jokes;
    }

    if (fetch_from_phone) {
        if (!connection_service_peek_pebble_app_connection()) {
            if (s_config.joke_mode == 2) {
                display_joke("Phone disconnected.\n\nReconnect to load custom jokes.");
            } else {
                load_builtin_joke();
            }
            return;
        }

        if (!s_is_phone_ready) {
            load_builtin_joke();
            return;
        }

        DictionaryIterator *iter;
        AppMessageResult outbox_res = app_message_outbox_begin(&iter);
        if (outbox_res == APP_MSG_OK) {
            dict_write_uint32(iter, MESSAGE_KEY_RequestJokeIdx, phone_idx);
            if (app_message_outbox_send() == APP_MSG_OK) {
                return;
            }
        } else if (outbox_res == APP_MSG_BUSY) {
            if (s_deferred_joke_timer) app_timer_cancel(s_deferred_joke_timer);
            s_deferred_joke_timer = app_timer_register(150, deferred_joke_request_cb, NULL);
            return;
        }

        if (s_config.joke_mode == 2) {
            display_joke("Failed to contact phone.\n\nRetrying...");
        } else {
            load_builtin_joke();
        }
        return;
    }

    load_builtin_joke();
}

static void next_joke(void) {
    uint16_t total_jokes = 0;
    if (s_config.joke_mode == 0) {
        total_jokes = s_num_builtin_jokes;
    } else if (s_config.joke_mode == 1) {
        total_jokes = s_num_builtin_jokes + s_config.custom_joke_count;
    } else if (s_config.joke_mode == 2) {
        total_jokes = s_config.custom_joke_count;
    }

    if (total_jokes == 0) {
        if (s_config.joke_mode == 2) load_current_joke();
        return;
    }

    s_current_index++;
    if (s_current_index >= total_jokes) {
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

static void click_config_provider(void *context) {
    window_single_click_subscribe(BUTTON_ID_BACK, button_dismiss_handler);
    window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
    window_single_click_subscribe(BUTTON_ID_SELECT, next_joke_handler);
    window_single_click_subscribe(BUTTON_ID_DOWN, down_click_handler);
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
    if (s_config.joke_mode == 2) {
        display_joke("Failed to fetch joke from phone.\n\nCheck Bluetooth connection.");
    } else {
        load_builtin_joke();
    }
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
    Tuple *deliver_t = dict_find(iter, MESSAGE_KEY_DeliverJokeText);
    if (deliver_t) {
        size_t max_len = sizeof(s_current_joke_buffer) - 1;

        if (PBL_IF_ROUND_ELSE(true, false)) {
            max_len -= 2;
        }

        strncpy(s_current_joke_buffer, deliver_t->value->cstring, max_len);
        s_current_joke_buffer[max_len] = '\0';

        char *read_ptr = s_current_joke_buffer;
        char *write_ptr = s_current_joke_buffer;
        while (*read_ptr) {
            if (*read_ptr == '\\' && *(read_ptr + 1) == 'n') {
                *write_ptr++ = '\n';
                read_ptr += 2;
            } else {
                *write_ptr++ = *read_ptr++;
            }
        }

        if (PBL_IF_ROUND_ELSE(true, false)) {
            *write_ptr++ = '\n';
            *write_ptr++ = '\n';
        }
        *write_ptr = '\0';

        display_joke(s_current_joke_buffer);
        return;
    }

    bool should_reshuffle = false;
    bool should_update_colors = false;

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

    Tuple *joke_mode_t = dict_find(iter, MESSAGE_KEY_CustomJokeMode);
    if (joke_mode_t) {
        s_is_phone_ready = true;
        int32_t new_mode = get_int_from_tuple(joke_mode_t);
        if (s_config.joke_mode != new_mode) {
            s_config.joke_mode = new_mode;
            should_reshuffle = true;
        }
    }

    Tuple *count_t = dict_find(iter, MESSAGE_KEY_CustomJokeCount);
    if (count_t) {
        int32_t new_count = get_int_from_tuple(count_t);
        if (s_config.custom_joke_count != new_count) {
            s_config.custom_joke_count = new_count;
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

    int16_t text_width = PBL_IF_ROUND_ELSE(full_bounds.size.w, full_bounds.size.w - 20);
    GRect scroll_bounds = GRect(0, 0, text_width, full_bounds.size.h);
    s_scroll_layer = scroll_layer_create(scroll_bounds);
    scroll_layer_set_click_config_onto_window(s_scroll_layer, window);

    ScrollLayerCallbacks callbacks = {
        .click_config_provider = click_config_provider
    };
    scroll_layer_set_callbacks(s_scroll_layer, callbacks);

    int16_t top_margin = PBL_IF_ROUND_ELSE(18, 0);
    s_joke_text_layer = text_layer_create(GRect(0, top_margin, scroll_bounds.size.w, scroll_bounds.size.h));

    text_layer_set_text_alignment(s_joke_text_layer, PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft));
    text_layer_set_overflow_mode(s_joke_text_layer, GTextOverflowModeWordWrap);

    scroll_layer_add_child(s_scroll_layer, text_layer_get_layer(s_joke_text_layer));
    layer_add_child(window_get_root_layer(window), scroll_layer_get_layer(s_scroll_layer));

    if (PBL_IF_ROUND_ELSE(true, false)) {
        text_layer_enable_screen_text_flow_and_paging(s_joke_text_layer, 12);
    }

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
    text_layer_destroy(s_joke_text_layer);
    text_layer_destroy(s_prompt_text_layer);
    layer_destroy(s_up_arrow_layer);
    layer_destroy(s_down_arrow_layer);
    scroll_layer_destroy(s_scroll_layer);
}

// --- App Lifecycle ---

static void init(void) {
    srand(time(NULL));

    if (persist_exists(PERSIST_KEY_CONFIG)) {
        if (persist_get_size(PERSIST_KEY_CONFIG) == sizeof(AppConfig)) {
            persist_read_data(PERSIST_KEY_CONFIG, &s_config, sizeof(AppConfig));
            if (s_config.timeout_sec > 0 && s_config.timeout_sec < 15) {
                s_config.timeout_sec = 15;
            }
        }
    }

    load_builtin_jokes_from_resource();

    uint16_t total_jokes = 0;
    if (s_config.joke_mode == 0) {
        total_jokes = s_num_builtin_jokes;
    } else if (s_config.joke_mode == 1) {
        total_jokes = s_num_builtin_jokes + s_config.custom_joke_count;
    } else if (s_config.joke_mode == 2) {
        total_jokes = s_config.custom_joke_count;
    }

    if (total_jokes > 0) {
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

    app_message_open(1024, 512);

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

    if (s_joke_offsets) free(s_joke_offsets);
    if (s_joke_order) free(s_joke_order);
}

int main(void) {
    init();
    app_event_loop();
    deinit();
}
