# Controller map

All menus, labels and messages are English. Arabic letters in the filename
search keyboard are input characters; they do not change the application language.

## Browser

| Button | Action |
| --- | --- |
| D-Pad Up / Down | Select the previous / next row |
| Left / Right | Move focus between list, tabs and details |
| Left Stick | The same navigation after a radial dead zone of 0.24 |
| X | Open source/folder/media; nonmedia remains information-only |
| Circle | Return and restore parent-folder selection |
| L1 / R1 | Previous / next of the four tabs |
| Triangle / Options | Available context actions |
| Square | Open / close file information |

USB Files starts at the source root. Videos, Music and Photos start in their
named source folders and show their corresponding media type plus folders.
Switching categories clears the old folder/search/paging state. Back from a
category root opens source selection; choosing a source preserves the active tab.

Navigation repeats after 0.38 seconds, then every 0.12 seconds. Trigger repeats
use 0.25 seconds. Selection/Back use edges and do not repeat from polling.

## Music

| Button / visible control | Actual action |
| --- | --- |
| X / Play or Pause | Toggle playback; replay after clean EOF |
| Left or L2 / Back 10s | Seek backward ten seconds |
| Right or R2 / Forward 10s | Seek forward ten seconds |
| L1 / Previous | Previous track when the playlist has multiple entries |
| R1 / Next | Next track when the playlist has multiple entries |
| Up / Down | Actual PCM volume ±5% |
| Circle / top-left Back | Return to the browser with music continuing |
| Square / Stop | Stop music, release artwork and return to the browser |
| Triangle / Options | Repeat, shuffle, playlist, information and stop |

Six 168×116 rounded controls share their geometry with native mouse hit testing;
32-pixel transport icons sit inside 48-pixel rounded tiles. Gaps do not activate
controls. Previous/Next are disabled for a one-track queue; unavailable seek is
not displayed as active. Volume can be reduced to zero.

## Video

| Button / visible control | Actual action |
| --- | --- |
| X / Play, Pause or Replay | Toggle playback; reopen the video after clean EOF |
| Left or L2 / Back 10s | Seek backward ten seconds and decode the target frame |
| Right or R2 / Forward 10s | Seek forward ten seconds and decode the target frame |
| L1 / Previous | Previous video when the playlist has multiple entries |
| R1 / Next | Next video when the playlist has multiple entries |
| Up / Down | Volume ±5% when the video contains audio |
| Circle / Back or Stop | Stop video, release its resources and return to the browser |
| Square / Fit, Fill or Original | Cycle video aspect mode |
| Triangle / Options | Playlist, information, aspect and stop actions |

Six compact 64×64 rounded transport buttons use centered 28-pixel icons without
text labels. Back, aspect and Options also use centered icons. The blue primary
button changes with playback state. A transparent
dark gradient behind the bottom controls keeps the video visible. Previous/Next
are disabled for a single video; seeking is disabled when unavailable. Rendered
geometry, availability and native pointer hit testing are shared. Gaps do not
activate buttons. Square remains the aspect shortcut in video.

Controls initially appear and hide after seven full seconds without intentional
input. Pause and open menus keep them visible. Real right-stick movement or a
button reveals them; drift/unchanged polling does not reset the timer. OSD does
not stop, pause or reopen the decoder.
The first pointer click while hidden only reveals the controls. Loading, error
and finished screens keep their applicable controls drawn and clickable, even
after the activity timer expires. Replay reopens the actual finished file.

## Photos and search

Photos: X toggles slideshow; Left/Right or L1/R1 selects photos; Up/Down zooms;
Square toggles fit/original; Circle returns; Triangle opens information. Options
provides rotation, slideshow interval and GIF/WebP animation pause/resume.

Search: directions select letters; X adds a character or chooses an action;
Square deletes the last character; Triangle switches Latin/Arabic input;
Options applies the search; Circle cancels. Action labels remain English.

## Native computer preview

| Keyboard | Controller action |
| --- | --- |
| Arrows or WASD | D-Pad |
| Enter or Space | X |
| Esc | Circle |
| T | Triangle |
| Q | Square |
| [ / ] | L1 / R1 |
| 1 / 2 | L2 / R2 |
| Tab | Options |

Click tabs, list rows or player buttons; double-click a row to open it. Click Back
to return. Pointer positions account for window resizing and letterboxing.
`./Preview.command --video` opens and pauses real video for interactive review;
the default preview opens music.
macOS GameController integration accepts an extended gamepad recognized by the OS.
PS4 Pad fields/buttons compile against the inspected SDK. Neither a physical DS4
nor PS4 runtime input was available for verification. Essential actions do not
use the optional touchpad.
