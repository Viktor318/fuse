/* soundsettings.h: Win32 Sound settings window
   Copyright (c) 2026 Viktor (GitHub: Viktor318)

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

*/

/* The window class of the faders (with their level meters) */
#define SOUND_FADER_CLASS "FuseSoundFader"

#define IDD_SOUNDSETTINGS     4500
#define IDC_SND_ENABLED       4501
#define IDC_SND_LOADING       4502
#define IDC_SND_STEREO_LABEL  4503
#define IDC_SND_STEREO        4504
#define IDC_SND_SPEAKER_LABEL 4505
#define IDC_SND_SPEAKER       4506
#define IDC_SND_RESET         4507
#define IDC_SND_MIXING        4508
#define IDC_SND_HINT          4509
#define IDC_SND_OUTPUT        4510
#define IDC_SND_MIXER         4511

/* One of each for the columns: master volume, AY, beeper, uSpeech,
   SpecDrum, Covox */
#define IDC_SND_GROUP1        4520
#define IDC_SND_FADER1        4530
#define IDC_SND_PERCENT1      4540
#define IDC_SND_MUTE1         4550
