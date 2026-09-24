# How the bin works

Shadow Worm Farm has three layers. A deterministic simulation core in plain C++ (`core/`) holds the bin. A
GDExtension (`extension/`) exposes that core to Godot and adds the sound and the live stream. A Godot project
(`game/`) renders the bin and runs the operator window. The presentation only reads the simulation. The two
keeper actions (feed now, mist now) are the only way anything outside the core changes it.

## Time and determinism

The core advances on a fixed tick of 30 Hz. The app runs as many ticks per rendered frame as real time requires,
measured by the wall clock and capped at 60 per frame. Gaps longer than 2.5 s, such as a suspend, are skipped and
counted rather than replayed in a burst. Rendering interpolates between the last two ticks, so motion is smooth at
60 FPS.

The same seed always gives the same bin. Randomness comes from PCG32 streams derived from the session seed with
SplitMix64. Terrain, behaviour, decoration and audio use separate streams, so changing a volume or the frame
rate can never change what the worms do. The core is built with `-fno-fast-math -ffp-contract=off`. The state
hash covers every byte of state, and the simulation structs are laid out with explicit padding so no
indeterminate bytes ever reach the hash.

## The substrate

The bin is a 320 × 180 grid seen against the front glass. One cell is about 2 mm, so the bin is about 60 cm wide.
Each cell holds:

- **material:** air, bedding, compost or soil;
- **castings level:** fresh worm compost;
- **moisture;**
- **density:** packed, or loosened where a worm has pushed along the glass.

A new bin gets:
- a wavy bedding surface under a thin headspace;
- a layer of shredded bedding with pockets of compost, over compost and garden soil;
- moisture rising with depth.

Boundaries between these are domain-warped noise, so no two bins look alike.

Once a second the substrate settles:
- burrows fill back in over about 15 minutes;
- the top of the bedding dries slowly;
- water seeps downward;
- every 10 minutes fresh castings fade one level back towards what the bin started with, as the worms mix them in.

That keeps the picture changing without it drifting to all-black over a long stream.

## Worms

Each worm is a chain of 24 body points. The head moves; the body follows like a rope, each point keeping its
spacing behind the one before. A worm also has:

- a depth into the bin, from 0 (pressed against the glass) to 1 (at the back);
- its length and growth (hatchling to adult);
- hunger, and castings still to pass;
- its state: wander, seek food, feed or rest;
- personal traits: speed, how straight it crawls, how often it comes to the glass, and the depth it likes to
  live at.

**Decisions** happen every 6 ticks (0.2 s), staggered across worms. A worm scores 11 candidate headings around
its current one and picks one at random, weighted by score. The score counts:
- keeping its heading;
- the smell gradient when hungry, or the direction of its chosen scrap when seeking one;
- moisture close to what worms like;
- staying away from the dry, light surface;
- looser, burrowed substrate;
- neighbours: crowding when wandering, company when resting;
- the worm's home depth when it isn't after food;
- a slow personal drift.

Headings into the air or the frame are never chosen. On top of that each worm meanders with a smooth noise term,
so paths curl rather than run straight.

**Depth:** every few minutes a worm picks a new target depth. About a third of the time it is at the glass, more
while feeding. The head leads and the body follows as it crawls, so a worm surfacing at the glass appears head
first. Only worms near the glass are drawn in full; deeper ones show through the substrate as they pass or not at
all.

**Hunger and feeding:** hunger rises over about 3 hours. A hungry worm looks for a scrap that has started to rot
(decay above 0.15) within a range that grows with the scrap's decay, and heads for it. At the scrap it feeds,
crawling slowly about under and around it, which is what makes the knots of feeding worms. Eating reduces the
scrap's mass and the worm's hunger and adds castings to pass. A worm leaves when it is full or after an hour.
Worms also eat a little bedding as they go.

**Castings** leave the tail as discrete pellets every few cells crawled. They darken the substrate where the
worm has been, most visibly along the glass.

**Burrows:** a worm crawling right against the glass (depth under 0.1) loosens the substrate in a disc around its
head. That leaves a visible channel that fills back in over a quarter of an hour.

**Life cycle:** well-fed adults lay cocoons now and then. Laying slows as the bin fills and stops at the
population cap (200), so the population levels off instead of growing without bound. Cocoons hatch after about
7 hours into one or two pale hatchlings, which grow into adults over about 20 hours.

## Food, smell and the keeper

Scraps have a type, size, mass, decay and a random seed for their look. They decay by type (lettuce fastest,
eggshell hardly at all), and faster when the substrate is wet. Microbes take a small share too. A scrap that is
used up leaves a patch of fresh castings.

Smell is a coarse field (2 × 2 cells). Scraps emit in proportion to their mass and decay; the smell diffuses
through the substrate (not through air) and fades with a 15-minute half-life. Worms follow its gradient.

The keeper feeds every 4 hours: two to four scraps spaced apart along the surface, laid on the bedding and
pressed in a little. The keeper checks the top of the bedding every 3 hours and mists it if it is dry. Misting
wets the top layer and raises the humidity, which the glass shows as condensation that clears over the next
half hour or so.

## Checkpoints

A checkpoint holds the whole state:
- the substrate grids;
- every worm, scrap and cocoon;
- the smell field, the random streams, the keeper's schedule and the counters.

It also records the configuration and the state hash, and ends with a checksum over the whole file. Loading
checks the magic, the checksum, the format version, the struct size, the configuration hash and the recomputed
state hash. A resumed bin continues exactly as the original would have (tested).

## Rendering

The bin is a shallow 3D scene: the substrate face, the worms, scraps and cocoons on it, the glass pane in front,
and an oak frame. It is lit by a key light with soft shadows, a warm spot lamp and a cool fill.

- **Substrate** (`substrate.gdshader`) reads the grid as a small texture, sampled bicubically so no cell edges
  show.
  - Materials: the four nearest cells vote with a little noise, which gives irregular, organic boundaries.
  - Bedding: torn strips of soaked cardboard and newsprint (with faint print) at every angle, over coir fibres,
    with compost crumbs mixed in.
  - Compost and soil: cellular crumbs at several scales.
  - Castings: glossy dark granules.
  - Moisture darkens and adds gloss, and burrows are darker, glossy channels.

  Screen-space derivatives are taken once, outside every branch and loop. Inside them neighbouring pixels
  diverge at cell borders, which showed up as a faint grid.
- **Worms** (`worm.gdshader`) are one tube mesh, instanced. The vertex shader bends each instance along its worm's
  body. The points come from a float texture, one row per worm, joined with a Catmull-Rom spline. The body
  radius follows a profile: a pointed head, the clitellum swelling on adults, and a tapering tail. Peristaltic
  waves run along it with distance crawled, the segments are grooved, and the body flattens where it touches the
  pane. The colouring follows *Eisenia fetida*:
  - maroon segments with buff grooves;
  - a paler underside;
  - an orange-tan clitellum on adults;
  - pale pink young worms.

  A clear-coat layer gives the wet sheen.
- **Food** (`food.gdshader`) draws each scrap's shape in a quad. Scraps are eaten away from the edges and in holes
  as their mass drops. They brown, darken and soften (glossier) with decay, and grow white mould in the middle
  stages.
- **Glass** (`glass.gdshader`) adds:
  - a faint room reflection, dust and smudges;
  - condensation that follows the bin's humidity: a light fog and drops, dense in the headspace and thinner below.
- **Live-stream picture:** a second camera renders the same scene offscreen at the stream size (see
  `live_stream.gd`).

## Sound

`WormAudioSynth` renders 48 kHz stereo from simulation events. It uses no samples:

- **Room tone:** a wide, very quiet low rumble with a slow drift, and a whisper of air.
- **Bedding crackle:** sparse soft ticks whose rate follows the number of worms crawling against the glass.
- **Rustles:** when a worm pushes through the bedding near the surface, a dry, papery crackle from band-passed
  noise and clicks.
- **Feeding:** soft, low, wet sounds with a small bubble.
- **Scraps landing:** a soft thump with a scatter of bedding.
- **Misting:** three squeezes of a spray bottle (breathy hiss), then a few droplets running down the glass.

Each category is rate-limited with a token bucket, voices are capped at 24, and a gentle soft limiter keeps the
output below full scale. The app renders the sound once, by the wall clock, and gives the same samples to the
speakers and the live stream.

## Live streaming

`FarmLiveStream` runs `ffmpeg` as a child process. Raw RGBA frames go to its stdin and 16-bit PCM audio goes to
a second pipe. Two writer threads, paced by a steady clock, feed video and audio. They catch up and never skip,
so picture and sound stay in sync. Encoding is H.264 (NVENC, or x264 as a fallback) at a constant bitrate, with
AAC audio, sent to RTMP/RTMPS.

A supervisor restarts `ffmpeg` with back-off (2 s up to 30 s) if the connection drops, and gives up after five
quick failures. Stream keys live in the desktop keyring (`secret-tool`, attributes
`application shadow-worm-farm destination <dest>`). They are passed to `ffmpeg` without appearing in logs, and
anything key-like in ffmpeg's messages is redacted.
