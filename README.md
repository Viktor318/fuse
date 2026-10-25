# Fuse – Hungarian Windows edition

**English** | [Magyar](README.hu.md)

This is a fork of [Fuse](https://fuse-emulator.sourceforge.net/), the Free
Unix Spectrum Emulator (version 1.10.0), made for using it on Windows with
its native Windows interface, in Hungarian or English. It adds a Hungarian
user interface and keyboard support, typing BASIC programs as text, an AY
music player with a playlist, integer-scaled full screen, and several
fixes, mostly to the Windows sound output.

The emulation itself is the original Fuse; this fork changes the Windows
user interface around it. The original Fuse documentation is in
[README](README) and the original change log in [ChangeLog](ChangeLog).
The changes of this fork are listed in [CHANGELOG-fork.md](CHANGELOG-fork.md).


## Download

Download `Fuse-1.10.0-hu.1-windows.zip` from the
[Releases](https://github.com/Viktor318/fuse/releases) page, unpack it into a
folder of your choice and start `fuse.exe`. Nothing has to be installed.

## What this edition adds

### Hungarian or English user interface

- **Options > General > Language**: *Automatic* (follows the Windows display
  language), *English* or *Magyar*. The change takes effect at once for the
  menus; windows already open show the new language when opened again.
- Every menu, dialog, option and message is translated. The dialogs grow
  where the Hungarian texts are longer.

### Keyboard

- **Type using the Windows keyboard layout** (Options > General, on by
  default): what you type is what appears on the Spectrum, whatever the
  layout of your keyboard. On a Hungarian keyboard Shift+2 types `"`,
  AltGr+V types `@`, and so on. Accented letters are typed without the
  accent. Symbols that need the extended mode (`[ ] { } \ | ~ ©`) are typed
  with the right key sequence automatically.
- **Use left Alt as Caps Shift with number keys** (on by default): left
  Alt + 0–9 gives Caps Shift + number (EDIT, the cursor keys, DELETE...).
  Otherwise Alt keeps its Windows meaning (menus, Alt+F4). The numeric
  keypad always gives digits and `+ - * / .`.
- **Help > Keyboard**: a high resolution picture of the Spectrum keyboard. It
  stays above Fuse without taking the focus, so you can keep typing; it can be
  resized and remembers its place and size.

### BASIC programs as text

- **File > Load BASIC text...** reads a BASIC program from a text file
  (zmakebas style escapes, BASin `.bas` files, UTF-8 or Windows-1250) and
  puts it into memory in place of the current program. Lines without numbers
  are numbered 10, 20, 30...; accented letters are replaced and reported. If
  a program is running, Fuse asks and resets the machine first.
- **File > Save BASIC as text...** writes the program in memory as text.

### AY music player


Plays `.ay` (ZXAYEMUL) music files – the music of many Spectrum games and
demos – on an emulated Spectrum +2A.

- **Opening**: *Media > AY music player...* opens the player without playing
  anything; *File > Open* (or dragging a file onto Fuse) adds the songs of an
  `.ay` file to the end of the playlist and plays the first one.
- **The program that was running is kept**: when the first song starts, Fuse
  takes a snapshot of the machine; closing the player (or Fuse) brings it
  back exactly as it was.
- **Playlist window**: songs of any number of files; Add (several files at
  once), Add folder, Remove (also the Delete key), Clear list, Up/Down, Load
  and Save list (`.m3u`), dragging files, folders or lists from Explorer.
  Double click or Enter plays a song. The window can be resized and remembers
  its place, size and column widths; the list is kept for the next start.
- **Play**: *1 song* or *Whole list*, and *Repeat* (the song or the whole
  list). Previous / Pause / Stop / Next, and a slider to seek in the song.
- **Song lengths are measured**: the lengths written in `.ay` files are often
  wrong or missing. The first time a song is played (or with *Measure lengths*
  in the playlist window), Fuse runs it at full speed without sound until the
  music repeats, and keeps its exact length (in well under a second). Songs
  that never repeat get 5 minutes. A song that goes round fades out in
  3 seconds when the player moves on.
- **Levels and sound**: level meters of the three AY channels (in the order
  of the stereo mode), a tick box to mute each channel, volume, and stereo
  mode (none, ACB, ABC and the new BAC) – all changed at once while playing.
- The music goes on while a menu is open, while a window is moved or when
  Fuse is minimized.

### Window and sound

- **Full screen** (F11, *Options > Full screen*): borderless and integer
  scaled, taken over from the unofficial
  [jfroco/fuse](https://github.com/jfroco/fuse) fork.
- **Resume last session** (Options > General): the machine, with the
  inserted tape and its position, is saved when Fuse exits and restored at the
  next start.
- The main window, the keyboard picture and the AY player windows open where
  they were last time.
- **Confirm on exit** (Options > General, on by default).
- The peripheral options grey out what the emulated machine can't have.
- The menu bar is drawn with a clearly visible selection.
- DirectSound: no old sound played again after a hold-up, no clicks.

### Where Fuse keeps its files

Everything is kept in the **Fuse folder of your Roaming folder**: type
`%APPDATA%\Fuse` into the address bar of Explorer (or Win+R). It holds:

| File | Content |
|---|---|
| `fuse.cfg` | the settings (saved with *Options > Save* or at exit when *Options > General > Auto-save settings* is on) |
| `fuse-windows.ini` | the places and sizes of the windows |
| `fuse-ay-playlist.m3u` | the AY playlist |
| `fuse_ay_lengths.txt` | the measured lengths of AY songs |
| `fuse-last-session.*` | the last session (with *Resume last session*) |

Files of earlier versions in your user folder are moved there at the first
start.

## Building from source

The Windows program is built on Linux or in WSL (Windows Subsystem for
Linux) with MinGW-w64:

```
sudo apt install gcc-mingw-w64-x86-64 binutils-mingw-w64-x86-64 \
     libz-mingw-w64-dev autoconf automake libtool pkg-config flex bison \
     perl zip git
./build-windows-cross.sh
```

The script fetches and builds libspectrum and then Fuse with the Win32
user interface, and packages `fuse.exe` with the DLLs, ROMs and data files
into `~/fuse-windows/Fuse-windows-<branch>.zip`. It works on a copy of the
source tree. Settings for building from VS Code connected to WSL are in
`.vscode` (Ctrl+Shift+B).

## Licence

Fuse is free software under the GNU General Public License, version 2 or
later (see [COPYING](COPYING)). The AY player starts songs the way the
ZEsarUX emulator does (`ayplayer.c`, based on ZEsarUX by César Hernández
Bañó, GPL version 3 or later), so this edition as a whole is distributed
under the GNU GPL version 3 or later.

## Thanks

- Philip Kendall, Fredrick Meunier and everyone who made
  [Fuse](https://fuse-emulator.sourceforge.net/).
- César Hernández Bañó for [ZEsarUX](https://github.com/chernandezba/zesarux).
- jfroco for the integer-scaled full screen of
  [jfroco/fuse](https://github.com/jfroco/fuse).
