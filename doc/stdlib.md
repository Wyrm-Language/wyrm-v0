# Wyrm Standard Library

## Builtin Functions

  - len(x): Return number of elements within a collection. For a str this counts
    Unicode codepoints (not UTF-8 bytes), matching Python's `len`; a list/tuple
    counts items, a dict counts entries (keys), bytes counts bytes, and a Pair
    chain counts cons cells out to the terminating nil (`()` alone is 0). An
    improper list (one whose final cdr isn't nil) has no well-defined length
    and faults, as does any non-collection argument.

## Builtin Primitives

### str

Messages:

  - substr(start: int, count: int) -> str: Return the count Unicode codepoints of
    s starting at 0-based codepoint offset start, e.g. "asdf" ! substr(1, 2) gives
    "sd". A start or count running past either end clips rather than faulting.

### bytes

A heap-allocated, mutable, resizable array of `u8` (0-255). Construction:

  - bytes(n: int) -> bytes: n zero bytes.
  - bytes(s: str) -> bytes: the UTF-8 encoding of s.
  - bytes(b: bytes) -> bytes: a copy of b (also reachable as the `copy` message below).

Indexing: `b[i]` reads byte i as an int 0-255; faults out of range. `b[i] := v` writes byte
i from an int, range-checking both `i` (0..len(b)-1) and `v` (0-255); either violation
faults.

Messages:

  - len(b) -> int: number of bytes (via the builtin `len`, not a message; listed here for
    completeness since every other bytes operation is a message).
  - append(v: int|bytes|str): appends one byte (v as 0-255), or every byte of another
    bytes/UTF-8-encoded str, growing capacity as needed.
  - resize(n: int): sets len to n. Growing pads with zero bytes; shrinking discards the
    tail. Never shrinks capacity.
  - slice(start: int, count: int) -> bytes: a new bytes holding a copy of count bytes
    starting at start. Faults if the range falls outside 0..len(b).
  - to_str() -> str: UTF-8-decodes the full contents. Faults (does not silently substitute
    or truncate) on any byte sequence that is not valid UTF-8, matching Python's
    `bytes.decode("utf-8")` default behavior, which both engines' native implementations
    are built directly on top of.
  - copy() -> bytes: a new bytes holding a copy of the full contents.
  - pack_u8(at: int, v: int): writes v (0-255) as one byte at offset at.
  - pack_i32(at: int, v: int): writes v as 4 little-endian bytes (two's complement) at
    offset at.
  - pack_u32(at: int, v: int): writes v as 4 little-endian bytes at offset at.
  - pack_f32(at: int, v: float): writes v, narrowed to IEEE-754 binary32, as 4
    little-endian bytes at offset at.
  - pack_f64(at: int, v: float): writes v as an IEEE-754 binary64, 8 little-endian bytes,
    at offset at.
  - unpack_u8(at: int) -> int: reads one byte at offset at as 0-255.
  - unpack_i32(at: int) -> int: reads 4 little-endian bytes at offset at as a signed
    two's-complement int.
  - unpack_u32(at: int) -> int: reads 4 little-endian bytes at offset at as an unsigned int.
  - unpack_f32(at: int) -> float: reads 4 little-endian bytes at offset at as IEEE-754
    binary32, widened to wyrm's float.
  - unpack_f64(at: int) -> float: reads 8 little-endian bytes at offset at as IEEE-754
    binary64.

Every `pack_*`/`unpack_*` faults if `at`..`at+width` falls outside `0..len(b)` - it never
resizes the buffer.

`==` compares two bytes values byte-for-byte (same length, same contents); `is bytes`
matches only bytes values, never str. `str(b)` renders as `"<N> bytes"` (N = `len(b)`) -
this mirrors the length-only summary the reference compiler's static-pool disassembly
already prints for a binary constant, and is the format this epic's engines use rather than
inventing a second one.

Example: `bytes(3)!pack_u32(0, 258)` first widens to a 4-byte write starting at offset 0,
but the buffer is only 3 bytes long (`0..3` is not inside `0..len(b)` = `0..3` since the
write needs bytes 0-3 inclusive, i.e. 4 bytes) - this call faults, illustrating the range
check above; `bytes(4)!pack_u32(0, 258)` instead writes `02 01 00 00` (258 = 0x00000102,
little-endian).

Deferred: a `b"..."` literal syntax is not part of this epic. `bytes(str)` already covers
building a bytes value out of source text, and no fixture or spec text needs a literal form
yet.

## Multiprocessing and Messaging

### External Context

The 'thread' builtin accepts a module path as an argument. The module is then
run as an external thread. The function returns a remote namespace:

    context = thread(myprogram::thread)

The thread object:

    class Thread:
        slot id: thread_id

    # Join the thread
    fn [Thread] join(): ...



### Signal

The Signal class:

    class Signal:
        ...

    fn [Signal] emit(obj):
        ...

    fn [Signal] connect(callable):
        ...

The transmitted object _must_ be serializable.
