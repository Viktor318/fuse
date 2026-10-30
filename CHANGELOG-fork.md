# Changes of the Hungarian Windows edition

**English** | [Magyar](CHANGELOG-fork.hu.md)

Changes of this fork on top of Fuse 1.10.1 (until v1.10.0-hu.1: 1.10.0).
The changes of Fuse itself are in [ChangeLog](ChangeLog).

## Unreleased

### AY mixer

- New **AY mixer** window (*Mixer...* in the AY player, *Options > AY
  mixer...*): the stereo mode, the places of the channels in the Custom mode
  (shown greyed out in the other modes), and the volume and **panorama** of
  each channel, in the order of the stereo mode. The panorama works as a
  balance: in the middle both sides are at full level, towards one side the
  other side gets quieter; where the stereo mode puts a channel it sounds as
  before. Games and the AY player hear these settings. *Reset*: volumes 100%,
  panoramas where the mode puts the channels (changing the mode does that
  too). Settings that can be changed in more than one window follow each
  other at once.
- **`.mix` files**: *Save...* in the mixer writes the mixer settings into a
  text file next to the song (`Song.mix`, or `Song.3.mix` for song 3 of a
  file; in `%APPDATA%\Fuse\mix` if the folder cannot be written). With *Load
  the mixer settings of the songs automatically* in the AY player, a song is
  played with its `.mix` file, until a song without one; the saved settings
  of Fuse are not changed by them. The mixer title shows the file in use.
- The *Mixer...* button of the AY player opens the mixer, or closes it if
  it is open. Loading the `.mix` files automatically is on by default.
- The AY stereo mode is in *Options > Sound* again (as in Fuse, now with
  all eight modes); it follows the mixer and the AY player at once, and
  *Cancel* restores it.
- The AY player window is as narrow as it was before the channel volumes
  (they are in the mixer now), with longer level meters; the *Mixer...*
  button is beside the stereo mode.
- *Options > Sound* keeps only the AY volume and the stereo mode of the AY
  settings; the places and the channel volumes are in the mixer.

### Files and screenshots

- *File > Screenshot > Save Screen as PNG...*: the screen can be saved as a
  PNG file too (with the chosen filter).
- `.bz2` compressed files (e.g. `game.tzx.bz2`) can be opened.
- Both are built into the exe; no further DLLs are needed.
- The titles of the filter and machine selection windows are translated
  too.

### Unicode user interface

- Every text of the user interface (window titles, menus, dialogs) and the
  file names go to Windows as UTF-8 (Windows 10 1903 and later), so
  Hungarian "ő" and "ű" show correctly on a non-Hungarian Windows too,
  instead of "o" and "u", and file names in any language can be opened.
  On a Hungarian Windows nothing looks different.
- Accented paths saved before (in `fuse.cfg` and the AY player's saved
  list) are converted automatically; `.m3u` lists and the titles and
  authors of music files in an old code page show correctly too.
- Where Windows doesn't use the process UTF-8 code page, Fuse passes the
  text as Unicode: the column titles and rows of lists (e.g. the AY
  playlist), the status bar, the file and folder selection windows, the
  names of dropped files and the owner drawn menu bar.
- Menu items are enabled and greyed in the Hungarian user interface too
  (the translated menu items weren't found before).

### SpecDrum and Covox

- The emulator no longer crashes when, with the SpecDrum or Covox switched
  on, a program writes to the device's port while the sound is off (during
  fastloading, or with the sound disabled), as the SpecDrum *KIT EDITOR*
  loader does. The bug is in upstream Fuse as well.

### Snapshots

- Loading a snapshot keeps the devices switched on in *Peripherals: sound*
  (Fuller Box, Melodik, SpecDrum, Covox, uSpeech). Most formats can't
  record them (`.z80` has no SpecDrum, for example), so loading used to
  switch them off. A device the snapshot itself asks for is still switched
  on.
- `.z80` files with junk after the last memory block (e.g. 64 stray bytes
  at the end of the SpecDrum kit snapshots) now load. They used to fail
  with "read_v2_block: not enough data in buffer"; the junk is now left
  out.

### Upstream Fuse

- Fix from upstream `master`: loading a snapshot now releases the joystick
  buttons (Kempston, Timex, Fuller), so fire or a direction no longer stays
  stuck (this also covers the program restored when the AY player closes).

## v1.10.1-hu.1 – 2026-10-30

Now based on Fuse 1.10.1 (its changes are in [ChangeLog](ChangeLog)).

### Sound

- WASAPI sound output (from the upstream `feature/wasapi-audio` branch):
  `fuse.exe` now uses it. Lower latency; the sound is fed from a thread of
  its own.
- WASAPI follows the Windows default output: when it is changed, or the
  device in use is unplugged, the sound moves to the new default device
  without restarting Fuse.
- `fuse-directsound.exe`: the same program with DirectSound, as a fallback.
  Both use the same settings.
- AY stereo modes: new **BCA** and **CAB**; the list is now ABC, ACB, BAC,
  BCA, CBA, CAB, Mono, Custom. *None* is called **Mono** (`--separation
  mono`; `none` and settings saved with *None* still work, and the names
  are accepted in any case).
- New AY stereo modes: **CBA** (ABC mirrored) and **Custom**: in *Options >
  Sound* (or with `--ay-channel-a/b/c Left/Centre/Right`) each channel can be
  on the left, in the centre or on the right, so two channels can be on the
  same side too.
- *Options > Sound* and the AY player show the same AY settings and follow
  each other at once (stereo mode, places of the channels, AY volume);
  *Cancel* restores them. The places of the channels can be set only in the
  Custom mode.

### AY music player

- Songs play apart from the emulated machine: a Z80 of its own (the chips
  Z80 by Andre Weissflog, zlib license) runs the player of the `.ay` file,
  with an AY sound generator of its own. The machine is no longer taken over
  (no +2A, no snapshot): the program that was running goes on and is heard
  together with the music. Pause stops the music only.
- With WASAPI (`fuse.exe`) the music has a sound stream of its own, fed by a
  thread: it plays on during menus, dialog boxes and window moves, with the
  emulation paused as usual, and follows the default output device too.
  With DirectSound it is mixed into the sound of the emulator, frame by
  frame (300 ms of sound kept ahead, as before).
- Song lengths are measured in the background, in a thread, while the song
  plays (no restart when the length is found); *Measure lengths* in the
  playlist window no longer stops the song playing. Lengths that the earlier
  version gave up on are measured again. Seeking is instant in both
  directions.
- The measured lengths are found at once (a hash table in memory), also in
  long playlists; when a song is in the lengths file more than once, the
  file is written again at the next start with one line for each song.
- Pro Tracker 3 modules (`.pt3`, also from Vortex Tracker II) can be played
  too: no Z80 player runs, the AY registers of every frame are worked out
  from the module (the player is a C port of ZXTune's, checked frame by
  frame against Sergey Bulba's Z80 player and against ZXTune). Title and
  author come from the module; the playlist, length measurement, seeking,
  level meters, stereo and channel muting work as for `.ay` files. Of
  TurboSound modules only the first AY is played for now.
- Volume of each channel: a slider beside each level meter (0-100%; the
  percent is shown above the thumb while moving it, and in a tooltip),
  *Reset* sets all three back to 100%. The meters show the levels after
  these volumes. The volumes are kept, and are also in *Options > Sound*;
  both windows show changes at once. The window is a little wider.
- While paused, the level meters stay where the music stopped and go on
  with the music.
- Measuring the lengths in the playlist window can be stopped: *Measure
  lengths* becomes *Stop measuring* while measuring (the length of the song
  being measured is still kept).
- Files of the saved playlist that are not there any more (moved, deleted,
  on a drive not connected) are left out without error messages; a list
  loaded by the user gives one message with the number of files left out.

### Other

- *Help > About* no longer pauses the emulation (the music no longer jumps).

## v1.10.0-hu.1 – 2026-10-25

The first release of the fork.

### Hungarian user interface

- English or Hungarian user interface: *Options > General > Language* (or
  `--language`). *Automatic* follows the Windows display language. The menus
  change at once; dialogs are translated when they are opened, messages when
  they are shown.
- Every menu, option, dialog and message is translated (the translation was
  reviewed text by text). Internal developer messages stay in English.
- Dialogs are widened where the Hungarian texts are longer: labels, check
  boxes, drop-down lists, buttons and the dialog itself; the fields beside
  labels move to the right.
- Drop-down list items, list headers, file selector titles and file types
  are translated too.
- The translation is kept in `translations/hu.tsv`; `i18n_hu.c` is generated
  from it by `translations/make_table.py`, which also checks that the printf
  conversions of every translation match the English text.
- Hungarian terms: *kazetta* for the tape, *Microdrive-kazetta* for the
  Microdrive cartridge.
- The menu bar items are measured again after a language change, so longer
  names are not cut off.

### Keyboard

- **Type using the Windows keyboard layout** (`--windows-layout`, on by
  default): typing follows the characters the Windows keyboard layout gives,
  not the positions of the keys, so any layout works.
  - Lower case letters and digits: the key itself; upper case letters:
    Caps Shift + letter; symbols: Symbol Shift + key.
  - Hungarian accented vowels are typed without the accent.
  - The extended mode symbols `[ ] { } \ | ~` and `©` are typed with a timed
    key sequence (in the 48K and 128K editors); keys typed meanwhile are
    queued to keep their order.
  - Characters with no Spectrum equivalent are ignored.
  - Keys without a character (cursors, Enter, function keys), Ctrl and Alt,
    joystick keys and the menus work as before.
  - The earlier option name `--hungarian-keyboard` is still accepted.
- **Use left Alt as Caps Shift with number keys** (on by default): left Alt +
  0–9 of the main block gives Caps Shift + number (EDIT, cursors, DELETE...).
  Alt keeps its Windows meaning otherwise (menus, Alt+F4); the menu letters are
  not underlined after Alt was used with a number.
- The numeric keypad (Num Lock on) gives digits and `+ - * / .` in every
  typing mode; keypad keys are released by their physical key.
- Digits on other keys (the 0 of the Hungarian layout) are digits too.
- AltGr no longer leaves Symbol Shift held (Windows sends AltGr as Ctrl + Alt).

### Keyboard picture (Help > Keyboard)

- A high resolution picture (`lib/keyboard.jpg`), decoded with GDI+ (JPEG,
  PNG, GIF, BMP), with `keyboard.scr` as the last fallback.
- The window stays above Fuse without taking the focus, so typing goes on;
  it can be resized (down to a quarter), scaled with halftone stretching, and
  opens scaled down if it would not fit on the screen.
- It remembers its place and size.

### BASIC programs as text

- **File > Load BASIC text...**: reads a BASIC program from a text file and
  puts it into memory, replacing the current one.
  - zmakebas style escapes for block graphics, UDGs and other codes; BASin
    `.bas` files (header lines skipped, `\#nnn`, `` \` `` and colour control
    escapes such as `\{f1}`); UTF-8 or Windows-1250.
  - Lines without numbers are numbered 10, 20, 30...; accented letters are
    replaced and reported.
  - In 48K BASIC the program is listed after the confirmation is closed; the
    128K editor is left and entered again so it lists the new program.
  - If a program is running, Fuse asks and resets the machine first.
- **File > Save BASIC as text...**: writes the program in memory as text.

### AY music player

- Plays `.ay` (ZXAYEMUL) music files on a Spectrum +2A with all-RAM special
  paging and the player stub of the format specification. Songs are started
  as ZEsarUX does (registers, player stub, blocks); songs without an init
  routine call their first block, as the format describes (ZEsarUX started
  the processor there, so such songs stayed silent).
- **Opening**: *Media > AY music player...* opens the player without playing;
  *File > Open*, dragging a file onto Fuse or the command line adds the songs
  of the file to the end of the playlist and plays the first one.
- **The running program is kept**: the first song played takes a snapshot of
  the machine; closing the player or Fuse brings it back, so the session
  saved at exit keeps it too.
- **Player window**: file information, the song being played, elapsed time
  and length, a slider to seek (forwards the emulation runs without sound,
  screen drawing or waiting; backwards the song starts again),
  Previous / Pause (real pause) / Stop / Next, List.
- **Play**: *1 song* or *Whole list*, and *Repeat*:
  - 1 song: stop at the end; with Repeat the song again (a song whose music
    goes round just goes on, the elapsed time back to where the round
    starts).
  - Whole list: the next song; at the end of the list stop, or with Repeat
    the first song again.
  - Kept in the settings.
- **Playlist window**: songs of any number of files; Add (several files),
  Add folder, Remove (also the Delete key), Clear list, Up / Down, Load and
  Save list (`.m3u`, lines `path::AY,song` as NEZplug, plain paths add every
  song of a file), Measure lengths, dragging files, folders or lists from
  Explorer; double click or Enter plays; the song being played in bold.
  Resizable (the list follows, the buttons keep their size), with its place,
  size and column widths remembered. The list is kept in
  `fuse-ay-playlist.m3u`.
- **Song lengths are measured**: the first time a song is played, it runs
  without sound, waiting or screen drawing while the AY registers of every
  frame are kept, until the music repeats (the smallest period repeating for
  at least a minute, then back to where the repetition starts). The song
  lasts until the end of the first round; if what repeats is silence, until
  the music stopped. At most 15 seconds; songs that do not repeat get
  5 minutes. A line at the bottom of the window shows the measuring. Lengths
  and the places where the songs go round are kept by file checksum in
  `fuse_ay_lengths.txt`, and override the lengths in the files ("(measured)",
  "(default)").
- *Measure lengths* measures the songs of the list not measured yet, also
  while a song plays (it goes on afterwards where it was).
- **Fading**: a song that goes round fades out in 3 seconds when the player
  leaves it (5 seconds for the 5 minute default length); songs whose music
  ends do not fade.
- **Level meters** of the three AY channels (envelope included), in the order
  of the stereo mode, with a tick box to mute each channel (the meter goes
  on in grey; closing the player turns every channel on again).
- **Volume** and **stereo mode** in the player window, applied at once while
  playing.
- The music goes on while a menu is open, while a window is moved or resized
  (from the click on), while a list opens, and when Fuse is minimized: a timer
  runs the frames when the main loop is held up, and the sound output keeps
  about 300 ms ahead while the player plays.

### Sound

- New AY stereo mode **BAC** (Options > Sound, `--separation bac`).
- The AY volume and the stereo mode can be changed while playing, without
  restarting the sound (the volume scales the amplitudes, a change between
  stereo modes only routes the channels again).
- DirectSound:
  - no old audio played again after an underrun (key clicks were heard twice);
  - data is always written beyond the write cursor; an underrun fades to
    silence in about 2 ms instead of holding the last sample;
  - a buffer of one second: games keep their low latency, the AY player keeps
    about 300 ms ahead.
- No AY register writes are queued while the sound is off, and the AY sound
  generator starts from the AY's current registers when the sound starts again
  (before, notes could stay on after the sound had been off).

### Window and settings

- **Full screen** (F11, *Options > Full screen*, `--full-screen`):
  borderless, integer scaled and centred, taken over from
  [jfroco/fuse](https://github.com/jfroco/fuse); per-monitor DPI awareness.
  The SDL 2 version also gets F11.
- **Resume last session** (*Options > General*, `--resume-last-session`):
  the machine, with the inserted tape and its position, is saved on exit and
  restored at the next start (unless a file to open is given), keeping the
  chosen peripherals.
- **Confirm on exit** (on by default; greyed out while confirmations are off).
- The peripheral options grey out what the emulated machine can't have.
- The menu bar is drawn by Fuse: the selected item on a darker grey, with the
  access letters while it is used from the keyboard.
- Information messages appear without the Windows sound.
- The main window, the keyboard picture and the AY player windows open where
  they were last time (if still on a screen).
- Everything Fuse keeps is in `%APPDATA%\Fuse`: `fuse.cfg`,
  `fuse-windows.ini` (places of the windows), `fuse-ay-playlist.m3u`,
  `fuse_ay_lengths.txt` and the last session. Files of earlier versions are
  moved there at the first start.

### Building

- `build-windows-cross.sh`: builds the Windows Fuse (Win32 user interface,
  DirectSound) from Linux or WSL with MinGW-w64, with libspectrum built as a
  static library, and packages it with its DLLs, ROMs and data files.
- VS Code settings for building from WSL (Ctrl+Shift+B), starting the program
  and IntelliSense for the MinGW-w64 build.
