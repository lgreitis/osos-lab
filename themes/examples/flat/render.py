# SPDX-License-Identifier: GPL-3.0-only
"""Render the Flat theme's artwork and manifest.

Glyphs come from Cupertino Icons (CupertinoIcons.ttf from the Flutter
cupertino_icons package, MIT; see LICENSE.cupertino-icons) and dial numerals
from Inter Medium (Inter-Medium.ttf from the Inter release, SIL OFL 1.1; see
LICENSE.inter). Neither font is part of the repository: pass their paths with
--font or CUPERTINO_ICONS and --text-font or INTER_FONT. A few
shapes the font lacks are drawn from primitives (`DRAWN`). Drawing uses
ImageMagick's `magick`. Sizes and formats come from the firmware bitmap
catalog, so the output always matches the native assets.

    python3 themes/examples/flat/render.py --font CupertinoIcons.ttf \
        --text-font Inter-Medium.ttf

Art drawn in one color is written once as coverage (gray level, or alpha) and
shared by both appearances; the manifest names the color role the device draws
it in (`icon_tint`, `text`, `on_accent`, `background`, ...), so it follows the
appearance and the selected accent. Art with several colors, or a color that is
not a role, is rendered per appearance. `--check` re-renders into a temporary folder
and compares the decoded pixels with the committed files.

Glyph sizes follow one optical rule per frame (see `optical_box`), so icons of
different shapes read as the same size; `--sizes` prints the resulting pixel
boxes.
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT))
from patching.bitmap import read_png  # noqa: E402
from themes import style  # noqa: E402

CATALOG = ROOT / "firmware/bitmaps.json"
PACK = ROOT / "build/themes/flat"

# iOS system palette; fill, text, selection and icon tint share one value. The
# theme's default accent is pink; every named accent, including blue, can be
# picked in Settings.
DEFAULT_ACCENT = "pink"
APPEARANCES = {
    "light": {
        "background": "#ffffff",
        "text": "#000000",
        "cover_border": "#c7c7cc",
        "muted": "#8e8e93",
        "accents": {
            "blue": "#007aff",
            "purple": "#af52de",
            "pink": "#ff2d55",
            "red": "#ff3b30",
            "orange": "#ff9500",
            "green": "#34c759",
            "teal": "#5ac8fa",
            "yellow": "#ffcc00",
            "graphite": "#8e8e93",
        },
    },
    "dark": {
        "background": "#1c1c1e",
        "text": "#ffffff",
        "cover_border": "#3a3a3c",
        "muted": "#8e8e93",
        "accents": {
            "blue": "#0a84ff",
            "purple": "#bf5af2",
            "pink": "#ff375f",
            "red": "#ff453a",
            "orange": "#ff9f0a",
            "green": "#30d158",
            "teal": "#64d2ff",
            "yellow": "#ffd60a",
            "graphite": "#98989d",
        },
    },
}
GREEN = {"light": "#34c759", "dark": "#30d158"}
RED = {"light": "#ff3b30", "dark": "#ff453a"}
ORANGE = {"light": "#ff9500", "dark": "#ff9f0a"}

# Cupertino Icons code points.
GLYPH = {
    "shuffle": 0xF4A9,
    "repeat": 0xF7BF,
    "repeat_1": 0xF7C0,
    "list_bullet": 0xF6E8,
    "arrow_up_arrow_down": 0xF51F,
    "speaker_2_fill": 0xF7EC,
    "speaker_1_fill": 0xF3B7,
    "speaker_slash_fill": 0xF3B9,
    "speaker_3_fill": 0xF3BA,
    "timer": 0xF868,
    "lightbulb": 0xF6DD,
    "lightbulb_fill": 0xF6DE,
    "sun_max_fill": 0xF4B7,
    "book_fill": 0xF3E8,
    "waveform": 0xF894,
    "waveform_path": 0xF897,
    "arrow_counterclockwise": 0xF21C,
    "antenna_radiowaves_left_right": 0xF4DC,
    "moon_fill": 0xF719,
    # The font draws this glyph with its ink on the right half.
    "square_lefthalf_fill": 0xF809,
    "paintbrush_fill": 0xF72F,
    "eyedropper_full": 0xF665,
    "slider_horizontal_3": 0xF7DC,
    "cube_fill": 0xF61D,
    "music_note_2": 0xF46C,
    "film": 0xF66B,
    "tv_music_note_fill": 0xF886,
    "photo_fill": 0xF768,
    "music_note_list": 0xF726,
    "mic_fill": 0xF461,
    "tv_fill": 0xF884,
    "person_2_fill": 0xF741,
    "doc_text_fill": 0xF639,
    "square_stack_fill": 0xF81D,
    "music_mic": 0xF725,
    "doc_fill": 0xF631,
    "search": 0xF4A5,
    "gamecontroller_fill": 0xF43B,
    "checkmark": 0xF3FD,
    "chevron_right": 0xF3D3,
    "pause_fill": 0xF478,
    "play_fill": 0xF488,
    "forward_fill": 0xF683,
    "backward_fill": 0xF57C,
    "lock_fill": 0xF4C9,
    "alarm_fill": 0xF4D6,
    "circle_fill": 0xF400,
    "bolt_fill": 0xF598,
    "battery_empty": 0xF112,
    "battery_full": 0xF113,
    "star_fill": 0xF822,
    "speaker_fill": 0xF3B8,
    "sun_min_fill": 0xF83D,
    "arrow_clockwise": 0xF49A,
    "textformat_123": 0xF85D,
    "sparkles": 0xF7E8,
    "gear_alt": 0xF43C,
    "arrow_right": 0xF50A,
}
# Glyphs drawn from whole-pixel primitives where the font's outline lands on
# fractional columns at small sizes: pause bars, the outline TV and folder, the
# status bar lock and the calendar bell.
PAUSE_BARS = "pause_bars"
TV_OUTLINE = "tv_outline"
FOLDER_OUTLINE = "folder_outline"
LOCK = "lock"
BELL = "bell_outline"
# Shapes the font lacks, as ImageMagick draw primitives in a 1000x1000 design
# space, white on black. They go through the same trim, resize and optical
# sizing as the font glyphs.
DRAWN_SPACE = 1000
DRAWN = {
    # Mortarboard: rounded band under a solid rhombus, tassel on the right.
    "graduationcap_fill": [
        "-draw",
        "roundrectangle 250,410 750,700 75,75",
        "-draw",
        "polygon 500,190 950,400 500,610 50,400",
        "-stroke",
        "white",
        "-strokewidth",
        "30",
        "-draw",
        "line 925,410 925,675",
        "-stroke",
        "none",
        "-draw",
        "circle 925,700 925,742",
    ],
    # Genius atom: three orbits around a nucleus.
    "atom": [
        "-fill",
        "none",
        "-stroke",
        "white",
        "-strokewidth",
        "50",
        "-draw",
        "translate 500,500 ellipse 0,0 450,190 0,360",
        "-draw",
        "translate 500,500 rotate 60 ellipse 0,0 450,190 0,360",
        "-draw",
        "translate 500,500 rotate 120 ellipse 0,0 450,190 0,360",
        "-stroke",
        "none",
        "-fill",
        "white",
        "-draw",
        "circle 500,500 500,580",
    ],
    # The same atom with thinner orbits for large sizes.
    "atom_thin": [
        "-fill",
        "none",
        "-stroke",
        "white",
        "-strokewidth",
        "30",
        "-draw",
        "translate 500,500 ellipse 0,0 450,190 0,360",
        "-draw",
        "translate 500,500 rotate 60 ellipse 0,0 450,190 0,360",
        "-draw",
        "translate 500,500 rotate 120 ellipse 0,0 450,190 0,360",
        "-stroke",
        "none",
        "-fill",
        "white",
        "-draw",
        "circle 500,500 500,580",
    ],
}

# Base glyph size and center for each native frame size. Settings previews keep
# the glyph in the upper part of the frame, where the stock art sits; the other
# frames center it.
FRAMES = {
    (78, 121): (66, (39, 40)),
    (78, 122): (66, (39, 40)),
    (119, 119): (66, (59, 59)),
    (98, 146): (72, (49, 73)),
    (127, 135): (72, (63, 67)),
    (116, 165): (72, (58, 82)),
    (120, 146): (72, (60, 73)),
    (142, 87): (72, (71, 43)),
    (200, 145): (88, (100, 72)),
    (161, 145): (88, (80, 72)),
    (55, 55): (40, (27, 27)),
    (128, 128): (80, (64, 64)),
    # The Contacts preview sits further right than the other Extras previews;
    # this centers its glyph on theirs.
    (130, 146): (72, (54, 73)),
    # Full preview panes, centered on the visible part like the other previews.
    (159, 240): (72, (72, 122)),
}
# Glyph boxes up to this size are downscaled by area averaging: at a few pixels
# the default filter's ringing shows as halo columns along straight edges.
SMALL_GLYPH = 26
# Optical scale of the base size by glyph shape (trimmed width / height), as
# (minimum aspect, scale of the long side). Moderately wide glyphs (shuffle,
# speakers, TV) would look heavier than square ones at the same box; very wide
# ones (controller, people) are already short, so they keep the full width.
OPTICAL = ((1.4, 1.0), (1.15, 0.82), (0.85, 1.0), (0.0, 0.97))
# Per-asset base overrides where one glyph should read larger than the rule.
OPTICAL_OVERRIDES = {
    "LockScreen_Padlock_Image": 86,
    # The mortarboard is wide and short; a little extra width matches the
    # taller glyphs around it.
    "No_Content_iTunesU_Preview_Image": 74,
    "No_Content_iTunesU_Small_Image": 45,
    "CoverFlow_Proxy_Image_iTunesU": 90,
}

SETTINGS_MASKS = {
    "Settings_ShuffleOff_Image": "shuffle",
    "Settings_ShuffleSongs_Image": "shuffle",
    "Settings_RepeatOff_Image": "repeat",
    "Settings_RepeatAll_Image": "repeat",
    "Settings_RepeatOne_Image": "repeat_1",
    "Settings_MainMenu_Image": "list_bullet",
    "Settings_SortBy_Image": "arrow_up_arrow_down",
    "Settings_VolumeLimit_Image": "speaker_2_fill",
    "Settings_Backlight_Image": "timer",
    "Settings_Backlight_2s_Image": "timer",
    "Settings_Backlight_5s_Image": "timer",
    "Settings_Backlight_10s_Image": "timer",
    "Settings_Backlight_20s_Image": "timer",
    "Settings_Backlight_30s_Image": "timer",
    "Settings_Backlight_on_Image": "lightbulb_fill",
    "Settings_Backlight_off_Image": "lightbulb",
    "Settings_Brightness_Image": "sun_max_fill",
    "Settings_Audiobooks_Image": "book_fill",
    "Settings_SoundCheckOff_Image": "waveform",
    "Settings_SoundCheckOn_Image": "waveform_path",
    "Settings_ClickerOn_Image": "speaker_1_fill",
    "Settings_ClickerOff_Image": "speaker_slash_fill",
    "Settings_Reset_Image": "arrow_counterclockwise",
    "Settings_Radio_Image": "antenna_radiowaves_left_right",
    # RepriseOS previews: catalog bitmaps in the payload block.
    "CFW_Info_Icon": "cube_fill",
    "CFW_EQ_Icon": "slider_horizontal_3",
    "CFW_Theme_Icon": "paintbrush_fill",
    "CFW_Accent_Icon": "eyedropper_full",
    "CFW_DarkMode_Moon": "moon_fill",
    "CFW_MenuCovers_Icon": "square_lefthalf_fill",
    "MainMenu_Shuffle_Image": "shuffle",
    "MainMenu_Backlight_Image": "timer",
    "MainMenu_Sleep_Image": "moon_fill",
    "No_Content_Music_Preview_Image": "music_note_2",
    "No_Content_Audiobooks_Preview_Image": "book_fill",
    "No_Content_Movies_Preview_Image": "film",
    "No_Content_Music_Video_Preview_Image": "tv_music_note_fill",
    "No_Content_Photo_Preview_Image": "photo_fill",
    "No_Content_Playlist_Preview_Image": "music_note_list",
    "No_Content_Podcast_Preview_Image": "mic_fill",
    "No_Content_iTunesU_Preview_Image": "graduationcap_fill",
    "No_Content_TVShow_Preview_Image": "tv_fill",
    "No_Content_Contacts_Preview_Image": "person_2_fill",
    "No_Content_Notes_Preview_Image": "doc_text_fill",
}
# Paletted Extras and accessory previews, tinted through their palette like the
# proxies.
EXTRAS = {
    "Alarms_Clock_Static_Image": "alarm_fill",
    "LockScreen_Padlock_Image": "lock_fill",
    "Notes_Preview_Image": "doc_text_fill",
    "Extras_Games_Preview_Image": "gamecontroller_fill",
    "AddressViewer_Preview_Image": "person_2_fill",
    "Speakers_Icon_Image": "speaker_3_fill",
}
# Stock shapes drawn in the icon tint: the EQ curves and the Apple logo.
TINT_ONLY = ["Settings_AppleLogo_Image", "Settings_AppleLogoPreview_Image"] + [
    f"Settings_EQ_{preset}_Image"
    for preset in (
        "Acoustic BassBooster BassReducer Classical Dance Deep Electronic Flat HipHop "
        "Jazz Latin Loudness Lounge Piano Pop RandB Rock SmallSpeakers SpokenWord "
        "TrebleBooster TrebleReducer VocalBooster"
    ).split()
]
PROXIES = {
    "Media_AllAlbumsProxy_Image": "square_stack_fill",
    "Media_AlbumsProxy_Image": "square_stack_fill",
    "Media_ArtistsProxy_Image": "music_mic",
    "Media_AudioProxy_Image": "music_note_2",
    "Media_AudioBooksProxy_Image": "book_fill",
    "Media_DocumentsProxy_Image": "doc_fill",
    "Media_MusicVideosProxy_Image": "tv_music_note_fill",
    "Media_PhotosProxy_Image": "photo_fill",
    "Media_PlaylistsProxy_Image": "music_note_list",
    "Media_PodcastsProxy_Image": "mic_fill",
    "Media_SearchProxy_Image": "search",
    "Media_TVShowsProxy_Image": "tv_fill",
    "Media_VideosProxy_Image": "film",
    "Games_Proxy_Image": "gamecontroller_fill",
}
# Small glyphs: name -> (glyph, box, color, crisp). A color that is a tint role
# (see `style.TINT_ROLES`) renders shared coverage the device draws in that role,
# so the glyph follows the appearance and the accent; any other color (`muted`)
# renders static art per appearance. Glyphs on highlighted rows (the White
# variants and the disclosure chevron) take on_accent, which is black for the
# yellow accent.
SMALL_GLYPHS = {
    "Settings_Legal_Image": ("doc_text_fill", (18, 23), "icon_tint", False),
    "NowPlaying_Classic_Shuffle_Image": ("shuffle", (18, 14), "icon_tint", False),
    "NowPlaying_Large_Shuffle_Image": ("shuffle", (25, 19), "icon_tint", False),
    "NowPlaying_Classic_Repeat_Image": ("repeat", (19, 15), "icon_tint", False),
    "NowPlaying_Classic_Repeat_Once_Image": ("repeat_1", (19, 15), "icon_tint", False),
    "Media_NowPlayingIcon_Black_Image": (
        "speaker_2_fill",
        (20, 14),
        "icon_tint",
        False,
    ),
    "Media_NowPlayingIcon_Playlist_Black_Image": (
        "speaker_2_fill",
        (15, 14),
        "icon_tint",
        False,
    ),
    "Settings_MainMenu_CheckmarkBlack_Image": (
        "checkmark",
        (14, 12),
        "icon_tint",
        False,
    ),
    "System_Submenu_Image": ("chevron_right", (8, 13), "on_accent", False),
    # Stopwatch state beside the elapsed time: the digits' 15 px cap height.
    "Stopwatch_SmallPlay_Image": ("play_fill", (13, 15), "text", False),
    "Stopwatch_SmallPause_Image": (PAUSE_BARS, (11, 15), "text", False),
    "Media_NowPlayingIcon_White_Image": (
        "speaker_2_fill",
        (20, 14),
        "on_accent",
        False,
    ),
    "Media_NowPlayingIcon_Playlist_White_Image": (
        "speaker_2_fill",
        (15, 14),
        "on_accent",
        False,
    ),
    "Settings_MainMenu_CheckmarkWhite_Image": (
        "checkmark",
        (14, 12),
        "on_accent",
        False,
    ),
    "NowPlaying_White_Pause_Image": ("pause_bars", (8, 10), "text", False),
    "NowPlaying_White_Play_Image": ("play_fill", (9, 10), "text", False),
    "NowPlaying_White_FF_Image": ("forward_fill", (11, 8), "text", False),
    "NowPlaying_White_Rew_Image": ("backward_fill", (11, 8), "text", False),
    "NowPlaying_White_Volume_Low_Image": ("speaker_1_fill", (9, 13), "text", False),
    "NowPlaying_White_Volume_High_Image": ("speaker_3_fill", (19, 13), "text", False),
    "NowPlaying_Dot_Image": ("circle_fill", (4, 4), "muted", False),
    "StatusBarWhite_Lock_Image": ("lock", (9, 11), "text", False),
    "StatusBarBlack_Lock_Image": ("lock", (9, 11), "text", False),
    "StatusBarWhite_PlayStatus_Image": ("play_fill", (9, 10), "text", False),
    "NowPlaying_Shuffle_Image": ("shuffle", (18, 14), "text", False),
    "NowPlaying_Repeat_Image": ("repeat", (20, 14), "text", False),
    "NowPlaying_Repeat_Once_Image": ("repeat_1", (20, 14), "text", False),
    "NowPlaying_Black_Pause_Image": ("pause_bars", (8, 10), "text", False),
    "NowPlaying_Black_Play_Image": ("play_fill", (9, 10), "text", False),
    "NowPlaying_Black_FF_Image": ("forward_fill", (11, 8), "text", False),
    "NowPlaying_Black_Rew_Image": ("backward_fill", (11, 8), "text", False),
    "NowPlaying_Black_Record_Image": ("circle_fill", (13, 13), "red", False),
    "NowPlaying_Black_Volume_Locked_Image": ("lock_fill", (9, 13), "white", False),
    "NowPlaying_White_Volume_Locked_Image": ("lock_fill", (9, 13), "text", False),
    "NowPlaying_Star_Image": ("star_fill", (11, 11), "text", False),
    "NowPlaying_Small_White_Star_Image": ("star_fill", (11, 11), "text", False),
    "NowPlaying_Small_Grey_Star_Image": ("star_fill", (11, 11), "muted", False),
    "NowPlaying_Blue_Star_Image": ("star_fill", (13, 12), "accent_fill", False),
    "NowPlaying_Blue_Dot_Image": ("circle_fill", (3, 3), "accent_fill", False),
    "NowPlaying_Large_Blue_Star_Image": ("star_fill", (19, 19), "accent_fill", False),
    "NowPlaying_Genius_Icon": ("atom", (26, 26), "text", False),
    "System_Overlay_VolumeLeft_Image": ("speaker_fill", (9, 13), "text", False),
    "System_Overlay_VolumeRight_Image": ("speaker_3_fill", (19, 13), "text", False),
    "System_Overlay_BrightnessLess_Image": ("sun_min_fill", (10, 13), "text", False),
    "System_Overlay_BrightnessMore_Image": ("sun_max_fill", (17, 17), "text", False),
    # List row icons, in outline where the font has one so the rows stay light.
    # Only Genius has a highlighted variant, so the others are drawn in text,
    # which reads on both the plain and the highlighted row.
    "Playlist_Regular_Icon": ("music_note_list", (21, 20), "text", False),
    "Playlist_Smart_Icon": ("gear_alt", (21, 20), "text", False),
    "Playlist_Saved_Genius_Icon": ("atom", (19, 18), "text", False),
    "Playlist_Folder_Icon": ("folder_outline", (21, 16), "text", False),
    "Playlist_Genius_Icon": ("atom", (19, 19), "text", False),
    "Playlist_Genius_Icon_Hi": ("atom", (19, 19), "on_accent", False),
    "Media_Genius_Normal_Image": ("atom", (17, 17), "text", False),
    "Media_Genius_Selected_Image": ("atom", (17, 17), "on_accent", False),
    "Media_Genius_Refresh_Image": ("arrow_clockwise", (15, 16), "text", False),
    "Media_Genius_Refresh_Hi_Image": ("arrow_clockwise", (15, 16), "on_accent", False),
    # Secondary marker at the right of video rows: outline, so it stays light.
    "Media_VideoStatusIcon_Image": ("tv_outline", (13, 10), "muted", False),
    "Media_VideoStatusIcon_White_Image": ("tv_outline", (13, 10), "on_accent", False),
    "Podcasts_new_item_Image": ("circle_fill", (9, 9), "accent_fill", False),
    "Podcasts_new_item_white_Image": ("circle_fill", (9, 9), "on_accent", False),
    "Podcasts_video_solo_Image": ("tv_outline", (11, 9), "muted", False),
    "Podcasts_video_white_solo_Image": ("tv_outline", (11, 9), "on_accent", False),
    "Page_White_Active_Image": ("circle_fill", (6, 6), "text", False),
    "Page_White_Inactive_Image": ("circle_fill", (6, 6), "muted", False),
    "Settings_About_Bubble_Selected_Image": ("circle_fill", (6, 6), "text", False),
    "Settings_About_Bubble_Deselected_Image": ("circle_fill", (6, 6), "muted", False),
    "Media_OTGPlaylist_Image": ("music_note_list", (36, 36), "icon_tint", False),
    # Calendar month markers at the right of the day number, in text so they
    # read on the day tiles and on the selected day: an outline bell for an
    # alarm, level with the number's cap, and an event dot like iOS below it.
    # The large bell keeps the same outline.
    "Calendar_Small_Alarm": (BELL, (9, 9), "text", False),
    "Calendar_Flag": ("circle_fill", (5, 5), "text", False),
    "Calendar_Alarm": (BELL, (16, 16), "text", False),
    # Genius: the empty Genius Mixes page's atom in the accent like the other
    # previews, and the atom beside the Genius slider.
    "Media_Genius_Large_Image": ("atom_thin", (88, 88), "icon_tint", False),
    "Genius_Slider_Icon_Image": ("atom", (18, 18), "text", False),
}
# Glyphs stacked in one bitmap: name -> (role, ((glyph, box, center), ...)).
# Podcast episodes that are new and video show the unplayed dot level with the
# title and the outline TV level with the subtitle, both in the role.
STACKED_GLYPHS = {
    "Podcasts_new_item_video_Image": (
        "accent_fill",
        (("circle_fill", (9, 9), (6, 5)), ("tv_outline", (11, 9), (6, 24))),
    ),
    "Podcasts_new_item_video_white_Image": (
        "on_accent",
        (("circle_fill", (9, 9), (6, 5)), ("tv_outline", (11, 9), (6, 24))),
    ),
}
# Glyph centers other than the frame center, where the stock frame keeps room
# for a reflection or the glyph sits off center like the stock art.
GLYPH_CENTERS = {
    # Right-aligned, 8 px before the elapsed time; level with its digits.
    "Stopwatch_SmallPlay_Image": (16, 8),
    "Stopwatch_SmallPause_Image": (17, 8),
    "Playlist_Regular_Icon": (11, 12),
    "Playlist_Smart_Icon": (11, 12),
    "Playlist_Saved_Genius_Icon": (11, 12),
    "Playlist_Folder_Icon": (11, 12),
    # Level with the single-line row title.
    "Playlist_Genius_Icon": (11, 11),
    "Playlist_Genius_Icon_Hi": (11, 11),
    "Media_VideoStatusIcon_Image": (16, 13),
    "Media_VideoStatusIcon_White_Image": (16, 13),
    # Unplayed dots: about two thirds of the title's cap height, level with the
    # title like iOS.
    "Podcasts_new_item_Image": (6, 5),
    "Podcasts_new_item_white_Image": (6, 5),
    "Podcasts_video_solo_Image": (6, 14),
    "Podcasts_video_white_solo_Image": (6, 14),
    "Media_OTGPlaylist_Image": (23, 28),
    "Calendar_Small_Alarm": (5, 6),
    "Calendar_Flag": (5, 6),
    # Centered on the screen and between the header and the description.
    "Media_Genius_Large_Image": (60, 54),
    "Genius_Slider_Icon_Image": (9, 9),
    # Status bar glyphs: playback glyphs at the cap height of the title (10 px)
    # and the lock at the battery body's (11 px), on the battery's rows in both
    # the White and the Black bar.
    "StatusBarWhite_Lock_Image": (6, 7),
    "StatusBarBlack_Lock_Image": (5, 7),
    **{
        name: (7, 7)
        for name in (
            "NowPlaying_White_Pause_Image",
            "NowPlaying_White_Play_Image",
            "NowPlaying_White_FF_Image",
            "NowPlaying_White_Rew_Image",
            "StatusBarWhite_PlayStatus_Image",
            "NowPlaying_Black_Pause_Image",
            "NowPlaying_Black_Play_Image",
            "NowPlaying_Black_FF_Image",
            "NowPlaying_Black_Rew_Image",
        )
    },
    # Level with the rating stars' centroid, drawn from the same frame origin.
    "NowPlaying_Dot_Image": (11, 11),
    "NowPlaying_Blue_Star_Image": (8, 8),
    "NowPlaying_Blue_Dot_Image": (7, 8),
    "NowPlaying_Large_Blue_Star_Image": (10, 10),
    "NowPlaying_Repeat_Once_Image": (12, 8),
    "System_Overlay_VolumeLeft_Image": (4, 6),
    "System_Overlay_BrightnessLess_Image": (5, 6),
}
# Full-screen views (Brightness, Screen Lock, the alarm pickers, Stopwatch,
# Charging) head with the Now Playing status bar and draw the Black icon family
# on it (StatusBarBlack_*, NowPlaying_Black_*). The bar is the page background
# and its icons draw in text, like the White bar; the title takes text or
# background, whichever contrasts with the bar. The Black bar background keeps
# a dark fill like the stock art.
DARK_BAR = "#1c1c1e"
# Solid fills. The preview pane shows the system background bitmap; the list is
# painted over it. Tint roles render one full-coverage file for both appearances.
SOLID = {
    "StatusBarWhite_Background_Image": "background",
    "StatusBarBlack_Background_Image": "dark_bar",
    "NowPlaying_StatusBar_Image": "background",
    "Background_LightBlue_Image": "background",
    "Settings_About_Band_Image": "background",
    # Genius Mixes keeps the stock dark page.
    "GeniusMixes_Background_Image": "dark_bar",
}
# Unselected World Clock rows: the page background with an iOS separator on
# the last row, so stacked rows stay apart when the background matches the
# list canvas (dark mode). The firmware tiles the bar across the row and draws
# the labels in the text role. Values are (name, separator colors).
CLOCK_ROW = ("WorldClock_Graybar_Image", {"light": "#c6c6c8", "dark": "#38383a"})
TRANSPARENT = (
    "Podcasts_spacer_Image",
    "Settings_BasePanel_Image",
    "Settings_BasePanelWide_Image",
    "Clock_Shadow_Image",
    # The stopwatch's pivot caps; its hands carry their own hubs.
    "Stopwatch_Caps_Image",
)
# World map behind the clocks and the time zone pin: square dots on a grid,
# one per land cell of the stock map's projection, so the firmware's pin
# positions still land on their cities. Rows are hex bitmaps (most significant
# bit = leftmost cell) starting at grid row `top`. The mask is tinted with the
# text role at low coverage, a soft gray in both appearances.
DOT_MAP = {
    "name": "WorldClock_Map_Image",
    "pitch": 4,
    "dot": 2,
    "level": 2,
    "top": 9,
    "rows": [
        "00000003000000000000",
        "00001fffe00000000000",
        "00003effc06000600000",
        "00003dffc0e000100000",
        "000eb9ffc000307c1000",
        "001c783fc00041fc0000",
        "001fdc3f80004fffdc00",
        "1f7f7f3f80783ffffff0",
        "1ffff3be00fffffffff8",
        "1ffff31860fffffffff0",
        "1fffc61801ffffffffe0",
        "1cffc78001fffffff100",
        "007ff7800dbfffffe300",
        "003fffc00dfffffff000",
        "001fffc007fffffff000",
        "001fff8007ffffffe000",
        "001ffe000ef37fffd000",
        "000ffc000c3f7ffe9000",
        "000ffc000f07fffe6000",
        "0007f8000ffffffe0000",
        "0003c8001ffffffe0000",
        "000180003fffcffc0000",
        "0000e0003fffc7700000",
        "000070003fff86380000",
        "000013003fff06100000",
        "000007c01fff00000000",
        "000007e000ff00360000",
        "000007f800fc00167000",
        "000007fc00fc00001800",
        "000007fc00fc00000000",
        "000003fc00fd0000f000",
        "000001fc00f90001f800",
        "000001f800790007fc00",
        "000001f000780003fc00",
        "000001e000700003fc00",
        "000003c0002000023c00",
        "000003c0000000000820",
        "00000300000000000800",
        "00000300000000000040",
        "00000300000000000000",
        "00000200000000000000",
        "00000100000000000000",
    ],
}
# The time zone pin: a red head on a gray needle whose tip is the stock tip
# pixel, the point the firmware places on the city. Values are (name, tip,
# head diameter); the needle runs straight up the tip's column to the head.
PIN = ("Settings_DateTime_TimeZonePin_Image", (13, 17), 11)
# Small flat clock faces: a disc with hour ticks drawn inside the native bitmap
# around the hand pivot (half the width, half the smaller side). Values are
# (face role, tick role, dial radius).
CLOCKS = {
    "Clock_Small_Image": ("face", "muted", 35),
    "Clock_Small_Night_Image": ("night", "muted", 35),
}
CLOCK_FACE = "#f2f2f7"
NIGHT_FACE = "#3a3a3c"
# Dials with numerals: the large world clock and the stopwatch. The firmware
# draws the hands from fixed masks around a pivot in the frame, so the face is
# drawn around that point: the large clock's 73x73 hands pivot on a pixel
# center and reach 25 px, so its dial is smaller than the 132x145 asset; the
# stopwatch pivots sit on pixel corners. Tick widths follow the pivot's parity
# so the upright and level ticks land on whole pixels. Ticks are (count, major every, outer radius, (length, width, color)
# for major and minor ticks). Numerals are (angle, label) pairs set in the text
# font, each ink box `gap` px inside the major ticks along its radius, so wide
# and narrow labels clear the ring by the same distance. The stopwatch adds a
# minutes ring around its second pivot, as (center, radius), and a body
# behind the face: a bezel ring, a crown on a stem above twelve and a pusher
# toward half past one, in the body color of each appearance. The large
# clock sits on the world map, so a halo of page color (width) clears the dots
# around it and a 1 px rim (colors) outlines the face.
DIAL_TEXT = "#1c1c1e"
DIALS = {
    "Clock_Large_Image": {
        "center": (66.5, 66.5),
        "radius": 48,
        "ticks": (60, 5, 44, (4, 1, "#8e8e93"), (2, 1, "#c7c7cc")),
        "numerals": [(hour * 30, str(hour or 12)) for hour in range(12)],
        "gap": 3,
        "size": 10,
        "ring": None,
        "body": None,
        "halo": (8, {"light": "#d1d1d6", "dark": "#48484a"}),
    },
    "Stopwatch_Clock_Image": {
        "center": (72, 104),
        "radius": 62,
        "ticks": (60, 5, 58, (6, 2, DIAL_TEXT), (3, 1, "#8e8e93")),
        "numerals": [(step * 30, str(step * 5 or 60)) for step in range(12)],
        "gap": 3,
        "size": 10,
        "ring": ((72, 89), 11),
        "halo": None,
        "body": {
            "colors": {"light": "#c7c7cc", "dark": "#48484a"},
            "bezel": 66,
            # Rectangles are inclusive (x0, y0, x1, y1), the crown as (rect,
            # corner radius); the pusher as (angle, (start, end, width) of the
            # stem, then of the cap) in radii from the center.
            "stem": (69, 30, 74, 44),
            "crown": ((62, 22, 81, 31), 3),
            "pusher": (42, (58, 70, 6), (68, 74, 12)),
        },
    },
}
# The hand pivot cap, drawn by the firmware over the hands: a tinted dot so it
# follows the accent. Values are (name, dot radius).
CENTER_CAP = ("Clock_CenterCap_Image", 2.5)
# Clock hands: untinted masks the firmware draws black on light faces and white
# on the night face. Frame i points 3 * i degrees clockwise from twelve; the
# 30-frame sets cover a quadrant and the 16-frame sets an octant, which the
# firmware rotates and mirrors around the frame center, the pivot. Values are
# (frames, stroke width, tip reach, tail reach, hub radius) in pixels. Stroke
# widths match the frame's parity so upright and level frames land on whole
# pixels. The world clock draws a cap over its pivot; the stopwatch draws none
# on screen, so its hands carry their own hub.
HANDS = {
    "Clock_Hours_Image_": (30, 3, 16, 0, 0),
    "Clock_Mins_Image_": (16, 3, 25, 0, 0),
    "Clock_Secs_Image_": (16, 1, 31, 7, 0),
    "Stopwatch_Secs_Image_": (30, 2, 54, 12, 3),
    "Stopwatch_Mins_Image_": (30, 2, 8, 3, 2),
}
# The stopwatch's idle button: a flat well with play and pause, as (name, inset,
# corner radius, play box, pause bar (width, height), gap between the bars, gap
# between play and pause). The well is an iOS gray 6 on the white light page and
# the WELL color on the dark page.
STOPWATCH_BUTTON = (
    "Stopwatch_PlayPause_Image",
    2,
    16,
    (24, 28),
    (7, 28),
    6,
    10,
)
# Three-slice bars drawn flat: (name prefix, suffixes, color role, vertical inset
# as one value or (top, bottom), corner radius, horizontal inset as one value
# or (left, right)). The thumb is placed flush with the well by the firmware,
# so its side inset matches the vertical one through transparent columns. The
# Black and White option bars are the letter strip of Search and the button row
# of dialogs (the well) and the key cursor or selected button (the thumb); the
# input field is the Search text box. The thumb is coverage art tinted with the
# selection role on the device, so it follows the accent; the firmware draws its
# letter in on_accent and the well letters in text or background, whichever
# contrasts with the well.
SLICED = (
    ("OptionBar_Black_Well_", ("LeftCap", "Center", "RightCap"), "well", 0, 4, 0),
    ("OptionBar_Black_Thumb_", ("LeftCap", "Center", "RightCap"), "selection", 2, 3, 2),
    ("OptionBar_White_Well_", ("LeftCap", "Center", "RightCap"), "well", 0, 4, 0),
    ("OptionBar_White_Thumb_", ("LeftCap", "Center", "RightCap"), "selection", 2, 3, 2),
    ("System_Input_Field_", ("Left", "Middle", "Right"), "field", 0, 5, 0),
    # Now Playing's shuffle selector (Off/Songs/Albums), an iOS segmented
    # control on the page: a gray well and the thumb in the selection role, its
    # radius concentric with the well's.
    (
        "OptionBar_NowPlaying_Well_",
        ("LeftCap", "Center", "RightCap"),
        "segment",
        0,
        8,
        0,
    ),
    (
        "OptionBar_NowPlaying_Thumb_",
        ("LeftCap", "Center", "RightCap"),
        "selection",
        2,
        6,
        2,
    ),
    # Dialog buttons: the active one in the selection role like the option bar
    # thumbs, the others a dark capsule like the stock button, whose label the
    # firmware draws light.
    ("System_ActiveButton", ("LeftCap", "Fill", "RightCap"), "selection", 0, 5, 0),
    ("System_NormalButton", ("LeftCap", "Fill", "RightCap"), "button", 1, 6, 0),
    # The rental warning over a video: a dark translucent panel like the stock
    # one.
    (
        "NowPlaying_Rental_Warning_Overlay_",
        ("Left", "Fill", "Right"),
        "overlay",
        0,
        8,
        0,
    ),
    ("System_Overlay_", ("Left", "Middle", "Right"), "panel", (12, 22), 6, 0),
    # The tray behind a dialog's buttons, where the stock body sits between its
    # drop shadow columns.
    (
        "Lozenge_",
        ("Left_White", "Middle_White", "Right_White"),
        "panel",
        (4, 8),
        8,
        (5, 7),
    ),
    (
        "NowPlaying_ProgressBar_",
        ("LeftEndcap", "Growth", "RightEndcap"),
        "small_track",
        1,
        2,
        0,
    ),
    (
        "NowPlaying_ProgressFill_",
        ("LeftEndcap", "Growth", "RightEndcap"),
        "accent_fill",
        1,
        2,
        0,
    ),
)
# The segmented control's well: iOS gray 6 on the white light page.
SEGMENT_WELL = {"light": CLOCK_FACE, "dark": "#2c2c2e"}
# Normal dialog buttons and the rental warning panel stay dark in both
# appearances.
BUTTON = "#3a3a3c"
OVERLAY = ("#000000", 0.55)
# Track of the small three-slice progress bar: translucent white, like the
# stock bar, for the dark video controls.
SMALL_TRACK = ("#ffffff", 0.35)
# The input field matches the well in both appearances: white in light, iOS
# secondarySystemBackground in dark. The device picks the well letter and input
# text colors by contrast with each, and draws the input caret in accent_text.
WELL = {"light": "#ffffff", "dark": "#2c2c2e"}
FIELD = WELL
# The translucent panel behind Search's keyboard, the Clock's Add/Edit bar and
# the volume and brightness overlays: (color, alpha). Light uses iOS systemGray4
# so the tray separates from white pages and from the Clock's native light-gray
# page, which matches systemGray5.
PANEL = {"light": ("#d1d1d6", 0.94), "dark": ("#383838", 0.94)}
# Bars tiled across the screen in the panel color: the video player's bottom
# controls, whose labels the firmware draws in text and whose icons are the
# overlay icons, and the photo slideshow's caption strip.
PANEL_BARS = ("NowPlaying_Overlay_Image", "Photos_Slideshow_Top_Strip")
# Header of a flipped Cover Flow album, tiled across: iOS grouped background
# with an opaque separator on the last row, where the stock tile has its line.
BACKSIDE_HEADER = (
    "CoverFlow_Backside_Top_Image",
    {"light": ("#f2f2f7", "#c6c6c8"), "dark": ("#2c2c2e", "#38383a")},
)
# The pane edge shadow starts at this alpha at the divider and eases out across
# the bitmap: alpha(x) = peak * (1 - x / width) ** SHADOW_EASE.
SHADOW = {"light": 0.18, "dark": 0.45}
SHADOW_EASE = 2.2
# Status bar battery (26x13), after the Cupertino battery glyph but pixel
# aligned: a 1px outline, a 1px gap, the charge level as a solid fill inside,
# and a translucent nub. The Charge and Plug overlays share the body and nub so
# every state has the same silhouette. Rectangles are inclusive frame pixels
# plus a corner radius.
BATTERY = {
    "body": (1, 1, 22, 11, 3),
    "fill": (3, 3, 20, 9, 1),
    "nub": (23, 4, 24, 8, 1),
    "outline_alpha": 1.0,
    "nub_alpha": 0.45,
    "stroke": 1,
}
# Now Playing idle battery (72x40): two colors per level, so an outline with no
# antialiasing.
# Status bar battery variants. Both bars are the flat background, so their
# batteries draw in text; levels up to LOW_BATTERY show a red fill.
STATUS_BATTERIES = ("White", "Black")
LOW_BATTERY = 2
IDLE_BATTERY = {
    "body": (3, 2, 60, 37, 6),
    "fill": (8, 7, 55, 32, 3),
    "nub": (62, 13, 67, 26, 2),
    "outline_alpha": 1.0,
    "nub_alpha": 1.0,
    "stroke": 2,
}
# Artwork placeholders (BAKED) sit on an iOS systemGray5 square in both
# appearances.
PLACEHOLDER = {"light": "#e5e5ea", "dark": "#2c2c2e"}
# Charging screen battery, after the status bar battery: an outline in text, a
# gap and a green fill, with a translucent nub. The firmware joins a left cap,
# tiled middles, the fill's end column and a right cap, so the pieces are cut
# from one strip of [left cap | middle | fill end | right cap] on whole
# columns. Rectangles are inclusive strip pixels plus a corner radius; the
# stock body rows are kept so the pieces line up with the firmware layout.
CHARGING_STRIP = (99, 142)
CHARGING_BATTERY = {
    "body": (22, 22, 67, 97, 12),
    "fill": (30, 30, 59, 89, 6),
    "nub": (68, 44, 72, 75, 2),
    "outline_alpha": 1.0,
    "nub_alpha": 0.45,
    "stroke": 4,
}
# Strip variant (empty, full, or the fill ending in the fill-end column as the
# level fraction) -> pieces as (name suffix, left column).
CHARGING_PIECES = {
    (0, 1): (("Empty_CapLeft", 0), ("Empty_Middle", 38), ("Empty_CapRight", 54)),
    (1, 1): (("Green_CapLeft", 0), ("Green_Middle", 38), ("Green_CapRight", 54)),
    (2, 3): (("Green_Middle_Cap", 46),),
}
# The bolt shown while charging and the plug shown when charged, as masks the
# device draws in text. The bolt sits at the left of its frame and low in it:
# that centers it on the flat battery, whose thin nub makes it read narrower
# than the stock battery with its metal cap.
CHARGING_BOLT = ("ChargingMode_Image_ToggleCharging", "bolt_fill", (19, 30), (9, 31))
CHARGING_PLUG = "ChargingMode_Image_ToggleCharged"
# About shows the iPod between Used and Free in place of the glossy logo: a 2 px
# body outline, a solid screen and a click wheel ring, in the icon tint. The box
# is centered on `IPOD_CENTER` in the frame, in the About band and level with
# the labels. Rectangles are inclusive box pixels plus a corner radius; the
# wheel is (center x, center y, outer radius, inner radius) in box coordinates.
IPOD = "Settings_About_Logo_Image"
IPOD_BOX = (22, 32)
IPOD_CENTER = (32, 40)
IPOD_BODY = (0, 0, 21, 31, 4)
IPOD_STROKE = 2
IPOD_SCREEN = (4, 4, 17, 13, 1)
IPOD_WHEEL = (11, 22, 6, 4)
# Storage bars. Each slice is a band across its frame, rounded only at a true
# end: (start column or None to run on from the previous slice, end column or
# None to run on into the next). The firmware joins the first segment's left
# cap, tiled centers, a 2 px edge between segments and the free part's right
# cap; the main menu's simple bar joins the used part's left cap and centers
# with the free part's centers and right cap.
CAPACITY_TRACK = {"light": "#d1d1d6", "dark": "#3a3a3c"}
CAPACITY_COLORS = {
    "Audio": {"light": "#007aff", "dark": "#0a84ff"},
    "Video": {"light": "#af52de", "dark": "#bf5af2"},
    "Photos": {"light": "#ffcc00", "dark": "#ffd60a"},
    "Other": {"light": "#ff9500", "dark": "#ff9f0a"},
}
# Detailed bar: rows, radius, and left/center/right ends.
CAPACITY_DETAILED = (
    (7, 18),
    4,
    {"Left": (6, None), "Center": (None, None), "Right": (None, 9)},
)
CAPACITY_SIMPLE = ((3, 9), 3)
# Legend squares beside the labels, as (inset, radius).
CAPACITY_LEGEND = (1, 2)
# Date & Time preview: a calendar card with a red header; the firmware draws
# the month on the header and the day on the card body. Values are the card
# (inclusive rectangle and radius) and the header's last row.
CALENDAR_CARD = ("Settings_DateTime_Calendar_Image", (16, 18, 113, 105, 10), 39)
# The card body: iOS gray 6 on the white light page, white on dark.
CARD = {"light": CLOCK_FACE, "dark": "#ffffff"}
# Date and time pickers: a tray with the field wells baked in, in the panel and
# well colors like a dialog's buttons, since the firmware draws the digits in
# text. The highlight covers one well and follows the selection role.
# Rectangles are inclusive.
PICKER_RADIUS = 6
PICKER_TRAYS = {
    # The wells sit under the firmware's highlight, which it draws at rows 6..42
    # on entry and 8..44 after moving between fields, so they are centered
    # between the two. Equal padding all round; the tray corner is concentric
    # with the wells'.
    **{
        name: ((10, 1, 156, 49, 12), [(x, 7, x + 39, 43) for x in (16, 63, 111)])
        for name in ("Settings_DateTime_Capsule_Image", "DateTimePicker_Bar")
    },
    # The timer's wells keep the stock columns, where the firmware places the
    # digits and the focus.
    "Alarms_Timer_Time_Panel": (
        (9, 0, 154, 44, 10),
        [(x, 4, x + 39, 40) for x in (13, 64, 111)],
    ),
    # Screen Lock: one well per digit under the 26 px focus, 40 px apart and
    # centered on the digits' rows.
    "LockScreen_TumblerBgnd_Image": (
        (11, 0, 168, 47, 12),
        [(x, 6, x + 25, 41) for x in range(17, 138, 40)],
    ),
}
PICKER_WELLS = {"DateTimePicker_Well": (0, 2, 39, 38)}
# Highlights tinted with the selection role, as an inclusive rectangle in the
# frame or None for the whole frame.
PICKER_HIGHLIGHTS = {
    "Settings_DateTime_Capsule_Hilite": None,
    "DateTimePicker_Selection": None,
    "Alarms_Timer_Time_Panel_Focus": None,
    "LockScreen_TumblerFocus_Image": (0, 6, 25, 41),
}
# Contacts without a picture: a circle with even margins in the frame and
# a head-and-shoulders silhouette clipped to it, as (diameter, (circle,
# silhouette) colors per appearance, people). Each person is (horizontal
# offset, head offset, head radius, shoulder offset, shoulder radius) as
# fractions of the diameter from the circle's center; later people draw in
# front.
AVATAR_COLORS = {"light": ("#e5e5ea", "#aeaeb2"), "dark": ("#3a3a3c", "#8e8e93")}
AVATARS = {
    "AddressViewer_NoContactPict_Image": (45, [(0, -0.12, 0.19, 0.47, 0.36)]),
    "AddressViewer_NoGroupPict_Image": (
        45,
        [(-0.17, -0.16, 0.15, 0.38, 0.28), (0.13, -0.08, 0.17, 0.5, 0.32)],
    ),
}
# The Volume Limit marker under the bar: an upright triangle in text, its tip
# on the middle column, which the firmware puts on the limit.
LIMIT_ARROW = "Settings_VolumeLimitArrow_Image"
# Settings > Language preview: greetings in the text font, one shared
# coverage file in the text role. Lines are (indent, point size, opacity,
# word), stacked so each cap top sits `gap` px under the previous baseline;
# the block is centered on `center` (the preview glyphs' row on screen). The
# frame runs past the screen edge, so long lines are cut there.
LANGUAGE = {
    "name": "Settings_Language_Image",
    "lines": [
        (30, 17, 0.32, "Bienvenue"),
        (50, 13, 0.22, "Добро пожаловать"),
        (10, 27, 1.0, "Welcome"),
        (30, 19, 0.42, "Willkommen"),
        (10, 13, 0.22, "Καλώς ήρθατε"),
        (50, 16, 0.32, "Benvenuto"),
    ],
    "gap": 14,
    "center": 122,
}
# The Genius slider: a knob with an arrow on an accent band that trails it
# from a rounded left end. Values are the band's rows (inclusive), the corner
# radius, the knob's columns in its frame and the arrow's box. The knob takes
# the well colors and its arrow the text color; the band is coverage in
# accent_fill, the role the stock band follows.
GENIUS_SLIDER = {
    "rows": (3, 29),
    "radius": 5,
    "knob": (4, 70),
    "arrow": (17, 13),
}
# Calendar month cells, as (corner radius, current-month tile colors, ring
# colors for the other months' outline tiles). The selected day is a tile in
# the selection role.
CALENDAR_CELLS = (
    6,
    {"light": "#f2f2f7", "dark": "#2c2c2e"},
    {"light": "#e5e5ea", "dark": "#2c2c2e"},
)
BAKED = {
    "CoverFlow_Proxy_Image": "music_note_2",
    "CoverFlow_Proxy_Image_iTunesU": "graduationcap_fill",
    "No_Content_Albums_Small_Image": "square_stack_fill",
    "No_Content_AudioBooks_Small_Image": "book_fill",
    "No_Content_MusicVideos_Small_Image": "tv_music_note_fill",
    "No_Content_Podcasts_Small_Image": "mic_fill",
    "No_Content_iTunesU_Small_Image": "graduationcap_fill",
    "No_Content_TVShows_Small_Image": "tv_fill",
    "No_Content_Videos_Small_Image": "film",
}
# Translucent dark rounded tiles, dark in both appearances because the firmware
# draws light content on them: (alpha, corner radius, white glyph and box or
# None). The paused badge carries a pause glyph; the Quick Scroll tile gets
# its letter from the firmware.
BADGES = {
    "NowPlaying_Paused_Image": (0.55, 12, ("pause_fill", (24, 28))),
    "System_QuickScroll_Image": (0.55, 12, None),
}
# Activity spinner petals: 3 px capsules 12 px long through the frame center,
# at these angles from vertical (clockwise), untinted like the stock masks.
DAISY = {
    f"BigDaisyPetal{index}": angle for index, angle in enumerate(range(0, 180, 30))
}
DAISY_PETAL = (12, 3)
# Untinted masks drawn in the firmware's own color, as (glyph, box, center).
PLAIN_MASKS = {
    "System_QuickScroll_123_Image": ("textformat_123", (46, 34), (37, 35)),
}
# Caption box corners: untinted masks of a rounded corner, as (name, corner of
# the frame the arc's center sits on).
CAPTION_CORNERS = {
    "Caption_LeftEndcapTop_Image": (1, 1),
    "Caption_RightEndcapTop_Image": (0, 1),
    "Caption_LeftEndcapBottom_Image": (1, 0),
    "Caption_RightEndcapBottom_Image": (0, 0),
}
# Light mode draws the scroll indicator in translucent black over the content.
SCROLL_INDICATOR = {"light": ("#000000", 0.28), "dark": ("#8e8e93", 1.0)}
SCROLLBAR = (
    "System_ScrollbarTop_Image",
    "System_ScrollbarCenter_Image",
    "System_ScrollbarBottom_Image",
)
# Now Playing progress and volume bar: 8 px tiles the firmware repeats across
# the bar, so the ends are square. The stock tiles keep a reflection under the
# 14 px bar; the flat bar is a 7 px band (rows inclusive) centered on the
# volume speaker glyphs beside it. The chapter marks are 1 px gaps cut through
# the band in the background color.
PROGRESS_ROWS = (5, 11)
PROGRESS_TRACK = {"light": "#e5e5ea", "dark": "#3a3a3c"}
PROGRESS_TILES = {
    "NowPlaying_ProgressBar_Active_Background_Image": "progress_track",
    "NowPlaying_ProgressBar_Inactive_Background_Image": "progress_track",
    "NowPlaying_ProgressBar_Active_Fill_Image": "accent_fill",
    "NowPlaying_ProgressBar_Inactive_Fill_Image": "accent_fill",
    "NowPlaying_ProgressBar_Active_Fill_Orange_Image": "orange",
    "NowPlaying_Chaptermark_Image": "background",
}
# Scrub knobs as (name, center, radius, color role): an accent disc one pixel
# past the band on each side on the large bar, and a white disc on the small
# bar.
KNOBS = (
    ("NowPlaying_ProgressBar_Scrub_Image", (6.5, 8.5), 4.5, "accent_fill"),
    ("NowPlaying_Scrub_Image", (7.0, 4.5), 4.0, "white"),
)


# ImageMagick output options for 8-bit gray coverage PNGs.
GRAY8 = (
    "-colorspace",
    "gray",
    "-depth",
    "8",
    "-define",
    "png:color-type=0",
    "-define",
    "png:bit-depth=8",
)


def rgba(color, alpha):
    r, g, b = (int(color[i : i + 2], 16) for i in (1, 3, 5))
    return f"rgba({r},{g},{b},{alpha})"


class Renderer:
    def __init__(self, font, text_font, catalog, out, work):
        self.font = str(font)
        self.text_font = str(text_font)
        self.assets = {asset["name"]: asset for asset in catalog["bitmaps"]}
        self.out = out
        self.work = Path(work)
        self.entries = {"light": [], "dark": []}
        self.sizes = {}

    def magick(self, *args):
        subprocess.run(["magick", *map(str, args)], check=True)

    def size(self, name):
        asset = self.assets[name]
        return asset["width"], asset["height"]

    def aspect(self, glyph):
        """Width / height of the glyph's trimmed outline."""
        path = self.work / f"{glyph}-trim.png"
        if not path.exists():
            self.magick(*self.outline(glyph), "-trim", "+repage", path)
        width, height = self.image_size(path)
        return width / height

    def image_size(self, path):
        output = subprocess.run(
            ["magick", str(path), "-format", "%wx%h", "info:"],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
        width, height = output.split("x")
        return int(width), int(height)

    def optical_box(self, name, glyph, base):
        """Square box that gives this glyph the frame's optical size."""
        base = OPTICAL_OVERRIDES.get(name, base)
        aspect = self.aspect(glyph)
        scale = next(s for limit, s in OPTICAL if aspect >= limit)
        side = round(base * scale)
        return side, side

    def outline(self, glyph):
        """magick arguments producing the untrimmed glyph, white on black."""
        if glyph in DRAWN:
            size = f"{DRAWN_SPACE}x{DRAWN_SPACE}"
            return ["-size", size, "xc:black", "-fill", "white", *DRAWN[glyph]]
        return [
            "-background",
            "black",
            "-fill",
            "white",
            "-font",
            self.font,
            "-pointsize",
            "200",
            f"label:{chr(GLYPH[glyph])}",
        ]

    def glyph(self, glyph, box, crisp=False):
        """Gray coverage of a glyph fitted into box, as a temporary PNG."""
        path = self.work / f"{glyph}-{box[0]}x{box[1]}-{int(crisp)}.png"
        if glyph == PAUSE_BARS and not path.exists():
            self.pause_bars(box, path)
        if glyph == TV_OUTLINE and not path.exists():
            self.tv_outline(box, path)
        if glyph == FOLDER_OUTLINE and not path.exists():
            self.folder_outline(box, path)
        if glyph == LOCK and not path.exists():
            self.lock(box, path)
        if glyph == BELL and not path.exists():
            self.bell_outline(box, path)
        if not path.exists():
            self.magick(
                *self.outline(glyph),
                "-trim",
                "+repage",
                *(("-filter", "point") if crisp else ()),
                *(("-filter", "Box") if max(box) <= SMALL_GLYPH and not crisp else ()),
                "-resize",
                f"{box[0]}x{box[1]}",
                "-background",
                "black",
                "-gravity",
                "center",
                "-extent",
                f"{box[0]}x{box[1]}",
                *(("-threshold", "50%") if crisp else ()),
                "-colorspace",
                "gray",
                "-depth",
                "8",
                "-define",
                "png:color-type=0",
                "-define",
                "png:bit-depth=8",
                path,
            )
        return path

    def pause_bars(self, box, path):
        """Two whole-pixel bars at the box edges; the gap takes the remaining
        columns (2 px at even widths, 3 px at odd ones). The font's pause glyph
        lands on fractional columns at status bar sizes."""
        s = 4
        width, height = box
        bar = (width - 2) // 2
        draws = []
        for x in (0, width - bar):
            draws += [
                "-draw",
                f"roundrectangle {x * s},0 {(x + bar) * s - 1},{height * s - 1} 2,2",
            ]
        self.magick(
            "-size",
            f"{width * s}x{height * s}",
            "xc:black",
            "-fill",
            "white",
            *draws,
            "-filter",
            "Box",
            "-resize",
            f"{width}x{height}!",
            *GRAY8,
            path,
        )

    def tv_outline(self, box, path):
        """A 1 px screen outline with a 1 px gap above a stand line."""
        s = 4
        width, height = box
        screen = height - 2
        # The stand matches the width's parity so it centers on whole pixels.
        stand = width // 2 - 1
        stand -= (width - stand) % 2
        x0 = (width - stand) // 2
        self.magick(
            "-size",
            f"{width * s}x{height * s}",
            "xc:black",
            "-fill",
            "white",
            "-draw",
            f"roundrectangle 0,0 {width * s - 1},{screen * s - 1} 4,4",
            "-fill",
            "black",
            "-draw",
            f"rectangle {s},{s} {(width - 1) * s - 1},{(screen - 1) * s - 1}",
            "-fill",
            "white",
            "-draw",
            f"rectangle {x0 * s},{(height - 1) * s} {(x0 + stand) * s - 1},{height * s - 1}",
            "-filter",
            "Box",
            "-resize",
            f"{width}x{height}!",
            *GRAY8,
            path,
        )

    def folder_outline(self, box, path):
        """A 1 px outline of a tab over a body, with the flap line under the
        tab; rectangles are inclusive pixels drawn at 4x."""
        s = 4
        width, height = box
        tab = width * 2 // 5

        def rect(x0, y0, x1, y1, radius=0):
            corners = f"{x0 * s},{y0 * s} {(x1 + 1) * s - 1},{(y1 + 1) * s - 1}"
            if not radius:
                return f"rectangle {corners}"
            return f"roundrectangle {corners} {radius * s},{radius * s}"

        self.magick(
            "-size",
            f"{width * s}x{height * s}",
            "xc:black",
            "-fill",
            "white",
            "-draw",
            rect(0, 0, tab, 4, 1),
            "-draw",
            rect(0, 2, width - 1, height - 1, 2),
            "-fill",
            "black",
            "-draw",
            rect(1, 1, tab - 1, 3),
            "-draw",
            rect(1, 3, width - 2, height - 2, 1),
            "-fill",
            "white",
            "-draw",
            rect(0, 4, width - 1, 4),
            "-filter",
            "Box",
            "-resize",
            f"{width}x{height}!",
            *GRAY8,
            path,
        )

    def lock(self, box, path):
        """A solid body under a 1 px shackle about half the body's width."""
        s = 4
        width, height = box
        body = height // 2
        # The shackle matches the width's parity so it centers on whole pixels.
        shackle = width // 2 + 1
        shackle -= (width - shackle) % 2
        x0 = (width - shackle) // 2
        x1 = x0 + shackle
        self.magick(
            "-size",
            f"{width * s}x{height * s}",
            "xc:black",
            "-fill",
            "white",
            "-draw",
            f"roundrectangle {x0 * s},0 {x1 * s - 1},{(body + 2) * s - 1}"
            f" {shackle * s / 2},{shackle * s / 2}",
            "-fill",
            "black",
            "-draw",
            f"roundrectangle {(x0 + 1) * s},{s} {(x1 - 1) * s - 1},{(body + 2) * s - 1}"
            f" {(shackle - 2) * s / 2},{(shackle - 2) * s / 2}",
            "-fill",
            "white",
            "-draw",
            f"roundrectangle 0,{body * s} {width * s - 1},{height * s - 1} {s},{s}",
            "-filter",
            "Box",
            "-resize",
            f"{width}x{height}!",
            *GRAY8,
            path,
        )

    def bell_outline(self, box, path):
        """A 1 px dome over a full-width rim, a knob above and a clapper below,
        on whole pixels; the knob and clapper follow the width's parity."""
        s = 4
        width, height = box
        rim = height - 3
        inset = round(width * 0.22)
        radius = (width - 2 * inset) // 2
        knob = 2 - width % 2
        clapper = 4 - width % 2
        middle = (width - knob) // 2

        def rect(x0, y0, x1, y1, radius=0):
            corners = f"{x0 * s},{y0 * s} {(x1 + 1) * s - 1},{(y1 + 1) * s - 1}"
            if not radius:
                return f"rectangle {corners}"
            return f"roundrectangle {corners} {radius * s},{radius * s}"

        self.magick(
            "-size",
            f"{width * s}x{height * s}",
            "xc:black",
            "-fill",
            "white",
            "-draw",
            rect(inset, 1, width - 1 - inset, rim + radius, radius),
            "-fill",
            "black",
            "-draw",
            rect(inset + 1, 2, width - 2 - inset, rim + radius, radius - 1),
            "-draw",
            rect(0, rim + 1, width - 1, height - 1),
            "-fill",
            "white",
            "-draw",
            rect(0, rim, width - 1, rim),
            "-draw",
            rect(middle, 0, middle + knob - 1, 0),
            "-draw",
            rect(
                (width - clapper) // 2,
                height - 1,
                (width + clapper) // 2 - 1,
                height - 1,
            ),
            "-filter",
            "Box",
            "-resize",
            f"{width}x{height}!",
            *GRAY8,
            path,
        )

    def place(self, name, glyph_png, box, center, relative):
        """Compose coverage on a black canvas with the glyph centered at center."""
        width, height = self.size(name)
        x, y = center[0] - box[0] // 2, center[1] - box[1] // 2
        path = self.out / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        self.magick(
            "-size",
            f"{width}x{height}",
            "xc:black",
            glyph_png,
            "-geometry",
            f"+{x}+{y}",
            "-composite",
            "-colorspace",
            "gray",
            "-depth",
            "8",
            "-define",
            "png:color-type=0",
            "-define",
            "png:bit-depth=8",
            path,
        )
        return path

    def coverage_art(self, name, glyph, folder, box=None, center=None, crisp=False):
        width, height = self.size(name)
        if box is None:
            base, center = FRAMES[width, height]
            box = self.optical_box(name, glyph, base)
        center = center or (width // 2, height // 2)
        coverage = self.glyph(glyph, box, crisp)
        self.sizes[name] = (glyph, box, self.image_size(self.trimmed(coverage)))
        return self.place(name, coverage, box, center, f"art/{folder}/{name}.png")

    def stacked_art(self, name, parts):
        """Coverage of several glyphs, each centered on its own point."""
        width, height = self.size(name)
        layers = []
        for glyph, box, center in parts:
            x, y = center[0] - box[0] // 2, center[1] - box[1] // 2
            layers += [
                self.glyph(glyph, box),
                "-geometry",
                f"+{x}+{y}",
                "-compose",
                "Lighten",
                "-composite",
            ]
        path = self.out / f"art/tint/{name}.png"
        self.magick("-size", f"{width}x{height}", "xc:black", *layers, *GRAY8, path)
        return path

    def trimmed(self, coverage):
        path = coverage.with_name(coverage.stem + "-bbox.png")
        if not path.exists():
            self.magick(coverage, "-trim", "+repage", path)
        return path

    def colored(self, name, coverage, color, relative):
        """Solid color with the coverage as alpha."""
        width, height = self.size(name)
        path = self.out / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        self.magick(
            "-size",
            f"{width}x{height}",
            f"xc:{color}",
            coverage,
            "-alpha",
            "off",
            "-compose",
            "CopyOpacity",
            "-composite",
            f"PNG32:{path}",
        )
        return path

    def entry(self, appearance, name, relative=None, tint=None):
        """tint is None or the color role the device draws the bitmap in."""
        item = {"name": name}
        if relative:
            item["file"] = relative
        if tint:
            item["tint"] = tint
        self.entries[appearance].append(item)

    def shared(self, name, relative, tint="icon_tint"):
        for appearance in self.entries:
            self.entry(appearance, name, relative, tint)

    def role(self, appearance, role):
        colors = APPEARANCES[appearance]
        fixed = {
            "white": "#ffffff",
            "black": "#000000",
            "red": RED[appearance],
            "orange": ORANGE[appearance],
            "progress_track": PROGRESS_TRACK[appearance],
            "dark_bar": DARK_BAR,
        }
        return fixed.get(role) or colors[role]

    def render_tinted(self):
        for name, glyph in SETTINGS_MASKS.items():
            self.coverage_art(name, glyph, "mask")
            self.shared(name, f"art/mask/{name}.png")
        for name in TINT_ONLY:
            self.shared(name, None)
        self.render_daisy()
        self.render_hands()
        self.render_ipod()
        self.render_language()
        self.render_limit_arrow()
        self.render_dot_map()
        self.render_caption_corners()
        name, glyph, box, center = CHARGING_BOLT
        path = self.coverage_art(name, glyph, "mask", box, center)
        self.magick(path, "-fx", "round(u*15)/15", *GRAY8, path)
        self.shared(name, f"art/mask/{name}.png", "text")
        self.render_plug()
        for name, (glyph, box, center) in PLAIN_MASKS.items():
            path = self.coverage_art(name, glyph, "mask", box, center)
            # 4-bit masks hold 16 gray levels.
            self.magick(path, "-fx", "round(u*15)/15", *GRAY8, path)
            self.shared(name, f"art/mask/{name}.png", None)
        for name, glyph in (*PROXIES.items(), *EXTRAS.items()):
            self.coverage_art(name, glyph, "tint")
            self.shared(name, f"art/tint/{name}.png")
        for name, (glyph, box, role, crisp) in SMALL_GLYPHS.items():
            if role in style.TINT_ROLES:
                center = GLYPH_CENTERS.get(name)
                self.coverage_art(name, glyph, "tint", box, center, crisp)
                self.shared(name, f"art/tint/{name}.png", role)
        for name, role in SOLID.items():
            if role in style.TINT_ROLES:
                width, height = self.size(name)
                relative = f"art/tint/{name}.png"
                self.magick(
                    "-size",
                    f"{width}x{height}",
                    "xc:white",
                    *GRAY8,
                    self.out / relative,
                )
                self.shared(name, relative, role)
        for name, (role, parts) in STACKED_GLYPHS.items():
            self.stacked_art(name, parts)
            self.shared(name, f"art/tint/{name}.png", role)
        self.render_tinted_batteries()
        self.render_center_cap()
        self.render_sliced(None, "art/tint")
        self.render_progress(None, "art/tint")
        self.render_knobs(None, "art/tint")

    def render_static(self, appearance):
        folder = f"art/{appearance}"
        for name, (glyph, box, role, crisp) in SMALL_GLYPHS.items():
            if role in style.TINT_ROLES:
                continue
            center = GLYPH_CENTERS.get(name)
            coverage = self.coverage_art(name, glyph, "work", box, center, crisp)
            self.colored(
                name, coverage, self.role(appearance, role), f"{folder}/{name}.png"
            )
            self.entry(appearance, name, f"{folder}/{name}.png")
        for name, role in SOLID.items():
            if role in style.TINT_ROLES:
                continue
            width, height = self.size(name)
            path = self.out / folder / f"{name}.png"
            self.magick(
                "-size",
                f"{width}x{height}",
                f"xc:{self.role(appearance, role)}",
                f"PNG24:{path}",
            )
            self.entry(appearance, name, f"{folder}/{name}.png")
        name, separator = CLOCK_ROW
        width, height = self.size(name)
        self.magick(
            "-size",
            f"{width}x{height}",
            f"xc:{self.role(appearance, 'background')}",
            "-fill",
            separator[appearance],
            "-draw",
            f"rectangle 0,{height - 1} {width - 1},{height - 1}",
            f"PNG24:{self.out / folder / name}.png",
        )
        self.entry(appearance, name, f"{folder}/{name}.png")
        for name in TRANSPARENT:
            width, height = self.size(name)
            path = self.out / folder / f"{name}.png"
            self.magick("-size", f"{width}x{height}", "xc:none", f"PNG32:{path}")
            self.entry(appearance, name, f"{folder}/{name}.png")
        for name, glyph in BAKED.items():
            width, height = self.size(name)
            coverage = self.coverage_art(name, glyph, "work")
            layer = self.colored(
                name, coverage, self.role(appearance, "muted"), "art/work/layer.png"
            )
            path = self.out / folder / f"{name}.png"
            self.magick(
                "-size",
                f"{width}x{height}",
                f"xc:{PLACEHOLDER[appearance]}",
                layer,
                "-composite",
                f"PNG24:{path}",
            )
            self.entry(appearance, name, f"{folder}/{name}.png")
        self.render_shadow(appearance, folder)
        self.render_clocks(appearance, folder)
        self.render_dials(appearance, folder)
        self.render_stopwatch_button(appearance, folder)
        self.render_sliced(appearance, folder)
        self.render_scrollbar(appearance, folder)
        self.render_batteries(appearance, folder)
        self.render_progress(appearance, folder)
        self.render_knobs(appearance, folder)
        self.render_badges(appearance, folder)
        self.render_charging(appearance, folder)
        self.render_capacity(appearance, folder)
        self.render_date_time(appearance, folder)
        self.render_avatars(appearance, folder)
        self.render_pin(appearance, folder)
        self.render_genius_slider(appearance, folder)
        for name in PANEL_BARS:
            width, height = self.size(name)
            path = self.out / folder / f"{name}.png"
            self.magick(
                "-size",
                f"{width}x{height}",
                f"xc:{rgba(*PANEL[appearance])}",
                f"PNG32:{path}",
            )
            self.entry(appearance, name, f"{folder}/{name}.png")
        name, colors = BACKSIDE_HEADER
        fill, separator = colors[appearance]
        width, height = self.size(name)
        path = self.out / folder / f"{name}.png"
        self.magick(
            "-size",
            f"{width}x{height}",
            f"xc:{fill}",
            "-fill",
            separator,
            "-draw",
            f"rectangle 0,{height - 1} {width - 1},{height - 1}",
            f"PNG24:{path}",
        )
        self.entry(appearance, name, f"{folder}/{name}.png")

    def render_shadow(self, appearance, folder):
        """One eased alpha column per pixel, repeated down the bitmap."""
        name = "PreviewArea_Shadow_Image"
        width, height = self.size(name)
        path = self.out / folder / f"{name}.png"
        points = []
        for x in range(width):
            alpha = SHADOW[appearance] * (1 - x / width) ** SHADOW_EASE
            points += ["-fill", f"rgba(0,0,0,{alpha:.4f})", "-draw", f"point {x},0"]
        self.magick(
            "-size",
            f"{width}x1",
            "xc:none",
            *points,
            "-scale",
            f"{width}x{height}!",
            f"PNG32:{path}",
        )
        self.entry(appearance, name, f"{folder}/{name}.png")

    def render_clocks(self, appearance, folder):
        import math

        s = 4
        for name, (face, ticks, radius) in CLOCKS.items():
            width, height = self.size(name)
            path = self.out / folder / f"{name}.png"
            cx, cy = width / 2, min(width, height) / 2
            face_color = NIGHT_FACE if face == "night" else CLOCK_FACE
            draws = [
                "-fill",
                face_color,
                "-draw",
                f"circle {cx * s},{cy * s} {(cx + radius) * s},{cy * s}",
                "-fill",
                self.role(appearance, ticks),
            ]
            for hour in range(12):
                angle = math.radians(hour * 30)
                major = hour % 3 == 0
                outer = radius - radius * 0.08
                inner = outer - radius * (0.125 if major else 0.0625)
                half = (max(1.5, radius / 24) if major else max(1.0, radius / 40)) / 2
                dx, dy = math.sin(angle), -math.cos(angle)
                px, py = -dy * half, dx * half
                points = [
                    (cx + dx * outer + px, cy + dy * outer + py),
                    (cx + dx * outer - px, cy + dy * outer - py),
                    (cx + dx * inner - px, cy + dy * inner - py),
                    (cx + dx * inner + px, cy + dy * inner + py),
                ]
                polygon = " ".join(f"{x * s},{y * s}" for x, y in points)
                draws += ["-draw", f"polygon {polygon}"]
            self.magick(
                "-size",
                f"{width * s}x{height * s}",
                "xc:none",
                *draws,
                "-filter",
                "Box",
                "-resize",
                f"{width}x{height}!",
                f"PNG32:{path}",
            )
            self.entry(appearance, name, f"{folder}/{name}.png")

    def render_dials(self, appearance, folder):
        import math

        s = 4

        def point(x, y):
            # Draw coordinates address pixel centers, hence the half pixel.
            return f"{x * s - 0.5},{y * s - 0.5}"

        for name, dial in DIALS.items():
            width, height = self.size(name)
            path = self.out / folder / f"{name}.png"
            cx, cy = dial["center"]
            radius = dial["radius"]
            draws = self.dial_body(dial["body"], (cx, cy), appearance, point)
            if dial["halo"]:
                halo, rims = dial["halo"]
                draws += [
                    "-fill",
                    self.role(appearance, "background"),
                    "-draw",
                    f"circle {point(cx, cy)} {point(cx + radius + halo, cy)}",
                    "-fill",
                    rims[appearance],
                    "-draw",
                    f"circle {point(cx, cy)} {point(cx + radius + 1, cy)}",
                ]
            draws += [
                "-fill",
                CLOCK_FACE,
                "-draw",
                f"circle {point(cx, cy)} {point(cx + radius, cy)}",
            ]
            count, every, outer, major, minor = dial["ticks"]
            for index in range(count):
                angle = math.radians(index * 360 / count)
                length, thickness, color = major if index % every == 0 else minor
                dx, dy = math.sin(angle), -math.cos(angle)
                px, py = -dy * thickness / 2, dx * thickness / 2
                inner = outer - length
                corners = [
                    (cx + dx * outer + px, cy + dy * outer + py),
                    (cx + dx * outer - px, cy + dy * outer - py),
                    (cx + dx * inner - px, cy + dy * inner - py),
                    (cx + dx * inner + px, cy + dy * inner + py),
                ]
                polygon = " ".join(point(x, y) for x, y in corners)
                draws += ["-fill", color, "-draw", f"polygon {polygon}"]
            if dial["ring"]:
                (rx, ry), ring = dial["ring"]
                draws += [
                    "-fill",
                    "none",
                    "-stroke",
                    "#8e8e93",
                    "-strokewidth",
                    s,
                    "-draw",
                    f"circle {point(rx, ry)} {point(rx + ring, ry)}",
                    "-stroke",
                    "none",
                ]
            inner = outer - major[0] - dial["gap"]
            for angle, label in dial["numerals"]:
                numeral = self.numeral(label, dial["size"] * s)
                w, h = self.image_size(numeral)
                dx = math.sin(math.radians(angle))
                dy = -math.cos(math.radians(angle))
                # Pull the ink box in until its nearest edge meets the gap.
                reach = inner - (abs(dx) * w / 2 + abs(dy) * h / 2) / s
                x = round((cx + dx * reach) * s - w / 2)
                y = round((cy + dy * reach) * s - h / 2)
                draws += [numeral, "-geometry", f"+{x}+{y}", "-composite"]
            self.magick(
                "-size",
                f"{width * s}x{height * s}",
                "xc:none",
                *draws,
                "-filter",
                "Box",
                "-resize",
                f"{width}x{height}!",
                f"PNG32:{path}",
            )
            self.entry(appearance, name, f"{folder}/{name}.png")

    def dial_body(self, body, center, appearance, point):
        """Draw arguments for the case behind a dial face."""
        import math

        if not body:
            return []
        s = 4
        cx, cy = center

        def bar(angle, start, end, width):
            dx, dy = math.sin(math.radians(angle)), -math.cos(math.radians(angle))
            px, py = -dy * width / 2, dx * width / 2
            corners = [
                (cx + dx * start + px, cy + dy * start + py),
                (cx + dx * end + px, cy + dy * end + py),
                (cx + dx * end - px, cy + dy * end - py),
                (cx + dx * start - px, cy + dy * start - py),
            ]
            return "polygon " + " ".join(point(x, y) for x, y in corners)

        def box(x0, y0, x1, y1):
            # Inclusive pixel rectangles on whole 4x pixels.
            return f"{x0 * s},{y0 * s} {(x1 + 1) * s - 1},{(y1 + 1) * s - 1}"

        (crown, corner) = body["crown"]
        angle, stem, cap = body["pusher"]
        return [
            "-fill",
            body["colors"][appearance],
            "-draw",
            f"rectangle {box(*body['stem'])}",
            "-draw",
            f"roundrectangle {box(*crown)} {corner * s},{corner * s}",
            "-draw",
            bar(angle, *stem),
            "-draw",
            bar(angle, *cap),
            "-draw",
            f"circle {point(cx, cy)} {point(cx + body['bezel'], cy)}",
        ]

    def numeral(self, label, pointsize):
        """A dial label in the text font, trimmed to its ink."""
        path = self.work / f"numeral-{label}-{pointsize}.png"
        if not path.exists():
            self.magick(
                "-background",
                "none",
                "-fill",
                DIAL_TEXT,
                "-font",
                self.text_font,
                "-pointsize",
                pointsize,
                f"label:{label}",
                "-trim",
                "+repage",
                path,
            )
        return path

    def render_stopwatch_button(self, appearance, folder):
        s = 4
        name, inset, radius, play, bar, bar_gap, gap = STOPWATCH_BUTTON
        width, height = self.size(name)
        path = self.out / folder / f"{name}.png"
        text = self.role(appearance, "text")
        play_layer = self.work / f"{name}-play-{appearance}.png"
        self.magick(
            "-size",
            f"{play[0]}x{play[1]}",
            f"xc:{text}",
            self.glyph("play_fill", play),
            "-alpha",
            "off",
            "-compose",
            "CopyOpacity",
            "-composite",
            f"PNG32:{play_layer}",
        )
        total = play[0] + gap + 2 * bar[0] + bar_gap
        x = (width - total) // 2
        y = (height - play[1]) // 2
        bars_x = x + play[0] + gap
        bars_y = (height - bar[1]) // 2
        draws = ["-fill", text]
        for left in (bars_x, bars_x + bar[0] + bar_gap):
            draws += [
                "-draw",
                f"rectangle {left},{bars_y} {left + bar[0] - 1},{bars_y + bar[1] - 1}",
            ]
        self.magick(
            "-size",
            f"{width * s}x{height * s}",
            "xc:none",
            "-fill",
            CLOCK_FACE if appearance == "light" else WELL[appearance],
            "-draw",
            f"roundrectangle {inset * s},{inset * s} {(width - inset) * s - 1},"
            f"{(height - inset) * s - 1} {radius * s},{radius * s}",
            "-filter",
            "Box",
            "-resize",
            f"{width}x{height}!",
            "+antialias",
            *draws,
            play_layer,
            "-geometry",
            f"+{x}+{y}",
            "-compose",
            "Over",
            "-composite",
            f"PNG32:{path}",
        )
        self.entry(appearance, name, f"{folder}/{name}.png")

    def render_charging(self, appearance, folder):
        text = self.role(appearance, "text")
        for (level, levels), pieces in CHARGING_PIECES.items():
            strip = self.work / f"charging-{level}-{levels}.png"
            self.battery(
                strip,
                CHARGING_STRIP,
                CHARGING_BATTERY,
                text,
                level,
                levels,
                GREEN[appearance],
                True,
            )
            for suffix, x in pieces:
                name = f"ChargingMode_Image_{suffix}"
                width, height = self.size(name)
                path = self.out / folder / f"{name}.png"
                self.magick(
                    strip,
                    "-crop",
                    f"{width}x{height}+{x}+0",
                    "+repage",
                    f"PNG32:{path}",
                )
                self.entry(appearance, name, f"{folder}/{name}.png")

    def band(self, path, size, rows, radius, ends, color):
        """A horizontal band over rows, rounded only where it ends in the frame."""
        s = 4
        width, height = size
        start, end = ends
        x0 = start if start is not None else -2 * radius
        x1 = end if end is not None else width + 2 * radius - 1
        self.magick(
            "-size",
            f"{width * s}x{height * s}",
            "xc:none",
            "-fill",
            color,
            "-draw",
            f"roundrectangle {x0 * s},{rows[0] * s} {(x1 + 1) * s - 1},"
            f"{(rows[1] + 1) * s - 1} {radius * s},{radius * s}",
            "-filter",
            "Box",
            "-resize",
            f"{width}x{height}!",
            f"PNG32:{path}",
        )

    def render_capacity(self, appearance, folder):
        rows, radius, ends = CAPACITY_DETAILED
        track = CAPACITY_TRACK[appearance]
        pieces = []
        for part, colors in {"Empty": track, **CAPACITY_COLORS}.items():
            color = colors if isinstance(colors, str) else colors[appearance]
            for side, end in ends.items():
                pieces.append((f"CapacityView_Detailed_{part}{side}_Image", end, color))
        for side, name in (
            ("Left", "LeftCap"),
            ("Center", "Fill"),
            ("Right", "RightCap"),
        ):
            pieces.append((f"Settings_About_Capacity_{name}_Image", ends[side], track))
        for name, end, color in pieces:
            path = self.out / folder / f"{name}.png"
            self.band(path, self.size(name), rows, radius, end, color)
            self.entry(appearance, name, f"{folder}/{name}.png")
        # The edge is drawn over the segments: a band in the page color, so the
        # segments read as separated by a gap.
        name = "CapacityView_Detailed_Edge_Image"
        path = self.out / folder / f"{name}.png"
        page = self.role(appearance, "background")
        self.band(path, self.size(name), rows, radius, (None, None), page)
        self.entry(appearance, name, f"{folder}/{name}.png")
        inset, corner = CAPACITY_LEGEND
        for part, colors in CAPACITY_COLORS.items():
            name = f"Settings_About_Capacity_{part}Legend_Image"
            width, height = self.size(name)
            path = self.out / folder / f"{name}.png"
            s = 4
            self.magick(
                "-size",
                f"{width * s}x{height * s}",
                "xc:none",
                "-fill",
                colors[appearance],
                "-draw",
                f"roundrectangle {inset * s},{inset * s} {(width - inset) * s - 1},"
                f"{(height - inset) * s - 1} {corner * s},{corner * s}",
                "-filter",
                "Box",
                "-resize",
                f"{width}x{height}!",
                f"PNG32:{path}",
            )
            self.entry(appearance, name, f"{folder}/{name}.png")
        rows, radius = CAPACITY_SIMPLE
        for part in ("Empty", "Fill"):
            for side in ("Left", "Center", "Right"):
                name = f"CapacityView_Simple_{part}{side}_Image"
                size = self.size(name)
                end = {
                    "Left": (0, None),
                    "Center": (None, None),
                    "Right": (None, size[0] - 1),
                }[side]
                if part == "Empty":
                    path = self.out / folder / f"{name}.png"
                    self.band(path, size, rows, radius, end, track)
                    self.entry(appearance, name, f"{folder}/{name}.png")
                elif appearance == "light":
                    # The used part follows the accent: shared coverage.
                    path = self.out / "art/tint" / f"{name}.png"
                    self.band(path, size, rows, radius, end, "#ffffff")
                    self.shared(name, f"art/tint/{name}.png", "accent_fill")

    def rounded(self, rect, radius):
        """A -draw primitive for an inclusive pixel rectangle at 4x."""
        s = 4
        x0, y0, x1, y1 = rect
        corners = f"{x0 * s},{y0 * s} {(x1 + 1) * s - 1},{(y1 + 1) * s - 1}"
        # ImageMagick skips round rectangles with a zero radius.
        if not radius:
            return f"rectangle {corners}"
        return f"roundrectangle {corners} {radius * s},{radius * s}"

    def shapes(self, path, size, draws):
        """Fill each (color, primitive) at 4x and box filter to size."""
        s = 4
        width, height = size
        layers = []
        for color, primitive in draws:
            layers += ["-fill", color, "-draw", primitive]
        self.magick(
            "-size",
            f"{width * s}x{height * s}",
            "xc:none",
            *layers,
            "-filter",
            "Box",
            "-resize",
            f"{width}x{height}!",
            f"PNG32:{path}",
        )

    def render_date_time(self, appearance, folder):
        name, (*card, radius), header = CALENDAR_CARD
        x0, y0, x1, y1 = card
        path = self.out / folder / f"{name}.png"
        self.shapes(
            path,
            self.size(name),
            [
                (CARD[appearance], self.rounded(card, radius)),
                (RED[appearance], self.rounded((x0, y0, x1, header + radius), radius)),
                (
                    CARD[appearance],
                    f"rectangle {x0 * 4},{(header + 1) * 4} "
                    f"{(x1 + 1) * 4 - 1},{(header + radius + 1) * 4 - 1}",
                ),
            ],
        )
        self.entry(appearance, name, f"{folder}/{name}.png")
        for name, ((*tray, radius), wells) in PICKER_TRAYS.items():
            path = self.out / folder / f"{name}.png"
            draws = [(rgba(*PANEL[appearance]), self.rounded(tray, radius))]
            draws += [
                (WELL[appearance], self.rounded(well, PICKER_RADIUS)) for well in wells
            ]
            self.shapes(path, self.size(name), draws)
            self.entry(appearance, name, f"{folder}/{name}.png")
        for name, well in PICKER_WELLS.items():
            path = self.out / folder / f"{name}.png"
            draws = [(WELL[appearance], self.rounded(well, PICKER_RADIUS))]
            self.shapes(path, self.size(name), draws)
            self.entry(appearance, name, f"{folder}/{name}.png")
        if appearance == "light":
            for name, frame in PICKER_HIGHLIGHTS.items():
                width, height = self.size(name)
                path = self.out / "art/tint" / f"{name}.png"
                frame = frame or (0, 0, width - 1, height - 1)
                self.shapes(
                    path,
                    (width, height),
                    [("#ffffff", self.rounded(frame, PICKER_RADIUS))],
                )
                self.shared(name, f"art/tint/{name}.png", "selection")
        radius, tiles, rings = CALENDAR_CELLS
        width, height = self.size("Calendar_Active_Cell")
        frame = (0, 0, width - 1, height - 1)
        inner = (1, 1, width - 2, height - 2)
        name = "Calendar_Active_Cell"
        path = self.out / folder / f"{name}.png"
        self.shapes(
            path, (width, height), [(tiles[appearance], self.rounded(frame, radius))]
        )
        self.entry(appearance, name, f"{folder}/{name}.png")
        name = "Calendar_Inactive_Cell"
        path = self.out / folder / f"{name}.png"
        self.shapes(
            path, (width, height), [(rings[appearance], self.rounded(frame, radius))]
        )
        hole = self.out / "art/work/calendar-hole.png"
        self.shapes(
            hole, (width, height), [("#ffffff", self.rounded(inner, radius - 1))]
        )
        self.magick(path, hole, "-compose", "DstOut", "-composite", f"PNG32:{path}")
        self.entry(appearance, name, f"{folder}/{name}.png")
        if appearance == "light":
            name = "Calendar_Hilite_Cell"
            path = self.out / "art/tint" / f"{name}.png"
            self.shapes(
                path, (width, height), [("#ffffff", self.rounded(frame, radius))]
            )
            self.shared(name, f"art/tint/{name}.png", "selection")

    def render_genius_slider(self, appearance, folder):
        top, bottom = GENIUS_SLIDER["rows"]
        radius = GENIUS_SLIDER["radius"]
        name = "Genius_Slider_Image"
        width, height = self.size(name)
        x0, x1 = GENIUS_SLIDER["knob"]
        path = self.out / folder / f"{name}.png"
        self.shapes(
            path,
            (width, height),
            [(WELL[appearance], self.rounded((x0, top, x1, bottom), radius))],
        )
        box = GENIUS_SLIDER["arrow"]
        arrow = self.colored(
            name,
            self.place(
                name,
                self.glyph("arrow_right", box),
                box,
                ((x0 + x1 + 1) // 2, (top + bottom + 1) // 2),
                "art/work/genius-arrow.png",
            ),
            self.role(appearance, "text"),
            "art/work/genius-arrow-color.png",
        )
        self.magick(path, arrow, "-compose", "Over", "-composite", f"PNG32:{path}")
        self.entry(appearance, name, f"{folder}/{name}.png")
        if appearance != "light":
            return
        for name, (start, end) in (
            ("Genius_Slider_Endcap_Image", (1, 1 + 2 * radius)),
            ("Genius_Slider_Fill_Image", (0, 0)),
        ):
            width, height = self.size(name)
            relative = f"art/tint/{name}.png"
            self.shapes(
                self.out / relative,
                (width, height),
                [
                    (
                        "#ffffff",
                        self.rounded(
                            (start, top, end, bottom), radius if end > start else 0
                        ),
                    )
                ],
            )
            self.shared(name, relative, "accent_fill")

    def render_caption_corners(self):
        s = 4
        for name, (cx, cy) in CAPTION_CORNERS.items():
            width, height = self.size(name)
            x, y = cx * width * s - 0.5, cy * height * s - 0.5
            relative = f"art/mask/{name}.png"
            self.magick(
                "-size",
                f"{width * s}x{height * s}",
                "xc:black",
                "-fill",
                "white",
                "-draw",
                f"circle {x},{y} {x + width * s},{y}",
                "-filter",
                "Box",
                "-resize",
                f"{width}x{height}!",
                "-fx",
                "round(u*15)/15",
                *GRAY8,
                self.out / relative,
            )
            self.shared(name, relative, None)

    def render_dot_map(self):
        name = DOT_MAP["name"]
        width, height = self.size(name)
        pitch, dot = DOT_MAP["pitch"], DOT_MAP["dot"]
        inset = (pitch - dot) // 2
        level = DOT_MAP["level"] * 17
        primitives = []
        columns = width // pitch
        for row, bits in enumerate(DOT_MAP["rows"], DOT_MAP["top"]):
            value = int(bits, 16)
            for column in range(columns):
                if value >> (columns - 1 - column) & 1:
                    x = column * pitch + inset
                    y = row * pitch + inset
                    primitives.append(f"rectangle {x},{y} {x + dot - 1},{y + dot - 1}")
        draw = self.work / "dot-map.mvg"
        draw.write_text("\n".join(primitives))
        relative = f"art/tint/{name}.png"
        self.magick(
            "-size",
            f"{width}x{height}",
            "xc:black",
            "+antialias",
            "-fill",
            f"rgb({level},{level},{level})",
            "-draw",
            f"@{draw}",
            *GRAY8,
            self.out / relative,
        )
        self.shared(name, relative, "text")

    def render_pin(self, appearance, folder):
        s = 4
        name, (tip_x, tip_y), diameter = PIN
        width, height = self.size(name)
        cx = tip_x + 0.5
        radius = diameter / 2
        path = self.out / folder / f"{name}.png"
        self.shapes(
            path,
            (width, height),
            [
                (
                    self.role(appearance, "muted"),
                    self.rounded((tip_x, diameter - 1, tip_x, tip_y), 0),
                ),
                (
                    RED[appearance],
                    f"circle {cx * s - 0.5},{radius * s - 0.5} "
                    f"{(cx + radius) * s - 0.5},{radius * s - 0.5}",
                ),
            ],
        )
        self.entry(appearance, name, f"{folder}/{name}.png")

    def render_limit_arrow(self):
        s = 4
        width, height = self.size(LIMIT_ARROW)
        relative = f"art/tint/{LIMIT_ARROW}.png"
        self.magick(
            "-size",
            f"{width * s}x{height * s}",
            "xc:black",
            "-fill",
            "white",
            "-draw",
            # Vertices on pixel edges: draw coordinates address pixel centers.
            f"polygon -0.5,{height * s - 0.5} {width * s - 0.5},{height * s - 0.5}"
            f" {width * s / 2 - 0.5},-0.5",
            "-filter",
            "Box",
            "-resize",
            f"{width}x{height}!",
            *GRAY8,
            self.out / relative,
        )
        self.shared(LIMIT_ARROW, relative, "text")

    def render_language(self):
        name = LANGUAGE["name"]
        width, height = self.size(name)
        caps = []
        for _, size, _, word in LANGUAGE["lines"]:
            # Cap height from the ink of a capital at this size.
            probe = self.work / f"cap-{size}.png"
            if not probe.exists():
                self.magick(
                    "-background",
                    "black",
                    "-fill",
                    "white",
                    "-font",
                    self.text_font,
                    "-pointsize",
                    size,
                    "label:H",
                    "-trim",
                    "+repage",
                    probe,
                )
            caps.append(self.image_size(probe)[1])
        baselines = []
        baseline = 0
        for cap in caps:
            baseline += (LANGUAGE["gap"] if baselines else 0) + cap
            baselines.append(baseline)
        # The block runs from the first cap top (0) to the last baseline.
        top = LANGUAGE["center"] - baselines[-1] // 2
        draws = []
        for (indent, size, opacity, word), y in zip(LANGUAGE["lines"], baselines):
            level = round(opacity * 255)
            draws += [
                "-fill",
                f"rgb({level},{level},{level})",
                "-pointsize",
                size,
                "-annotate",
                f"+{indent}+{top + y}",
                word,
            ]
        relative = f"art/tint/{name}.png"
        self.magick(
            "-size",
            f"{width}x{height}",
            "xc:black",
            "-font",
            self.text_font,
            *draws,
            *GRAY8,
            self.out / relative,
        )
        self.shared(name, relative, "text")

    def render_avatars(self, appearance, folder):
        s = 4
        circle_color, person_color = AVATAR_COLORS[appearance]
        for name, (diameter, people) in AVATARS.items():
            width, height = self.size(name)
            cx, cy = width / 2, height / 2

            def circle(x, y, radius):
                x, y = x * s - 0.5, y * s - 0.5
                return f"circle {x},{y} {x + radius * s},{y}"

            draws = ["-fill", circle_color, "-draw", circle(cx, cy, diameter / 2)]
            for index, (dx, head, head_r, body, body_r) in enumerate(people):
                x = cx + dx * diameter
                parts = (
                    (cy + head * diameter, head_r),
                    (cy + body * diameter, body_r),
                )
                # A ring of the circle color separates a person from the one
                # behind.
                for color, ring in ((circle_color, 1.5), (person_color, 0)):
                    if ring and not index:
                        continue
                    draws += ["-fill", color]
                    for y, r in parts:
                        draws += ["-draw", circle(x, y, r * diameter + ring)]
            path = self.out / folder / f"{name}.png"
            self.magick(
                "-size",
                f"{width * s}x{height * s}",
                "xc:none",
                *draws,
                "(",
                "-size",
                f"{width * s}x{height * s}",
                "xc:none",
                "-fill",
                "white",
                "-draw",
                circle(cx, cy, diameter / 2),
                ")",
                "-compose",
                "DstIn",
                "-composite",
                "-filter",
                "Box",
                "-resize",
                f"{width}x{height}!",
                f"PNG32:{path}",
            )
            self.entry(appearance, name, f"{folder}/{name}.png")

    def render_plug(self):
        """A plug on its cord, pointing right, from whole-pixel rectangles."""
        s = 4
        name = CHARGING_PLUG
        width, height = self.size(name)
        middle = height // 2
        body = (12, middle - 9, 30, middle + 9)
        relative = f"art/mask/{name}.png"

        def rect(x0, y0, x1, y1, radius=0):
            corners = f"{x0 * s},{y0 * s} {(x1 + 1) * s - 1},{(y1 + 1) * s - 1}"
            if not radius:
                return f"rectangle {corners}"
            return f"roundrectangle {corners} {radius * s},{radius * s}"

        self.magick(
            "-size",
            f"{width * s}x{height * s}",
            "xc:black",
            "-fill",
            "white",
            "-draw",
            rect(1, middle - 1, body[0], middle + 1, 1),
            "-draw",
            rect(*body, 3),
            "-draw",
            rect(body[2], middle - 6, width - 2, middle - 4, 1),
            "-draw",
            rect(body[2], middle + 4, width - 2, middle + 6, 1),
            "-filter",
            "Box",
            "-resize",
            f"{width}x{height}!",
            "-fx",
            "round(u*15)/15",
            *GRAY8,
            self.out / relative,
        )
        self.shared(name, relative, "text")

    def render_ipod(self):
        s = 4
        width, height = self.size(IPOD)
        x = IPOD_CENTER[0] - IPOD_BOX[0] // 2
        y = IPOD_CENTER[1] - IPOD_BOX[1] // 2

        def rect(x0, y0, x1, y1, radius):
            return self.rounded((x0 + x, y0 + y, x1 + x, y1 + y), radius)

        def circle(cx, cy, radius):
            # Draw coordinates address pixel centers, hence the half pixel.
            cx, cy = (cx + x) * s - 0.5, (cy + y) * s - 0.5
            return f"circle {cx},{cy} {cx + radius * s},{cy}"

        x0, y0, x1, y1, radius = IPOD_BODY
        inset = IPOD_STROKE
        cx, cy, outer, inner = IPOD_WHEEL
        coverage = self.work / f"{IPOD}-coverage.png"
        self.magick(
            "-size",
            f"{width * s}x{height * s}",
            "xc:black",
            "-fill",
            "white",
            "-draw",
            rect(x0, y0, x1, y1, radius),
            "-fill",
            "black",
            "-draw",
            rect(x0 + inset, y0 + inset, x1 - inset, y1 - inset, radius - inset),
            "-fill",
            "white",
            "-draw",
            rect(*IPOD_SCREEN),
            "-draw",
            circle(cx, cy, outer),
            "-fill",
            "black",
            "-draw",
            circle(cx, cy, inner),
            "-filter",
            "Box",
            "-resize",
            f"{width}x{height}!",
            *GRAY8,
            coverage,
        )
        relative = f"art/tint/{IPOD}.png"
        (self.out / relative).parent.mkdir(parents=True, exist_ok=True)
        self.colored(IPOD, coverage, "#ffffff", relative)
        self.shared(IPOD, relative)

    def render_daisy(self):
        import math

        s = 4
        length, thickness = DAISY_PETAL
        reach = (length - thickness) / 2
        for name, angle in DAISY.items():
            width, height = self.size(name)
            cx, cy = width / 2, height / 2
            dx = math.sin(math.radians(angle)) * reach
            dy = -math.cos(math.radians(angle)) * reach
            relative = f"art/mask/{name}.png"
            self.magick(
                "-size",
                f"{width * s}x{height * s}",
                "xc:black",
                "-stroke",
                "white",
                "-strokewidth",
                thickness * s,
                "-draw",
                # Draw coordinates address pixel centers, hence the half pixel.
                f"stroke-linecap round line {(cx - dx) * s - 0.5},{(cy - dy) * s - 0.5}"
                f" {(cx + dx) * s - 0.5},{(cy + dy) * s - 0.5}",
                "-filter",
                "Box",
                "-resize",
                f"{width}x{height}!",
                "-fx",
                "round(u*15)/15",
                *GRAY8,
                self.out / relative,
            )
            self.shared(name, relative, None)

    def render_hands(self):
        import math

        s = 4
        for prefix, (frames, stroke, reach, tail, hub) in HANDS.items():
            for index in range(frames):
                name = f"{prefix}{index}"
                width, height = self.size(name)
                cx, cy = width / 2, height / 2
                dx = math.sin(math.radians(index * 3))
                dy = -math.cos(math.radians(index * 3))
                # Round caps add half the stroke beyond each end point.
                tip, back = reach - stroke / 2, tail - stroke / 2
                relative = f"art/mask/{name}.png"
                hub_draw = []
                if hub:
                    hub_draw = [
                        "-stroke",
                        "none",
                        "-fill",
                        "white",
                        "-draw",
                        f"circle {cx * s - 0.5},{cy * s - 0.5}"
                        f" {(cx + hub) * s - 0.5},{cy * s - 0.5}",
                    ]
                self.magick(
                    "-size",
                    f"{width * s}x{height * s}",
                    "xc:black",
                    "-stroke",
                    "white",
                    "-strokewidth",
                    stroke * s,
                    "-draw",
                    # Draw coordinates address pixel centers, hence the half pixel.
                    f"stroke-linecap round line"
                    f" {(cx - dx * back) * s - 0.5},{(cy - dy * back) * s - 0.5}"
                    f" {(cx + dx * tip) * s - 0.5},{(cy + dy * tip) * s - 0.5}",
                    *hub_draw,
                    "-filter",
                    "Box",
                    "-resize",
                    f"{width}x{height}!",
                    # 16 gray levels; the lowest is stroke fuzz beside straight
                    # edges, so it is dropped.
                    "-fx",
                    "u < 1.5/15 ? 0 : round(u*15)/15",
                    *GRAY8,
                    self.out / relative,
                )
                self.shared(name, relative, None)

    def render_center_cap(self):
        """Coverage of the pivot dot; the device colors it in the icon tint."""
        s = 4
        name, radius = CENTER_CAP
        width, height = self.size(name)
        relative = f"art/tint/{name}.png"
        path = self.out / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        cx, cy = width / 2, height / 2
        self.magick(
            "-size",
            f"{width * s}x{height * s}",
            "xc:none",
            "-fill",
            "#ffffff",
            "-draw",
            f"circle {cx * s},{cy * s} {(cx + radius) * s},{cy * s}",
            "-filter",
            "Box",
            "-resize",
            f"{width}x{height}!",
            f"PNG32:{path}",
        )
        self.shared(name, relative)

    def render_sliced(self, appearance, folder):
        """Flat rounded bars cut into their left cap, center column and right cap.
        Without an appearance, renders the role-tinted bars as shared coverage."""
        s = 4
        for prefix, suffixes, role, inset, radius, side in SLICED:
            tinted = role in style.TINT_ROLES
            if tinted != (appearance is None):
                continue
            # Most slices end in _Image; the rental overlay's do not.
            names = [
                name if name in self.assets else name.removesuffix("_Image")
                for name in (f"{prefix}{suffix}_Image" for suffix in suffixes)
            ]
            widths = [self.size(name)[0] for name in names]
            height = self.size(names[0])[1]
            width = sum(widths)
            top, bottom = inset if isinstance(inset, tuple) else (inset, inset)
            left, right = side if isinstance(side, tuple) else (side, side)
            color = (
                "#ffffff"
                if tinted
                else {
                    "well": WELL[appearance],
                    "field": FIELD[appearance],
                    "panel": rgba(*PANEL[appearance]),
                    "small_track": rgba(*SMALL_TRACK),
                    "button": BUTTON,
                    "segment": SEGMENT_WELL[appearance],
                    "overlay": rgba(*OVERLAY),
                }[role]
            )
            whole = self.work / f"{prefix}{appearance or 'tint'}.png"
            self.magick(
                "-size",
                f"{width * s}x{height * s}",
                "xc:none",
                "-fill",
                color,
                "-draw",
                f"roundrectangle {left * s},{top * s}"
                f" {(width - right) * s - 1},{(height - bottom) * s - 1}"
                f" {radius * s},{radius * s}",
                "-filter",
                "Box",
                "-resize",
                f"{width}x{height}!",
                f"PNG32:{whole}",
            )
            for name, x, slice_width in zip(
                names, (0, widths[0], widths[0] + widths[1]), widths
            ):
                path = self.out / folder / f"{name}.png"
                path.parent.mkdir(parents=True, exist_ok=True)
                self.magick(
                    whole,
                    "-crop",
                    f"{slice_width}x{height}+{x}+0",
                    "+repage",
                    f"PNG32:{path}",
                )
                if tinted:
                    self.shared(name, f"{folder}/{name}.png", role)
                else:
                    self.entry(appearance, name, f"{folder}/{name}.png")

    def render_progress(self, appearance, folder):
        """Progress tiles: a band across the full tile width, straight edges
        only. Without an appearance, renders the role-tinted tiles as shared
        coverage."""
        top, bottom = PROGRESS_ROWS
        for name, role in PROGRESS_TILES.items():
            tinted = role in style.TINT_ROLES
            if tinted != (appearance is None):
                continue
            width, height = self.size(name)
            color = "#ffffff" if tinted else self.role(appearance, role)
            path = self.out / folder / f"{name}.png"
            path.parent.mkdir(parents=True, exist_ok=True)
            self.magick(
                "-size",
                f"{width}x{height}",
                "xc:none",
                "+antialias",
                "-fill",
                color,
                "-draw",
                f"rectangle 0,{top} {width - 1},{bottom}",
                f"PNG32:{path}",
            )
            if tinted:
                self.shared(name, f"{folder}/{name}.png", role)
            else:
                self.entry(appearance, name, f"{folder}/{name}.png")

    def render_knobs(self, appearance, folder):
        s = 4
        for name, (cx, cy), radius, role in KNOBS:
            tinted = role in style.TINT_ROLES
            if tinted != (appearance is None):
                continue
            width, height = self.size(name)
            color = "#ffffff" if tinted else self.role(appearance, role)
            path = self.out / folder / f"{name}.png"
            self.magick(
                "-size",
                f"{width * s}x{height * s}",
                "xc:none",
                "-fill",
                color,
                "-draw",
                # Draw coordinates address pixel centers, hence the half pixel.
                f"circle {cx * s - 0.5},{cy * s - 0.5}"
                f" {(cx + radius) * s - 0.5},{cy * s - 0.5}",
                "-filter",
                "Box",
                "-resize",
                f"{width}x{height}!",
                f"PNG32:{path}",
            )
            if tinted:
                self.shared(name, f"{folder}/{name}.png", role)
            else:
                self.entry(appearance, name, f"{folder}/{name}.png")

    def render_badges(self, appearance, folder):
        s = 4
        for name, (alpha, radius, content) in BADGES.items():
            width, height = self.size(name)
            path = self.out / folder / f"{name}.png"
            layers = [
                "-size",
                f"{width * s}x{height * s}",
                "xc:none",
                "-fill",
                rgba("#000000", alpha),
                "-draw",
                f"roundrectangle 0,0 {width * s - 1},{height * s - 1}"
                f" {radius * s},{radius * s}",
                "-filter",
                "Box",
                "-resize",
                f"{width}x{height}!",
            ]
            if content:
                glyph, box = content
                coverage = self.glyph(glyph, box)
                layer = self.work / f"{name}-glyph.png"
                self.magick(
                    "-size",
                    f"{box[0]}x{box[1]}",
                    "xc:white",
                    coverage,
                    "-alpha",
                    "off",
                    "-compose",
                    "CopyOpacity",
                    "-composite",
                    f"PNG32:{layer}",
                )
                x, y = width // 2 - box[0] // 2, height // 2 - box[1] // 2
                layers += [
                    layer,
                    "-geometry",
                    f"+{x}+{y}",
                    "-compose",
                    "Over",
                    "-composite",
                ]
            self.magick(*layers, f"PNG32:{path}")
            self.entry(appearance, name, f"{folder}/{name}.png")

    def render_scrollbar(self, appearance, folder):
        """Each cap is a rounded and a square rectangle drawn over each other, so
        the shape is drawn opaque and its alpha scaled afterwards; drawing in the
        translucent color would double the alpha where the two overlap."""
        color, alpha = SCROLL_INDICATOR[appearance]
        for name, draw in zip(
            SCROLLBAR,
            (
                ["-draw", "roundrectangle 1,0 4,9 2,2", "-draw", "rectangle 1,4 4,9"],
                ["-draw", "rectangle 1,0 4,7"],
                ["-draw", "roundrectangle 1,0 4,8 2,2", "-draw", "rectangle 1,0 4,4"],
            ),
        ):
            width, height = self.size(name)
            path = self.out / folder / f"{name}.png"
            self.magick(
                "-size",
                f"{width}x{height}",
                "xc:none",
                "+antialias",
                "-fill",
                color,
                *draw,
                "-channel",
                "A",
                "-evaluate",
                "multiply",
                alpha,
                "+channel",
                f"PNG32:{path}",
            )
            self.entry(appearance, name, f"{folder}/{name}.png")

    def battery(self, path, size, spec, outline, level, levels, fill, antialias):
        """Battery outline, fill clipped to the charge level, and nub. Antialiased
        variants are drawn at 4x on pixel-aligned geometry and box filtered down,
        so only the corners carry intermediate alpha."""
        width, height = size
        s = 4 if antialias else 1
        canvas = ["-size", f"{width * s}x{height * s}", "xc:none"]
        if not antialias:
            canvas.append("+antialias")

        def shape(x0, y0, x1, y1, radius):
            return (
                f"roundrectangle {x0 * s},{y0 * s} {(x1 + 1) * s - 1},{(y1 + 1) * s - 1}"
                f" {radius * s},{radius * s}"
            )

        x0, y0, x1, y1, radius = spec["body"]
        stroke = spec["stroke"]
        layers = [
            *canvas,
            "(",
            *canvas,
            "-fill",
            rgba(outline, spec["outline_alpha"]),
            "-draw",
            shape(x0, y0, x1, y1, radius),
            "(",
            *canvas,
            "-fill",
            "white",
            "-draw",
            shape(
                x0 + stroke,
                y0 + stroke,
                x1 - stroke,
                y1 - stroke,
                max(radius - stroke, 0),
            ),
            ")",
            "-compose",
            "DstOut",
            "-composite",
            ")",
            "-compose",
            "Over",
            "-composite",
        ]
        fx0, fy0, fx1, fy1, fr = spec["fill"]
        layers += [
            "-fill",
            rgba(outline, spec["nub_alpha"]),
            "-draw",
            shape(*spec["nub"]),
        ]
        if level:
            filled = max(2 * fr, round((fx1 - fx0 + 1) * level / levels))
            layers += [
                "-fill",
                fill,
                "-draw",
                shape(fx0, fy0, fx0 + filled - 1, fy1, fr),
            ]
        if antialias:
            layers += ["-filter", "Box", "-resize", f"{width}x{height}!"]
        self.magick(*layers, f"PNG32:{path}")

    def pill(self, path, size, color, background, bolt=None):
        """Charge and Plug overlays: the firmware composites them over the level
        battery rather than replacing it, so the whole frame is opaque, flattened
        against the status bar background. Otherwise the level battery would show
        through the antialiased corners and the translucent nub."""
        width, height = size
        s = 4
        x0, y0, x1, y1, radius = BATTERY["body"]
        nx0, ny0, nx1, ny1, nr = BATTERY["nub"]

        def shape(x0, y0, x1, y1, r):
            return (
                f"roundrectangle {x0 * s},{y0 * s} {(x1 + 1) * s - 1},{(y1 + 1) * s - 1}"
                f" {r * s},{r * s}"
            )

        layers = [
            "-size",
            f"{width * s}x{height * s}",
            f"xc:{background}",
            "-fill",
            rgba(color, BATTERY["nub_alpha"]),
            "-draw",
            shape(nx0, ny0, nx1, ny1, nr),
            "-fill",
            color,
            "-draw",
            shape(x0, y0, x1, y1, radius),
            "-filter",
            "Box",
            "-resize",
            f"{width}x{height}!",
        ]
        if bolt:
            coverage = self.trimmed(self.glyph("bolt_fill", (7, 9)))
            glyph_width, glyph_height = self.image_size(coverage)
            x = round((x0 + x1 + 1) / 2 - glyph_width / 2)
            y = round((y0 + y1 + 1) / 2 - glyph_height / 2)
            layers += [
                "(",
                "-size",
                f"{glyph_width}x{glyph_height}",
                f"xc:{bolt}",
                coverage,
                "-alpha",
                "off",
                "-compose",
                "CopyOpacity",
                "-composite",
                ")",
                "-geometry",
                f"+{x}+{y}",
                "-compose",
                "Over",
                "-composite",
            ]
        self.magick(*layers, f"PNG32:{path}")

    def render_tinted_batteries(self):
        """Status bar levels drawn in one color: white coverage the device draws
        in text. Low levels carry a red fill and stay static."""
        for variant in STATUS_BATTERIES:
            for level in range(LOW_BATTERY + 1, 23):
                name = f"StatusBar{variant}_Battery_Image_{level}"
                relative = f"art/tint/{name}.png"
                self.battery(
                    self.out / relative,
                    self.size(name),
                    BATTERY,
                    "#ffffff",
                    level,
                    22,
                    "#ffffff",
                    True,
                )
                self.shared(name, relative, "text")

    def render_batteries(self, appearance, folder):
        text = self.role(appearance, "text")
        background = self.role(appearance, "background")
        for variant in STATUS_BATTERIES:
            # The red low levels; the tinted pass covers the others.
            for level in range(LOW_BATTERY + 1):
                name = f"StatusBar{variant}_Battery_Image_{level}"
                path = self.out / folder / f"{name}.png"
                self.battery(
                    path,
                    self.size(name),
                    BATTERY,
                    text,
                    level,
                    22,
                    RED[appearance],
                    True,
                )
                self.entry(appearance, name, f"{folder}/{name}.png")
            for suffix, color, bolt in (
                ("Charge", GREEN[appearance], "#ffffff"),
                ("Plug", text, None),
            ):
                name = f"StatusBar{variant}_Battery_Image_{suffix}"
                path = self.out / folder / f"{name}.png"
                self.pill(path, self.size(name), color, background, bolt)
                self.entry(appearance, name, f"{folder}/{name}.png")
        for level in range(8):
            name = f"NowPlaying_Idle_Battery_{level}_Image"
            path = self.out / folder / f"{name}.png"
            fill = RED[appearance] if level == 0 else "#ffffff"
            self.battery(
                path,
                self.size(name),
                IDLE_BATTERY,
                "#ffffff",
                max(level, 1),
                7,
                fill,
                False,
            )
            self.entry(appearance, name, f"{folder}/{name}.png")

    def manifest(self):
        appearances = {}
        for appearance, colors in APPEARANCES.items():
            accent = colors["accents"][DEFAULT_ACCENT]
            accents = {}
            for name, value in colors["accents"].items():
                accents[name] = {
                    role: value
                    for role in ("accent_fill", "accent_text", "selection", "icon_tint")
                }
                accents[name]["on_accent"] = (
                    "#000000" if name == "yellow" else "#ffffff"
                )
            appearances[appearance] = {
                "colors": {
                    "background": colors["background"],
                    "text": colors["text"],
                    "accent_fill": accent,
                    "accent_text": accent,
                    "on_accent": "#ffffff",
                    "selection": accent,
                    "cover_border": colors["cover_border"],
                    "icon_tint": accent,
                },
                "accents": dict(sorted(accents.items())),
                "rendering": {
                    "selection": "flat",
                    "menu_covers": "off",
                    "now_playing_art": "flat",
                },
                "bitmaps": sorted(
                    self.entries[appearance], key=lambda item: item["name"]
                ),
            }
        return {
            "schema": 1,
            "id": "flat",
            "name": "Flat",
            "author": "RepriseOS",
            "version": "1.0",
            "appearances": appearances,
        }

    def render(self):
        self.render_tinted()
        for appearance in APPEARANCES:
            self.render_static(appearance)
        work = self.out / "art/work"
        for file in work.iterdir():
            file.unlink()
        work.rmdir()
        manifest = self.manifest()
        (self.out / "theme.json").write_text(json.dumps(manifest, indent=2) + "\n")
        # Remove art from earlier renders that the manifest no longer uses.
        used = {
            item["file"]
            for appearance in manifest["appearances"].values()
            for item in appearance["bitmaps"]
            if "file" in item
        }
        for path in (self.out / "art").rglob("*.png"):
            if path.relative_to(self.out).as_posix() not in used:
                path.unlink()


def compare(expected, actual):
    differences = []
    for path in sorted(actual.rglob("*.png")):
        other = expected / path.relative_to(actual)
        if not other.exists() or read_png(path) != read_png(other):
            differences.append(str(path.relative_to(actual)))
    for path in sorted((expected / "art").rglob("*.png")):
        if not (actual / path.relative_to(expected)).exists():
            differences.append(f"{path.relative_to(expected)} (stale)")
    if (expected / "theme.json").read_text() != (actual / "theme.json").read_text():
        differences.append("theme.json")
    return differences


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--font", type=Path, default=os.environ.get("CUPERTINO_ICONS"))
    parser.add_argument("--text-font", type=Path, default=os.environ.get("INTER_FONT"))
    parser.add_argument("--catalog", type=Path, default=CATALOG)
    parser.add_argument(
        "--check",
        action="store_true",
        help="render to a temporary folder and compare with the committed files",
    )
    parser.add_argument(
        "--sizes",
        action="store_true",
        help="print the rendered glyph size of each icon",
    )
    args = parser.parse_args()
    if not args.font or not args.font.is_file():
        parser.exit(1, "CupertinoIcons.ttf is required (--font or CUPERTINO_ICONS)\n")
    if not args.text_font or not args.text_font.is_file():
        parser.exit(1, "Inter-Medium.ttf is required (--text-font or INTER_FONT)\n")
    catalog = json.loads(args.catalog.read_text())
    with tempfile.TemporaryDirectory(prefix="flat-theme-") as work:
        if args.check:
            with tempfile.TemporaryDirectory() as temporary:
                renderer = Renderer(
                    args.font, args.text_font, catalog, Path(temporary), work
                )
                renderer.render()
                differences = compare(HERE, Path(temporary))
        else:
            renderer = Renderer(args.font, args.text_font, catalog, HERE, work)
            renderer.render()
    if args.sizes:
        for name, (glyph, box, (width, height)) in sorted(renderer.sizes.items()):
            print(f"{name:48s} {glyph:28s} box {box[0]:3d}  glyph {width}x{height}")
    if args.check:
        if differences:
            parser.exit(1, "Out of date: " + ", ".join(differences) + "\n")
        print("Flat theme artwork is up to date")
        return
    subprocess.run(
        [
            sys.executable,
            "-m",
            "themes.package",
            str(HERE / "theme.json"),
            "--out",
            str(PACK),
            "--catalog",
            str(args.catalog),
        ],
        check=True,
        cwd=ROOT,
    )


if __name__ == "__main__":
    main()
