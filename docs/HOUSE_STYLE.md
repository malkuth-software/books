# House style

The collection's C++ is written in one style, which its author calls orthodox
and ultra-transparent: code in which a reader can see every type, every step and
every owner without running it in their head. It refuses some conveniences of
modern C++ on purpose and admits others. Part of the aim is readability and part
is performance: patterns that keep the compiler from doing something large and
surprising.

Every file you write for a game follows it: `src`, `tests`, `tools`, `dev`.
Files you borrow unchanged from another game are exempt; list them under
`borrowed` in `GAME.json`.

The template is written in this style. When unsure, write it the way the
template does.

## Checked by machine

`scripts/check-style.py` finds the spellings below, and the gate runs it on your
files. It is a spelling check. It cannot see ownership, lifetimes or kernel
design; those are yours to get right.

| Rule | Instead |
|---|---|
| No `auto` (and no `decltype(auto)`) | Spell the type: `const std::size_t index = ...`, `std::map<Key, Mask>::const_iterator found = ...` |
| No `->` anywhere, including trailing return types | `(*pointer).member` |
| No lambdas, including small predicates | A named function or a small named functor |
| No structured bindings | Name each value: `const int row = cell / side;` |
| No `std::ranges` or `std::views` | A loop, or a standard algorithm with a named predicate |
| No coroutines | |
| No defaulted comparison operators | Write the comparison a type needs, or none |

## Types and values

- Spell local, parameter, iterator and return types. Use a descriptive alias when
  a type is long.
- Initialize every variable where it is declared.
- Choose precision once. Game arithmetic is `double` from end to end; pixels and
  colour channels in the rasteriser are `float` because that is what it stores.
  Do not mix them inside one computation.
- Name fields; group related data in a `struct`; use `enum class` for
  alternatives. Use designated initializers for independent options
  (`Options{.hosted = true}`).
- Keep apart quantities that are easily confused: points and device pixels,
  cells and coordinates, seconds and frames. Name them so
  (`width_points`, `device_width`).
- Say what an array is: its shape and order ("side * side cells, row-major").
  Mind signed and unsigned: index with `std::size_t`, compute with `int`, and
  convert explicitly with `static_cast`.

## Statements

- Write in the order things happen. Assignments, conditions, counted loops,
  early returns, named calls.
- One recognizable operation per expression. Name intermediate values that mean
  something.
- Calculate, then return: `const bool ok = decode(body, session); return ok;`
- Keep side effects in their own statements so their order is visible.
- Write the loop when order, indices or bounds explain the algorithm. Use
  `std::min`, `std::max`, `std::clamp`, `std::find`, `std::accumulate` and their
  kin when they say exactly what you mean.
- Mark a deliberately ignored result: `static_cast<void>(press(board, cell));`

## Functions and data

- Say what goes in, what comes out, and what may be changed. Read-only
  parameters are `const`. Outputs are named as outputs (`destination`, `out`).
- Mark results that must not be dropped `[[nodiscard]]`.
- A function that can fail says what it leaves behind when it does. `decode`
  leaves its destination untouched on failure; `write_save` leaves the previous
  file.
- Validate everything that comes from outside (a save file, an environment
  variable, a pointer position) before using it. Assert what your own code
  guarantees.
- Fill storage the caller supplies for bulk work; return small values directly.

## Ownership

- Every piece of data has one owner and a lifetime. Persistent state and
  reusable workspaces are members of the object that owns them.
- `std::unique_ptr` for sole ownership; `std::shared_ptr` only where lifetime is
  truly shared (controls and live surfaces are, by the toolkit's design).
- Pass `const T&` to observe, `T&` to change. A reference kept across a frame, a
  callback or a container growth must still be valid then: a mask reference from
  `text_mask` is good until `text_cache_trim()`, and no longer.
- `std::vector` for things that grow, fixed arrays for real fixed bounds,
  `std::string` for text. Allocate substantial storage before the repeated work
  and reuse it.

## Repeated work

For anything that runs per pixel, per cell or per body:

- handle empty and short inputs first;
- decide the mode once, before the loop, and run one plain loop for it;
- no allocation, growth, logging, locking or string building inside the loop;
- read fixed parameters into locals; accumulate locally and store once;
- keep branches that guard against invalid access or needless work. Do not
  replace them with arithmetic tricks;
- no blanket fast-math. Reordering floating-point arithmetic changes results,
  and the collection's games are deterministic.

## Callbacks and threads

- A callback has a named target and explicit context: a member function bound
  with `gf::on(event, *this, &View::tick)`, whose subscription the owner holds and
  revokes when it is disposed. Controls made in a loop bind an index, never a
  pointer search. `subscribe` with a kept token is for a connection that must be
  cancelled on its own. See [controls](04-controls.md).
- `std::function` only where a callable must be stored and owned, with a named
  target.
- Asynchronous work is named, has a completion the owner polls, and is waited
  for on shutdown. Mutable state belongs to one thread. See
  [performance](06-performance.md) before adding a thread.

## Abstractions

Admitted: templates and `constexpr` for declared choices, `std::span`,
`std::optional`, `std::variant` with named visitors, standard containers and
algorithms, exceptions at real failure boundaries.

Refused: general `std::any` plumbing, home-made containers and allocators,
callable frameworks, portability layers written "in case", accessors and
wrappers that add no rule.

Write the game you have. Do not build an engine around it.

## Layout and comments

- `.clang-format` at the repository root is the formatting: four spaces, 100
  columns, attached braces, `Type* pointer` and `Type& reference`. Run
  `clang-format` on your files if you have it.
- One namespace per game, closed with `}  // namespace <ns>`.
- File-local helpers go in an unnamed namespace at the top of the `.cpp`.
- A header begins with a short comment saying what the file is for and what it
  does not do. Read the template's.
- Comments explain what the code cannot: why a surprising step exists, what a
  dimension or unit is, what a lifetime depends on, where a boundary is. They do
  not narrate. Remove one when it stops being true.
- Names are words. `layout`, `visual`, `device_width`. Short mathematical names
  (`x`, `dx`, `row`) inside a few lines of arithmetic are fine.

## Python

The kit's tools and a game's sound scripts are Python 3 with the standard
library (sound may use NumPy). Give functions type hints, keep scripts
deterministic, and make them runnable from the repository root.
