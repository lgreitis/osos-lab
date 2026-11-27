/* SPDX-License-Identifier: MIT */
/* Adapted from RepriseOS game-sdk/include/game_api.h (8fc840d) by lgreitis. */

#ifndef EAPP_H
#define EAPP_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

/* The SDK version. The minor goes up with every change to what an app builds
 * against; the major when a call or a layout changes meaning. */
#define EAPP_SDK_MAJOR 2
#define EAPP_SDK_MINOR 25

#ifdef __cplusplus
extern "C" {
#endif

/* The buttons, and the wheel's touch surface, which presses when a finger
 * lands and releases when it lifts. */
enum eapp_button {
    EAPP_BUTTON_MENU = 1,
    EAPP_BUTTON_CENTER = 2,
    EAPP_BUTTON_NEXT = 3,
    EAPP_BUTTON_PREVIOUS = 4,
    EAPP_BUTTON_PLAY = 5,
    EAPP_BUTTON_WHEEL = 6,
};

/* The bit of a button in a held mask. */
#define EAPP_HELD(button) (1u << (button))

enum eapp_input_type {
    EAPP_INPUT_RELEASE = 1,
    EAPP_INPUT_PRESS = 2,
};

/* The screen, and the framebuffer a frame draws into. */
#define EAPP_SCREEN_WIDTH 320
#define EAPP_SCREEN_HEIGHT 240

/* An RGB565 colour from 8-bit components. */
#define EAPP_RGB565(r, g, b) \
    ((uint16_t)((((r) & 0xf8) << 8) | (((g) & 0xfc) << 3) | (((b) & 0xff) >> 3)))

struct eapp_framebuffer {
    uint16_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride_pixels;
};

struct eapp_input_event {
    uint8_t button;
    uint8_t type;
    uint32_t timestamp_ms; /* When the button moved, on the eapp_ticks_ms clock,
                              within a millisecond. */
};

struct eapp_wheel {
    int32_t delta;    /* Clockwise in 96ths of a turn since the last frame. */
    uint8_t position; /* right = 0, up = 64, left = 128, down = 192. */
    uint8_t touched;
};

/* What a frame call is handed: RGB565 pixels, borrowed for the call and
 * presented on return (NULL pixels when the host's surface is not the
 * expected one), the time, the wheel since the last frame and the buttons
 * down right now, as EAPP_HELD bits, after the frame's input events. */
struct eapp_frame {
    struct eapp_framebuffer framebuffer;
    uint32_t ticks_ms; /* The eapp_ticks_ms clock at this frame. */
    uint32_t delta_ms; /* Since the previous frame; 0 at the first. */
    struct eapp_wheel wheel;
    uint8_t held;
};

/* The app's callbacks. init runs before the host has set up its OpenGL ES
 * context; start runs once, before the first frame's input and drawing, with
 * the context current, so one-time GL setup goes there (gl* calls in init do
 * nothing). input gets the host's button events, then a wheel touch or lift
 * as EAPP_BUTTON_WHEEL. Play, Next and Previous releases go to the iPod's
 * music control after the input callback unless the app handles them
 * itself. */
struct eapp {
    void (*init)(void);
    void (*start)(void);
    void (*input)(const struct eapp_input_event *event);
    void (*frame)(const struct eapp_frame *frame);
    void (*shutdown)(void);
    uint8_t handles_music_buttons;
};

extern const struct eapp eapp_application;

/* Ends the app after the current callback returns. */
void eapp_request_exit(void);

/* Blocks of the eApp heap; the host frees them all at shutdown. */
void *eapp_alloc(size_t size);
void *eapp_realloc(void *pointer, size_t size);
void eapp_free(void *pointer);
/* Milliseconds since the app's init. */
uint32_t eapp_ticks_ms(void);
/* Blocks the app's task for at least that long; the host's tasks keep
 * running. */
void eapp_sleep_ms(uint32_t milliseconds);

/* The iPod's clock, local time; a fixed default date while the user never
 * set the clock. */
struct eapp_date_time {
    uint16_t year; /* The full year. */
    uint8_t month; /* 1..12 */
    uint8_t day;   /* 1..31 */
    uint8_t hour;  /* 0..23 */
    uint8_t minute;
    uint8_t second;
};

void eapp_date_time(struct eapp_date_time *out);
/* 1 when Settings > Date & Time shows the 24-hour clock, else 0. */
int eapp_clock_24h(void);

#define EAPP_TIME_TEXT_SIZE 9

/* "9:05 PM" or "21:05" after the user's clock setting, into `out` of
 * EAPP_TIME_TEXT_SIZE bytes; returns the end of the text, NULL for no
 * buffer, and an empty text for no time. */
char *eapp_format_time(char *out, const struct eapp_date_time *time);

/* The backlight, 0..100 of the brightness scale. A level stays after the
 * app ends: the host makes it the user's Brightness setting, and the
 * host's level query reports only what the app set, never the user's
 * value, so set it only when the user asked for it. Dimmed
 * caps the level at 20 and is undone when the app ends. The backlight
 * timer of Settings keeps running, so the screen still goes dark without
 * input. */
void eapp_set_backlight_level(uint32_t percent);
void eapp_set_backlight_dimmed(int on);

/* 1 while a computer or a charger powers the iPod over the cable, 0 on
 * battery; USB power the iPod's charger detection cannot classify does
 * not count, which is what the emulator's USB mode supplies. */
int eapp_external_power(void);

/* The battery, 0 to 100 in the host's 22 steps; -1 while the host has no
 * reading. The host lets the level fall one step per query, so the runtime
 * asks it at most once a second. */
int eapp_battery_level(void);

/* The iPod the host runs on, as the games platform Apple's games are built
 * for; the emulator reports the classic. */
enum eapp_platform {
    EAPP_PLATFORM_UNKNOWN = 0,
    EAPP_PLATFORM_IPOD_VIDEO = 1,
    EAPP_PLATFORM_NANO_3G = 2,
    EAPP_PLATFORM_CLASSIC = 3,
};

enum eapp_platform eapp_platform(void);

/* The language of Settings as a locale code, "en_US", "de_DE", ...; NULL
 * for one the runtime does not know. */
const char *eapp_language(void);

/* The package's name as Extras lists it, in the current language when the
 * manifest has it, into `out`; returns its length, or -1 when it does not
 * fit in `capacity` with its terminator. */
int eapp_name(char *out, size_t capacity);

/* One line to the host's debug console: the emulator's qemu.log; an iPod
 * without a debugger shows nothing. eapp_logf formats first, into
 * EAPP_LOG_SIZE bytes. */
void eapp_log(const char *text);
void eapp_logf(const char *format, ...);
#define EAPP_LOG_SIZE 128

/* Text from a format, for an app without a C library: %d %i %u %x %X %c %s
 * and %%, with the - and 0 flags, a width, a precision for %s, and l
 * accepted and ignored. Writes at most `capacity` bytes, terminated and cut
 * before a UTF-8 character that does not fit, and returns the whole text's
 * length, so a result of `capacity` or more means it was cut. */
int eapp_format(char *out, size_t capacity, const char *format, ...);
int eapp_format_list(char *out, size_t capacity, const char *format, va_list args);

/* xorshift32, seeded from the clock at the app's init.
 * eapp_random_seed gives a fixed stream for a seed; seed 0 is the fixed
 * default stream. eapp_random_below is 0..count - 1 with no bias, 0 for a
 * count below 2. */
void eapp_random_seed(uint32_t seed);
uint32_t eapp_random(void);
uint32_t eapp_random_below(uint32_t count);

enum eapp_direction {
    EAPP_DIRECTION_UP = 1,
    EAPP_DIRECTION_RIGHT = 2,
    EAPP_DIRECTION_DOWN = 4,
    EAPP_DIRECTION_LEFT = 8,
};

#define EAPP_WHEEL_TOP_POSITION 64

/* Eight touch sectors; a diagonal combines two direction bits; zero when untouched.
 * top_position is the wheel position read as Up, EAPP_WHEEL_TOP_POSITION. */
uint8_t eapp_wheel_directions(const struct eapp_wheel *wheel, uint8_t top_position);

/* 1 while the Hold switch is on, else 0. Reads the switch over the power
 * controller's bus each call, so ask when it matters, not every frame. */
int eapp_hold_locked(void);

/* Folders of the package, "" for its root, with subfolders when recursive is
 * nonzero. Four may be open at once. Read returns 1 with an entry, 0 at the end
 * or for a missing folder, -1 for a bad argument or a name that does not fit.
 * Names are relative to the folder opened. Shutdown closes what is left open. */
struct eapp_directory;

struct eapp_directory_entry {
    char name[256];
    uint8_t is_directory;
};
struct eapp_directory *eapp_directory_open(const char *path, int recursive);
int eapp_directory_read(struct eapp_directory *directory,
                        struct eapp_directory_entry *entry);
void eapp_directory_close(struct eapp_directory *directory);

/* Files of the package, read only, by path inside it; ".." is refused. Twelve
 * may be open at once. Open returns NULL on failure, read the bytes read or -1,
 * seek 0 or -1, size and position 0 for a closed or NULL file. Shutdown
 * closes what is left open. */
struct eapp_file;
struct eapp_file *eapp_file_open(const char *path);
int eapp_file_read(struct eapp_file *file, void *buffer, size_t size);
int eapp_file_seek(struct eapp_file *file, uint32_t offset);
uint32_t eapp_file_size(const struct eapp_file *file);
uint32_t eapp_file_position(const struct eapp_file *file);
void eapp_file_close(struct eapp_file *file);

/* Files of the package's data folder, /Reprise/Data/Games/gamedata_RW/<id>/ for
 * a game and /Reprise/Data/Apps/appdata_RW/<id>/ for an app under RepriseOS, by
 * path inside it with the package's path rules. Writing creates the folder and
 * replaces the file; the handle takes write, seek and close. The new contents
 * go to `<name>.tmp` beside the file and take its name when the handle is
 * closed, so a write that is cut short or refused leaves the old file in
 * place; the host has no atomic rename, so a rename it refuses on close loses
 * the file, and shows in eapp_file_error. Names are shorter than 256
 * bytes. Delete returns 0, -1 on failure. */
enum eapp_file_mode { EAPP_FILE_READ = 1, EAPP_FILE_WRITE = 2 };
struct eapp_file *eapp_data_open(const char *name, enum eapp_file_mode mode);
int eapp_file_write(struct eapp_file *file, const void *data, size_t size);
int eapp_data_delete(const char *name);

/* Whole files, with no handle to close. A read copies at most `capacity`
 * bytes from the start of the file into `buffer` and returns the file's
 * whole size, so a result above `capacity` means the buffer holds only the
 * beginning; -1 when the file cannot be read.
 * eapp_file_read_all reads a file of the package, eapp_data_read one of the
 * data folder. eapp_data_write replaces a data file with `size` bytes,
 * creating it and its folders, through `<name>.tmp` as above: 0, or -1 on
 * failure. */
int eapp_file_read_all(const char *path, void *buffer, size_t capacity);
int eapp_data_read(const char *name, void *buffer, size_t capacity);
int eapp_data_write(const char *name, const void *data, size_t size);

/* The host's status of the last file, data, folder or save call that failed,
 * a FileStatus_* or GameSaveStatus_* value of osos-2.0.5.h; 0 while nothing
 * failed. */
uint32_t eapp_file_error(void);

/* Modules: code outside the 1 MiB image, a <name>.emod of the package that
 * the build makes from the Makefile's MODULES. A module begins with a table
 * its sources define with EAPP_MODULE, a struct of the app's own type that a
 * header shared by both sides declares. eapp_module_load reads the module
 * into one heap block and returns that table, or NULL with a log line when
 * the file is missing, is not a module, was built for another image or does
 * not fit the heap. Module code calls the image's functions and uses its
 * globals directly; its own data and bss start fresh at every load.
 * eapp_module_free frees the block; nothing of the module may be used after
 * it. */
const void *eapp_module_load(const char *path);
void eapp_module_free(const void *module);
#define EAPP_MODULE(type) \
    const type eapp_module __attribute__((section(".eapp_module"), used))

/* Apple's save store, for an app with little to keep: a file of the data
 * folder whose contents the host holds in memory and writes at the next
 * system save or app launch, not when the app exits. Four slots exist on the
 * device and the slots hold 100 KiB together. Open loads the file; write
 * replaces the contents; read copies from `offset` and returns the bytes
 * copied, 0 past the end, -1 on error; size is 0 for a closed or NULL save.
 * Shutdown closes what is left open. */
struct eapp_save;
struct eapp_save *eapp_save_open(const char *name);
int eapp_save_read(struct eapp_save *save, void *buffer, size_t size, uint32_t offset);
int eapp_save_write(struct eapp_save *save, const void *data, size_t size);
uint32_t eapp_save_size(const struct eapp_save *save);
void eapp_save_close(struct eapp_save *save);

/* A struct kept in the save store with a header the SDK checks: `magic`
 * names the record, `version` its layout, and a checksum guards the bytes.
 * read fills `data` and returns 1 for a record of that magic, version and
 * size whose checksum holds, 0 for none or one that differs, so a changed
 * layout or a damaged file reads as no record; `data` may hold the rejected
 * bytes then. write replaces the record: 0, or -1 on failure. */
int eapp_save_record_read(const char *name, uint32_t magic, uint32_t version, void *data,
                          size_t size);
int eapp_save_record_write(const char *name, uint32_t magic, uint32_t version,
                           const void *data, size_t size);

/* Sound effects: PCM samples the host mixes over the music on one of eight
 * voices; the oldest voice is taken when all eight play. A sample is 8-bit
 * signed or 16-bit little-endian PCM, 1 or 2 channels, 8000..48000 Hz. The
 * host reads the sample memory while the sound plays: a buffer given to
 * create stays the app's and must outlive the sound, and any sound that
 * names it as next; load keeps its own copy. Thirty-two sounds may exist at
 * once. Free stops the sound unless another names it as next. Shutdown
 * stops and frees what is left. */
struct eapp_sound;

#define EAPP_SOUND_VOLUME_MAX 0x7fff
#define EAPP_SOUND_PAN_MAX 0x7fff /* -max is left, max right. */
#define EAPP_SOUND_SPEED_MIN 500  /* Per mille. */
#define EAPP_SOUND_SPEED_MAX 2000
#define EAPP_SOUND_RATE_MIN 8000 /* Hz. */
#define EAPP_SOUND_RATE_MAX 48000

/* A PCM WAV of the package (format 1, 8 or 16 bits, 1 or 2 channels,
 * 8000..48000 Hz); NULL when missing, not plain PCM or out of range. */
struct eapp_sound *eapp_sound_load(const char *path);
/* `bytes` of PCM at `pcm`, whole frames; NULL for a format the host refuses
 * or when no sound is free. */
struct eapp_sound *eapp_sound_create(const void *pcm, size_t bytes, uint32_t rate,
                                     uint32_t channels, uint32_t bits);
void eapp_sound_free(struct eapp_sound *sound);

/* Play starts the sound from the beginning, stopping a run still playing,
 * with the properties set so far. */
void eapp_sound_play(struct eapp_sound *sound);
void eapp_sound_stop(struct eapp_sound *sound);
void eapp_sound_pause(struct eapp_sound *sound);
void eapp_sound_resume(struct eapp_sound *sound);
/* 1 while a voice renders the sound; a paused sound is not playing. */
int eapp_sound_playing(const struct eapp_sound *sound);
/* Frames played of the current pass: 0 before the first play and after a
 * natural end; stale after a stop. */
uint32_t eapp_sound_position(const struct eapp_sound *sound);

/* The host's defaults: volume EAPP_SOUND_VOLUME_MAX, pan 0, speed 1000,
 * play count 1, no lead-in, tail or next, music duck off. Ranges are the
 * host's and are not checked. */
void eapp_sound_set_volume(struct eapp_sound *sound, uint32_t volume);
void eapp_sound_set_pan(struct eapp_sound *sound, int32_t pan);
void eapp_sound_set_speed(struct eapp_sound *sound, uint32_t per_mille);
/* 0 loops until stop, n plays n times. */
void eapp_sound_set_play_count(struct eapp_sound *sound, uint32_t count);
/* Silence before the sound (added to the music duck's milliseconds) and
 * after it, before `next`. */
void eapp_sound_set_lead_in_ms(struct eapp_sound *sound, uint32_t milliseconds);
void eapp_sound_set_tail_ms(struct eapp_sound *sound, uint32_t milliseconds);
/* `next` plays after the sound and its tail. The host cannot unchain, so a
 * next that is freed stays, and plays, until the last sound naming it is
 * freed. */
void eapp_sound_set_next(struct eapp_sound *sound, struct eapp_sound *next);
/* The music fades to `level` (0..EAPP_SOUND_VOLUME_MAX) over
 * `milliseconds` of silence before the sound and comes back after it;
 * 0 ms leaves the music alone. */
void eapp_sound_set_music_duck(struct eapp_sound *sound, uint32_t level,
                               uint32_t milliseconds);

/* Music: the iPod's player, through the host. A command is queued for the
 * host's UI task: state, track_count and the settings read back at once,
 * position, track_index and scanning report the old values until the UI
 * task has run. The volume takes effect at
 * once. Transport commands and the repeat, shuffle, EQ and speed setters do
 * nothing while no playlist is loaded. The host restores repeat, shuffle,
 * speed, gain and an EQ set until exit when the app ends; the volume and
 * set_eq stay. */
enum eapp_music_state {
    EAPP_MUSIC_STOPPED = 0,
    EAPP_MUSIC_PLAYING = 1,
    EAPP_MUSIC_PAUSED = 2,
};

enum eapp_music_state eapp_music_state(void);
/* 1 while a list is loaded, also when stopped. */
int eapp_music_has_playlist(void);
void eapp_music_play_pause(void);
/* Stop ends playback and drops the playlist: has_playlist is 0 afterwards
 * and play_pause does nothing until the user starts music again. The host
 * reads repeat and shuffle as off after a stop until they are set again. */
void eapp_music_stop(void);
void eapp_music_next(void);
void eapp_music_previous(void);
/* Starts track `index` of the playlist, counted from 0; an index past the
 * end is dropped. */
void eapp_music_play_track(uint32_t index);
/* Into the track; past its end the player moves on. */
void eapp_music_seek(uint32_t milliseconds);
/* The press-and-hold fast forward and rewind; scan_stop resumes. */
void eapp_music_scan_forward(void);
void eapp_music_scan_backward(void);
void eapp_music_scan_stop(void);
/* 1 forward, -1 backward, 0 not scanning. */
int eapp_music_scanning(void);
uint32_t eapp_music_position(void); /* Milliseconds into the track. */
uint32_t eapp_music_duration(void); /* The position plus what is left. */
uint32_t eapp_music_track_index(void); /* From 0. */
uint32_t eapp_music_track_count(void);

enum eapp_music_repeat {
    EAPP_MUSIC_REPEAT_OFF = 0,
    EAPP_MUSIC_REPEAT_ONE = 1,
    EAPP_MUSIC_REPEAT_ALL = 2,
};

enum eapp_music_shuffle {
    EAPP_MUSIC_SHUFFLE_OFF = 0,
    EAPP_MUSIC_SHUFFLE_SONGS = 1,
    EAPP_MUSIC_SHUFFLE_ALBUMS = 2,
};

enum eapp_music_repeat eapp_music_repeat_mode(void);
void eapp_music_set_repeat_mode(enum eapp_music_repeat mode);
enum eapp_music_shuffle eapp_music_shuffle_mode(void);
void eapp_music_set_shuffle_mode(enum eapp_music_shuffle mode);

#define EAPP_MUSIC_VOLUME_MAX 255

/* The volume on the iPod's scale, up to the user's volume limit; set_volume
 * clamps to the limit and is not restored at exit. */
uint32_t eapp_music_volume(void);
uint32_t eapp_music_volume_limit(void);
void eapp_music_set_volume(uint32_t volume);
/* The index into the EQ list of Settings. set_eq_until_exit is undone
 * when the app ends; set_eq stays on the player, while the Settings list
 * keeps showing the user's own choice. */
uint32_t eapp_music_eq(void);
void eapp_music_set_eq_until_exit(uint32_t index);
void eapp_music_set_eq(uint32_t index);

#define EAPP_MUSIC_SPEED_MIN 500 /* Per mille; the audiobook speed. */
#define EAPP_MUSIC_SPEED_MAX 2000
#define EAPP_MUSIC_GAIN_MAX 32767 /* The host's unit. */

uint32_t eapp_music_speed(void);
/* Outside the range the call is ignored. */
void eapp_music_set_speed(uint32_t per_mille);
int32_t eapp_music_gain(void);
/* Clamped to -EAPP_MUSIC_GAIN_MAX..EAPP_MUSIC_GAIN_MAX. */
void eapp_music_set_gain(int32_t gain);

/* Hands a button to the music control the way the host does for Apple's
 * games: a Play release toggles play and pause, Next and Previous releases
 * skip; anything else is ignored. For an app that handles the music buttons
 * but passes some on. */
void eapp_music_pass_button(const struct eapp_input_event *event);

#ifdef __cplusplus
}
#endif

#endif
