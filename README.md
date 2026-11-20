Media Center is a homebrew media application for the PlayStation 4. It allows users to browse files on a USB drive, play videos and music, and view images—all through a lightweight, intuitive, and modern graphical interface controlled via the console's controller.

## Key Features

- Browse USB files and organize content.
- View storage sources (USB) with essential information, such as the name and available space (where applicable).

- Four main sections/tabs:
- **USB Files:** Browse all folders and files readable from the USB drive. 
- **Videos:** Access the `Videos` folder to view supported video files (you must create a folder named "Videos" for its contents to appear in this section). 
- **Music:** Access the `Music` folder to view supported audio files (you must create a folder named "Music" for its contents to appear in this section).
- **Photos:** Access the `Photos` folder to view supported image files (you must create a folder named "Photos" for its contents to appear in this section).
- Navigate through subfolders while maintaining a fast browsing experience.
- View file details (name, type, size, path, etc., where available).
- Search within the current folder, with support for both Arabic and Latin character input.
- Features include "Favorites," "Last Played," and saved or current playlists, categorized by media type.
- Safe handling of USB disconnection during use (automatically halts operations related to the disconnected source and returns the application to a safe state).

### 2) Video Player

- Play video files from a USB drive, featuring a modern control interface within the player. - Progress bar with current/total time, supporting fast-forward and rewind.
- Basic playback controls: Play/Pause, Previous/Next (when a playlist is available), Rewind/Fast-forward (±10 seconds), and Stop/Back.
- Volume control (for files containing an audio track).
- Aspect ratio adjustment: Fit/Fill/Original (subject to availability).
- Show/Hide control interface:
- **Auto-hide** after 7 seconds of inactivity. 
- **Manual hide** by pressing the **R3** button. 
- **Show** upon any controller input (pressing a button or moving the analog sticks/triggers).
- Attempts to display at **60Hz** on PS4 (automatically falling back to 30Hz if unavailable) to enhance visual smoothness when system resources permit.

### 3) Music Player

- Playback of audio files, displaying track information (such as Title/Artist/Album, if available in the metadata tags).
- Displays embedded cover art (if present) within a dedicated frame.
- Includes a progress bar and time display, Play/Pause, Previous/Next, Rewind/Fast-forward (±10 seconds), and volume control.
- Supports Shuffle/Repeat modes and displays the current playlist.
- **Background Playback:** Allows navigation to the file browser while music continues to play.
- **Active Track Indicator:** When browsing files during playback, an indicator marks the currently playing file within the list. - **Quick Return to Player:** Tapping on the track currently playing in the background opens the music player and returns you directly to it—**without restarting the track from the beginning**.

### 4) Image Viewer

- View images with support for zooming, rotation, and options to fit to screen or display at original size.
- Slideshow mode with adjustable interval timing.
- Support for animated images (such as GIFs or animated WebP files, within supported limits), with the option to pause or resume the animation.
- Display of image metadata (dimensions, file size, etc.).

### 5) Arabic Filename Support and Search

- Correct handling of UTF-8 encoded filenames (including Arabic characters).
- A search input field that supports both Arabic and Latin characters (making it easy to search for files by name).

## Supported Formats (Summary)

> Actual support depends on the specific file (encoding, audio/video tracks, metadata, etc.). Some less common variations may not work, even if the file extension matches.

### Video (Containers/Extensions commonly supported in browsers)
`MP4`, `MKV`, `AVI`, `MOV`, `WebM`, `M4V`, `MPEG/MPG`, `TS/M2TS/MTS`

- **Supported Video Codecs (Software-based):** Includes H.264, HEVC/H.265, AV1, VP8/VP9, MPEG-4, and a wide range of other codecs available within the specific FFmpeg build used. - **Video Audio:** Audio tracks are extracted and converted to a standardized output format (Stereo, 48kHz), with synchronization maintained relative to the playback timeline.
- **Notes:** Subtitles and interactive selection of multiple audio tracks are not currently supported (default or appropriate tracks are selected automatically).

### Audio

`WAV`, `MP3`, `FLAC`, `OGG (Vorbis)`, `AAC`, `M4A`, `OPUS`, `WMA`, `AIFF`

- Supports playback with seek (fast-forward/rewind) functionality; displays embedded metadata/album art when available.
- Audio Output: **48kHz / 16-bit / Stereo**.

### Images

`JPG/JPEG`, `PNG`, `BMP`, `GIF`, `WEBP`, `TGA`, `PSD` (limited support), `HDR`, `PNM/PPM/PGM`, `TIFF` (subject to specific encoding/constraints), `ICO`

- Supports animated images (GIF/WebP), subject to memory and frame-count limitations.
- Certain formats or variants (e.g., HEIC/HEIF/AVIF/JXL, or specific TIFF compression types) may be flagged as unsupported, with an explanatory message provided.

> For more detailed information regarding technical testing and constraints within the project, please refer to the documentation files included with the source code (e.g., `SUPPORTED_FORMATS.md` and `TECHNICAL_DESIGN.md`).

## Controls (Summary)

> Specific actions may vary depending on the screen or context (e.g., browser, music player, video player, or image viewer).

- **Left Stick:** Navigate between items and menus.
- **X:** Open/activate the selected item.
- **O (Circle):** Go back.
- **Triangle / Options:** Contextual options.
- **Square:** File details/information (depending on the context).
- **L1 / R1:** Switch between tabs.

### Video Player
- **X:** Play/pause (or restart if at the end).
- **Left/Right or L2/R2:** Fast-forward/rewind by ±10 seconds.
- **L1 / R1:** Previous/next (if a playlist/list is available).
- **Up/Down:** Volume control (if audio is present).
- **Square:** Change display mode (aspect ratio).
- **Triangle / Options:** Options.
- **O (Circle):** Stop and return to the browser.
- **R3:** Hide/show the progress bar and controls (with an auto-hide feature after 7 seconds).

### Music Player
- **X:** Play/pause (or restart if at the end).
- **Left/Right or L2/R2:** Fast-forward/rewind by ±10 seconds.
- **L1 / R1:** Previous/next (if a playlist/list is available).
- **Up/Down:** Volume control.
- **Triangle / Options:** Options (repeat, shuffle, playlist, information, etc.).
- **O (Circle):** Return to the browser while music continues to play.
- **Square:** Stop music and return.

### Image Viewer
- **Left/Right or L1/R1:** Navigate between images.
- **Up/Down:** Zoom in/out. - **Square:** Fit to screen / Original size.
- **X:** Start/stop slideshow (context-dependent).
- **Triangle / Options:** Information and options (rotate, display duration, pause animations, etc.).
- **O (Circle):** Back.

## Requirements and Operational Notes
- A **PS4** console with a suitable Homebrew environment (such as GoldHEN).
- Media stored on a **USB** drive in a format/structure accessible by the system.
- It is recommended to place video, music, and image files into `Videos`, `Music`, and `Photos` folders for a better browsing experience across the application's tabs.
- The application operates in **read-only** mode; it does not copy or modify your media files.
- Application settings and state (such as favorites, history, and playlists) may be saved internally within the application's data directory on the console.

## Privacy and Security
- The application does not require an internet connection to play local media.
- It does not upload files or alter the contents of your USB drive.
- Read operations are restricted exclusively to the files/sources on the USB drive that are accessible to the system.

## Known Limitations (Summary)
- Video, audio, and image decoding is performed via **software**; therefore, some high-resolution or complexly encoded files (such as certain 4K, HEVC, or AV1 files) may not play smoothly on all consoles.
- There is currently no support for subtitles or manual selection of multiple audio tracks within the player.
- Playback smoothness and frame rates depend on both the specific file and the console hardware; practical testing on a PS4 is required for a definitive assessment.
