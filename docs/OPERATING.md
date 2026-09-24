# Operating guide

## Starting and stopping

| Command | What it does |
|---|---|
| `shadow-worm-farm` | New bin from a fresh random seed, full screen |
| `shadow-worm-farm --resume` | Continue the newest valid checkpoint (starts a new bin if there is none) |
| `shadow-worm-farm --seed 12345` | New bin from a specific seed (reproducible) |
| `shadow-worm-farm --load FILE` | Continue a specific checkpoint file |
| `shadow-worm-farm --operator` | Also open the operator window |
| `shadow-worm-farm --windowed` | Run in a 1600 × 900 window instead of full screen |
| `shadow-worm-farm --fps 30` | Lighter 30 FPS preset (default 60; also switchable in the operator window) |
| `shadow-worm-farm --config FILE` | Simulation tuning file (`key = value` lines; see `core/include/wormfarm/config.hpp`) |
| `shadow-worm-farm --live [youtube\|x\|custom]` | Go live at start (see *Going live*) |

The desktop entry also offers *Resume last bin*, *Resume and go live*, *Resume with operator window* and *New bin
in a window*.

**Quit** from the operator window: press *Quit…*, then press it again within 5 seconds. Closing the bin window
through the desktop (for example Super+Q) also saves a checkpoint first. No key on the bin window quits it, so a
stray key press on stream can't end the show.

## Keys (bin window)

| Key | Action |
|---|---|
| F2 | Show or hide the operator window |
| F5 | Feed now: drop a few scraps on the bedding |
| F6 | Mist now: spray the top of the bedding (the glass fogs over for a while) |
| F11 | Toggle full screen / window |

The mouse cursor is always hidden over the bin.

## The keeper

The bin looks after itself. Every 4 simulated hours the keeper drops two to four scraps on the bedding: banana
peel, apple core, lettuce leaf, carrot peelings, coffee grounds or eggshell. Every 3 hours the keeper checks the top
of the bedding and mists it if it has dried out. *Feed now* and *Mist now* in the operator window (or F5 / F6) do
the same on demand, without changing the schedule. Worms only go for scraps once they have started to rot, so fresh
scraps sit untouched for the first hour or two.

## Operator window

A separate desktop window (it never draws over the bin) showing:

- **Status:** seed, simulated time, worms (and how many are young), how many are at the glass and feeding,
  cocoons, scraps, handfuls eaten, feedings, mists, humidity, frame rate, dropped ticks, memory and the log path.
- **Feed now (F5)** and **Mist now (F6)**.
- **Save checkpoint now** and **Save screenshot** (to `~/.local/share/shadow-worm-farm/screenshots`).
- **Frame rate:** 60 FPS or 30 FPS.
- **Live stream** (see below).
- **Sound:** master volume plus four categories (room tone, crawling, feeding, keeper). Settings are remembered.
- **Start a new bin…**, guarded: type `NEW BIN` to enable the button. The running bin is checkpointed first, so
  it can still be resumed with `--load`.
- **Quit…**, guarded: press twice within 5 seconds. It saves a checkpoint first.

Closing the operator window only hides it; the bin keeps running.

## Going live (built in)

The bin streams itself to YouTube, X or any RTMP/RTMPS server. It sends its own rendered picture and its own
sound, so there's no screen capture: nothing else on your desktop can end up in the stream, and it can never go
black because a screen share stopped.

**Set up once, in the operator window (F2) → Live stream:**

1. Choose **Stream to**: YouTube, X or Custom RTMP.
2. For **X** or **Custom**, paste the **server address**. YouTube needs no address. For X, create the livestream,
   then open *Edit livestream → Details → Show RTMP* and copy the **RTMP URL** of its source. It starts with
   `rtmp://` or `rtmps://`; the broadcast's `https://x.com/…` share link and the source's name are not server
   addresses, and the app refuses them with a message saying so.
3. Paste the **stream key** and press **Save key**. It goes into your desktop keyring (`secret-tool`), never into
   files or logs. Keys are kept separately from Shadow Ant Farm's.
4. Choose the **Quality**: 720p at 4 Mb/s (recommended) or 1080p at 7 Mb/s.
5. Optionally tick **Go live automatically whenever the bin starts**.

**Going live:**

- Press **Go live** in the operator window, or use the app menu's **Resume and go live**, or run
  `shadow-worm-farm --resume --live` (`--live youtube` / `--live x` picks the destination).
- The status shows **● LIVE** with the uptime and bitrate. If the connection drops, the bin reconnects by itself.
  If the server keeps refusing it (a wrong key or address), it stops after five tries and says so.
- **YouTube:** in YouTube Studio → Stream settings, turn on **Auto-start** once. YouTube then goes public by
  itself whenever the bin starts sending.
- **X:** with *Auto-start* on for the livestream, X goes live by itself once its source turns green; otherwise
  press *Go Live* on the livestream page once the source shows connected.
- **Running alongside Shadow Ant Farm:** both apps can stream at the same time, each to its own destination, with
  its own key. Use a different YouTube stream (key) for each, because one key accepts only one sender.

The launcher always uses the X11 display path, which keeps drawing and streaming even while the bin window is
behind other windows or the screen is asleep. The Wayland path pauses a hidden window, which would freeze a
stream.

What is sent: H.264 (NVIDIA NVENC, or x264 if NVENC isn't available), 30 fps, a keyframe every 2 s (every 3 s
for X), constant bitrate, and AAC stereo at 48 kHz, 128 kb/s.

## Capturing the window with another tool

- The bin window has a stable identity: app id and title `Shadow Worm Farm`. The operator window is titled
  `Shadow Worm Farm — Operator`.
- It never opens a camera or microphone. It keeps the screen awake while running and keeps running when
  unfocused.

## Where things are kept

Everything lives in `~/.local/share/shadow-worm-farm/`:

| Folder / file | Contents | Bound |
|---|---|---|
| `checkpoints/` | `bin-<seed>-t<tick>.wormfarm`, about 0.5 MB each, written every 10 minutes and on quit | Newest 4, plus one per simulated hour for 8 hours, plus the newest of 2 previous bins (about 7 MB at most) |
| `logs/shadow-worm-farm.log` | Start-up, checkpoints, keeper actions, an hourly status line, live-stream state, warnings | Rotated at 4 MB, 5 kept |
| `logs/engine.log` | Godot's own log | 6 kept |
| `settings.cfg` | Frame-rate preset, volumes, live destination and quality (never the key) | |
| `soak/`, `screenshots/` | Soak reports and stills | Only what you create |

Checkpoints are written atomically (temporary file, `fsync`, rename) and carry a format version, a
configuration hash, a state hash and a whole-file checksum. `--resume` skips any damaged checkpoint and falls
back to the next newest.

## Long-run and inspection tools

**Real-time soak test** (the only way to show that a 24-hour run holds up; it takes 24 real hours):

```bash
shadow-worm-farm --soak 24 --operator
```

Every real minute it writes a JSON line to `~/.local/share/shadow-worm-farm/soak/soak-<start>.jsonl`: frame rate,
99th-percentile and worst frame time, memory, dropped ticks, worms, feeding, scraps, castings, audio statistics.
It saves stills at 0, 1, 4, 8, 12, 18 and 24 real hours and a `…-summary.json` when done, then saves a checkpoint
and quits. A soak only counts as passed if it actually ran for 24 real hours.

**Accelerated visual inspection** (renders stills without waiting in real time):

```bash
shadow-worm-farm --seed 12345 --capture ~/wormfarm-stills --capture-hours 0,1,4,8,12,18,24
```

The bin renders offscreen at 3840 × 2160 (`--capture-size` to change), fast-forwards the same simulation to each
hour, and saves a PNG.

**Headless accelerated runs and maps:** `build/wormfarm_sim --seed 12345 --hours 24 --maps DIR` prints an
hourly table (worms, young, at the glass, feeding, resting, cocoons, scraps, eaten, castings) and writes
diagnostic maps; `--checkpoint FILE` saves the end state, which `--load FILE` continues in the app.

**Frame-time benchmark:** `--bench 30` (add `--render-size 3840x2160` to render 4K offscreen, and `--uncapped`
to measure headroom without the frame cap).

**Headless checks through the extension:** `godot --headless --path game -- --headless-test 1` covers
determinism, resume, fresh seeds, buffer sizes, feeding and the audio range.

## Troubleshooting

- **No sound:** check the output device and the operator window's volume sliders (the settings persist).
- **The bin looks frozen in a capture preview:** the window is probably hidden or the display is asleep, so the
  compositor isn't asking it for frames. Show it on its monitor. The bin itself kept running, and the built-in
  live stream is not affected.
- **Heavy GPU load elsewhere:** switch to the 30 FPS preset. The simulation is unaffected.
- **Start over:** use *Start a new bin…* in the operator window, or simply launch without `--resume`.
