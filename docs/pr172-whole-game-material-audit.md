# PR 172: whole-pack material audit, 2026-10-12

The starting build (`2057eb2`) has **eight confirmed material/rendering fault
classes** in the supplied Bean pack. Two are fixed by this follow-up; six remain.
This counts distinct implementation faults, not screenshots, draw records or
every object instance. The asset sweep covers the whole supplied pack; it does
not certify visual correctness in every playable scene.

## What was checked

The new `tools/gevr_bean_material_audit.py` compiles a read-only C probe against
the production CAFF reader, texture decoder and triangle loader. It reads every
`default.bin`, including both looks and switch alternatives. Input assets stay
outside the repository. The full local report includes asset hashes and metadata;
the committed `pr172-material-census.json` contains counts and affected names.

- **987 CAFF files:** 499 HD assets and 488 original-look counterparts.
- **7,880 texture records:** every record decoded successfully at its authored
  dimensions. The HD subset contains 3,141 texture records.
- **25,769 material records:** 10,277 belong to HD assets. These include alternate
  pieces, so the counts are not numbers of visible defects.
- **27 motion entries in 23 files:** 13 HD files and 10 original-look files.
- **983 meshes loaded:** the four empty cases are the HD/original `tvscreen` and
  `tv4screen` containers, which keep their native dynamic screen path. They have
  no texture records. No loaded mesh has a missing texture binding.
- Registration and room catalogs were checked against the census so unused pack
  variants are excluded from the registered-asset counts below.

The earlier 84 folder repaints and 121 image repaints also passed against these
local assets. Those checks establish decoding/mapping, not the appearance of
the rendered menus. The generic texture selector was compared with DAB's pinned
selector across the material census; no additional different **valid texture
indices** were found. Special material handling still matters, as the watch and
Complex demonstrate. DAB is a source reference, not a claim of full XBLA parity.

## Confirmed faults and extent

| Fault | Counted extent | State |
| --- | --- | --- |
| Complex water takes a reflection image as its surface | 3 material records resolve to texture 22 instead of surface 23; 4 emitted water faces | **Fixed.** Identify its two animated bump inputs, reflection and surface binding, independent of release/CE shader offsets. Four faces now use the translucent surface; none use the reflection as diffuse art. |
| Texture scrolling is only initialized for backgrounds | Helicopter colour input authors 0.9 repeats/second; the prop never receives that scroll | **Fixed.** Read motion for model/prop files too. The real helicopter has one moving texture at the authored rate. |
| Watch light is discarded | 2 triangles on suitlfhand texture 9 | **Remaining.** `watchTriangleWanted` drops them unconditionally. The release draws the small green/white light. Restoring it also needs its authored colour preserved. |
| Texture rotation is ignored | 11 HD assets author rotation; 7 registered prop assets have affected world effects | **Remaining.** Five Frigate bridge consoles, the door console and helicopter. The other four HD records are AR33/RC-P90 flash variants; the current port deliberately uses native flashes, so they are not counted as four proven broken world objects. |
| Running picture frames are ignored | Complex beacon texture 24, 2 frames at 0.5 seconds/frame; 8 emitted triangles | **Remaining.** It stays on frame zero. Four textures in the whole pack have multiple frames; the other three are two unused water bump inputs and the helicopter disc whose metadata explicitly does not run frames. Do not count all four as four visible frozen animations. |
| Additional reflection/scratch inputs are omitted | 27 registered HD asset families have such bindings; another 4 level surface families need water/polished-floor reflection shaders | **Remaining.** Shared bindings occur on glass, visors, lab glassware, vehicles, the GoldenEye key, CCTV, tanks, oil drums, lamps and missiles. Dam/Complex water, Silo's reflective surface and Control's marble also use multiple inputs. Nintendo's native chrome exception does not implement these passes. |
| Lit models discard authored vertex RGB | 67 mapped HD character/head asset families contain non-white colours in the loaded geometry | **Remaining pipeline fault.** The emitter stores normals in the RGB bytes for `G_LIGHTING`; GPU and CPU lighting replace their authored colour. The 67 names are candidates for visible damage, not 67 confirmed bad-looking models: some coloured pieces are discarded or belong to another outfit. |
| HD fog is only implemented for Dam | 18 additional retail fog stage states in DAB's table | **Remaining fidelity gap.** They retain native fog, not the HD linear line. Includes Surface/Surface 2, Runway, Depot, Jungle, Cradle and indoor stages. Transition-aware object fog and CE overrides also remain unported. This is not a claim that those 18 scenes have no fog at all. |

The fog states are Statue, Control, Archives, Train, Bunker 2, Aztec, Streets,
Depot, Complex, Egyptian, Facility, Runway, Surface, Jungle, Temple, Caverns,
Cradle and Surface 2. Surface 1/2 share a background asset but have separate
environment states. The comparison uses DAB commit
`adef1dc2d4eca1a5c453a7f2bf03a3b6cdad99b8` (`gebean.c` material/animation logic
and `gebeanstage.c` retail fog table), under `port/licenses/dabs-mod.txt`.

## Depth fighting and performance: quantified candidates

The production loader still omits DAB's backed-face, partial fight, room overlap,
decal lift/corner and fully covered blended-face handling. A geometry census
found **2,558 coincident non-decal groups / 5,164 faces across 15 HD background
files**, within 0.001 file units. Dam has **26 groups / 52 faces**, including
24 groups with opposite winding. Frigate has 1,978 groups / 3,956 faces.

These are candidates, not 2,558 proven defects. Two-faced sheets and repeated
passes can be deliberate. The census excludes already-marked decals, tests
only coincident triangles, and does not determine visibility, partial overlap,
room ownership or rounding after room placement. Automatically deleting these
faces could remove visible geometry. The committed JSON lists all 15 files.

Fresh ADB reads succeeded on Quest 3 `2G0YC1ZF8Q0JQ6`. The installed package's
last update is **2026-10-11 21:23:46**, and the pack marker is `2026.10.11.3`.
The retained 04:45 log still reports approximately 711,500 triangles / 8,900
flushes per 72 frames, including 936 room-mesh draws and 432 GPU skin draws,
with zero CPU skin poses and zero rejected mesh chunks in that view. It contains
no moving tunnel/scope capture and no new `bean-paint` diagnostics. It therefore
does not measure the latest build or establish what causes the reported stutter.
No APK was installed or headset files modified.

The next performance investigation should capture a moving tunnel/scope run
with CPU render timing, draw/flush counts, room visibility and texture upload
events. The current evidence supports investigating CPU submission/culling and
material overdraw; it does not justify assigning the stutter to fog or decals
alone. The menu complaint also remains unconfirmed at runtime despite complete
local repaint mapping.

## Validation of this follow-up

The real-file material regression now checks Complex's four translucent water
faces and the helicopter's authored scroll, alongside the existing watch,
Nintendo, Dam glass/alpha and road-paint checks. The full 987-file sweep was
repeated after these loader changes; every texture still decodes, with no new
missing bindings. Full asset hashes/metadata remain in ignored
`build/bean/full-material-audit.json`.

Sanitizer validation also found a signed shift in the SDK fog-mode macro at
the new room-state boundary. The helper now constructs the same blender bits
with unsigned operands; the regression asserts the independent expected word.
This repair changes no intended material behavior and is not counted as another
visible material fault class.

## Signed build

- Clean source commit: `e9821e5187dc1bc62439110e57208dd1ab1ec728`.
- APK: `android/app/build/outputs/apk/release/GoldenEye-VR-PR172-Material-Audit-e9821e5.apk`.
- SHA-256: `03630864965a63fa354519df05923259d27f179b640122e15ee9bc8aa3e896e4`.
- Exact native symbols and archive metadata: `android/app/build/outputs/symbols/af96520913944a30ecdbe042d22bda1ed7bfe619/`.
- Release build succeeds with JDK 20. Existing JVM tests are cached/up-to-date;
  this follow-up has no Java changes. Bean regression and real-file material
  checks pass with address/undefined sanitizers; undefined errors halt the
  regression run. The packaged native stamp is verified as `e9821e5`.
- Archive metadata records `dirty: false`, `installed: false`, `published: false`.
  Headset appearance and stutter improvement have not been validated on this APK.
