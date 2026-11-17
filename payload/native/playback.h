/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

struct track_session;

static inline void osos_clock_read(void *clock)
{
    typedef void (*fn)(void *);
    ((fn)OSOS_CLOCK_READ)(clock);
}

static inline uint32_t osos_clock_seconds(void *clock)
{
    typedef uint32_t (*fn)(void *, int);
    return ((fn)OSOS_CLOCK_SECONDS)(clock, 1);
}

static inline uint32_t osos_clock_mac_seconds(void *clock)
{
    typedef uint32_t (*fn)(void *, int);
    return ((fn)OSOS_CLOCK_MAC_SECONDS)(clock, 1);
}

static inline uint32_t osos_playback_clock(void)
{
    typedef uint32_t (*fn)(void);
    return ((fn)OSOS_PLAYBACK_CLOCK)();
}

static inline void osos_track_session_finish(struct track_session *session,
                                             uint32_t position)
{
    typedef void (*fn)(struct track_session *, uint32_t);
    ((fn)OSOS_TRACK_SESSION_FINISH)(session, position);
}

static inline int osos_player_select_track(struct osos_player *player, int index,
                                           int flags)
{
    typedef int (*fn)(struct osos_player *, int, int);
    return ((fn)OSOS_PLAYER_SELECT_TRACK)(player, index, flags);
}

static inline int osos_player_is_shuffled(struct osos_player *player)
{
    typedef int (*fn)(struct osos_player *);
    return ((fn)OSOS_PLAYER_IS_SHUFFLED)(player);
}

static inline int osos_player_is_repeat_one(struct osos_player *player)
{
    typedef int (*fn)(struct osos_player *);
    return ((fn)OSOS_PLAYER_IS_REPEAT_ONE)(player);
}

static inline int osos_player_is_repeat_all(struct osos_player *player)
{
    typedef int (*fn)(struct osos_player *);
    return ((fn)OSOS_PLAYER_IS_REPEAT_ALL)(player);
}
