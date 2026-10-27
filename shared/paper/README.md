# The paper engine

A painter of paper for anything printed on in PlaySuite: the help document and the
Settings screen are the first two sheets, and any game that shows a page, a card, a
label, a notebook or a wrapped parcel can declare `"engines": ["paper"]` and link
`paper_core`.

## Why it looks like paper

Studied from book, copy, recycled, laid, Bible, parchment, cotton rag and kraft papers,
and from the decisions of the Pocket Bible interview (a private sibling project that
chose its page material, ink and night mode in detail). The interview's choices shaped
the defaults: the paper is *quiet* (you notice it only when you look for it), there is
no show-through, foxing or ageing, the light is fixed, and the picture is "pregenerated
paper with minor noise displacement": a fibre mat plus value noise, which is instant,
rather than the felt engine's traced, shaded fibres, which take a second a tile.

Machine-made printing paper, the book, copy and recycled presets, is a flat mat and
nothing else: no relief is lit, because a lit relief reads as embossing, which paper
is not.

| In the sheet | How it is made |
|---|---|
| The fibre web | Thousands of tiny fibres per 10,000 square points (`web`), each about `web_length` points long and a tenth of a point wide, gently curved, laid over one another at random or, the more `machine_grain` says, along the machine direction. Their coverage is accumulated as density: where more overlap the sheet is denser and whiter, where fewer do it shows through greyer, by up to `web_contrast` of full scale. |
| Formation | The pulp settled unevenly: three octaves of value noise `mottle_size` points across (8 to 9 for machine paper, tens for handmade) move the tone by up to `mottle`. |
| Grain | A per-pixel speckle of `grain`. |
| Flecks | Recycled stock: specks of unbleached fibre and old ink, `flecks` per 10,000 square points, under a point across, darker by `fleck_contrast`. |
| Fibre inclusions | Handmade papers: long light and dark strands on the surface, `fibres` per 10,000 square points, `fibre_length` points long, `fibre_contrast` deep. Zero for machine paper. |
| Tooth | Handmade and rag papers: a fine relief lit from the upper left, shifting a pixel by about `tooth`; `machine_grain` stretches it along x. Zero for machine paper. |
| Laid paper | Wire lines `laid_spacing` points apart down the sheet and chain lines `chain_spacing` apart across it, each a soft pulse `laid_depth` deep. |
| Parchment | Broad patches (`blotch`) of darkening, balanced so the sheet keeps its colour on average. |
| Darkening warms | With `warmth` at 1 a darker patch keeps its reds and gives up blues, as pigment in paper does; at 0 it darkens in grey. |
| A bound page | A sheet may carry a shadow into the gutter at either side and a faint fall-off toward every edge (`Shading`). |

Everything is sized in points. Give `pixels_per_point` the window's scale and the same
parameters make the same paper, feature for feature, at 1x and 2x.

## Interface (`src/paper.hpp`)

| Piece | Role |
|---|---|
| `Parameters` | `pixels_per_point`; `colour` (a flat, open patch comes out exactly at it); `web`, `web_length`, `web_contrast`; `mottle`, `mottle_size`; `grain`; `machine_grain`; `flecks`, `fleck_contrast`; `fibres`, `fibre_length`, `fibre_contrast`; `tooth`, `tooth_size`; `laid_spacing`, `chain_spacing`, `laid_depth`; `blotch`, `blotch_size`; `warmth`; `seed`. Out-of-range values are clamped and non-numbers replaced. |
| `preset(Preset)` | Ten starting points. Machine-made: `book` (uncoated cream book paper, the shell's choice), `copy` (bright, slightly cool office paper), `recycled` (greyer, flecked). Handmade and decorative: `wove`, `laid`, `india` (thin, smooth, slightly grey Bible paper), `parchment`, `rag` (bright cotton with tooth and fibres), `kraft` (brown wrapping paper), `night` (dark warm paper for light ink). |
| `generate_tile(parameters, width, height, out)` | A picture that repeats seamlessly in both directions, 4 to 4096 pixels a side. Mottle, tooth and lines are fitted to whole periods across it. |
| `generate_sheet(parameters, width, height, shading, out)` | One sheet of any size, not repeating, with a `Gutter` shadow (`gutter_width`, `gutter_depth`) and an edge fall-off (`edge_width`, `edge_depth`). |
| `Image` | Straight-alpha sRGB bytes (red, green, blue, alpha), alpha always 255. |

Generation is deterministic: the same parameters and seed give the same bytes on every
platform (integer hashing, no fast-math, `-ffp-contract=off`). Failure leaves `out`
untouched.

## Cost

Measured on an M-series laptop (Release; `paper_tests` prints these):

| Picture | Time |
|---|---|
| 256 × 256 book tile at 1x | about 12 ms |
| 512 × 512 book tile at 2x (a 256-point tile on a Retina display) | about 23 ms |
| 1024 × 1024 book tile at 2x | about 90 ms |
| A handmade preset (no web) at any of these | 1 to 10 ms |

The fibre web is the cost: its count follows the area in points, so a 2x tile draws
the same fibres over four times the pixels. Make a tile once per display scale and
keep it; a sheet of any size is then one pattern fill. The shell does exactly that
(`src/paper_sheet.*`): a 256-point book tile in the help document's cream, made the
first time the help book or the Settings sheet is shown and drawn with the painter's
`fill_image_pattern`. Nothing is generated per frame.

## Seeing it

`paper_swatches sheet.ppm [side] [pixels_per_point] [zoom]` writes every preset as a
tile shown twice across (so the seam can be judged), then a bound sheet with a gutter
and edge shading, with each picture's time. `paper_swatches sheet.ppm 96 1 4` shows the
fibres and tooth pixel by pixel.

## Tests (`tests/paper_tests.cpp`, CTest `paper_engine`)

Determinism of every preset and a different seed giving different paper; opacity and
size; a sheet with every amount at zero being exactly its colour; every preset keeping
its colour on average, textured but quiet, with India paper smoother than rag; seams
across four seeds of every preset; what each layer does alone (mottle slow, grain fine,
fibres sparse and two-toned, tooth zero on average, laid and chain lines repeating at
their spacing, blotches balanced, warmth sparing the blue channel, the web flat on
average and more even when denser); machine-made book paper far quieter than rag; the
same paper at
2x; gutter and edge shading at both scales; refusal of bad sizes leaving the picture
alone and clamping of bad parameters; and the cost at three sizes.

## Limits

- The web is density, not geometry: there is no show-through of a printed reverse,
  no translucency and no edge deckle.
- Where a tooth is lit (handmade presets) the light is baked from the upper left,
  which is how people expect a page to be lit.
- Text is the toolkit's business. Dark text blended in gamma-encoded colour over light
  textured paper can look a little thinner than with linear-light blending; the request
  for that option is in `docs/TOOLKIT_REQUESTS.md`.
