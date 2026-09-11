# Pure 68K FreeMIDI 1.45 inert probe plan

This is a static specification for a later controlled observation. It is not
an implementation, build recipe, or installation instruction. No proprietary
resource fork is copied here and no probe was built or installed.

## Scope and decision

A pure 68K inert DDef is independently plausible. The authentic DDef resources
begin with executable 68K prefixes, and the System Extension's direct call is
68K. The probe therefore needs no PPC PEF, XCOF, USB Manager, Name Registry,
allocation, port publication, or persistent callback. It must not attempt the
MOTU Mixed Mode bridge.

The safe experiment is a lifecycle/rejection probe: answer setup and teardown,
return an inert selector-7 descriptor prefix, and reject selector 9 without
installing a callback. It must not claim a port or retain a pointer. This is a
feasibility result, not an assertion that FreeMIDI will accept every chosen
status code.

## Authenticated packaging facts

The later file should carry Finder type/creator metadata and BNDL/FREF/signature
and version resources matching one authentic 1.45 DDef package. The corpus
establishes these concrete forms:

| driver | Finder target | creator | DDef ID | prefix bytes |
|---|---|---|---:|---|
| MOTU | `DDef` | `mUSB` | 128 | `60 0a 00 00 44 44 65 66 00 80 00 00` |
| Roland | `DDef` | `RdSU` | 0 | `60 0a 00 00 44 44 65 66 00 00 00 00` |
| SCC | `DDef` | `SCCd` | 128 | `60 0a 00 00 44 44 65 66 00 80 00 00` |
| InterApplication | `DDef` | `APPd` | 128 | `60 0a 00 00 44 44 65 66 00 80 00 00` |

The first four bytes branch over the self-identifying bytes; the resource
body is executable 68K code. The ID is not universal: the System Extension
itself statically requests `DDef` ID 1 in one resource-loading path, while
authentic driver files use 0 and 128. Consequently the exact accepted ID for
a new inert file is the one remaining packaging fact requiring one dynamic
observation. This is why this document is a probe plan rather than a
byte-accurate Rez specification.

## Entry behavior justified by code

The direct binary frame is:

```text
a6+8  long par1
a6+c  word selector
a6+e  long par2
return d0 low word; caller removes 10 bytes; callee executes rts
```

The probe's pure 68K entry should implement only the observed lifecycle calls:

| selector | inert behavior | basis |
|---:|---|---|
| 1 | return a deterministic setup status without allocation or retained state; `0` is the conservative first value, but acceptance is unresolved | SCC saves/returns its service result while InterApplication's handler returns `1`; the corpus proves no universal selector-1 success code |
| 7 | write creator at `par2+0`, `0x0100` at `+4`, null icon handle at `+6`, zero `+a/+b`, and a valid empty Pascal name at `+c`; return zero | Roland, SCC, and InterApplication independently write this prefix; the rest of the caller-copied 268 bytes remains zero |
| 9 | do not dereference the callback value at `par2+4`; return zero | the System Extension treats the result as a boolean and authentic DDef implementations may omit selector 9; this avoids the unresolved relocation and callback lifetime |
| 8 | return zero and release no external state | System Extension teardown follows the call by disposing its resource pointer; the inert probe owns none |

The creator and resource ID must be chosen consistently with the package
metadata. A null icon is the least stateful choice, but whether FreeMIDI
requires a non-null icon for acceptance is not proven. The 268-byte caller
copy is verified; the meanings of the remaining bytes are not.

## Facts requiring one dynamic observation

The remaining facts for a byte-accurate, accepted packaging specification are
one FreeMIDI 1.45 discovery/setup run with an authentic inert file in a cloned
environment, capturing:

1. the candidate file's Finder type, creator, and current resource file;
2. the resource ID requested at the `Get1Resource('DDef', id)` boundary;
3. whether the chosen selector-1 status (`0` first) is accepted, whether
   selector-7/9/8 zero returns are accepted, and the selector-7 output bytes
   through offset `0x117`.

The exact debugger stop is CODE1 `+0x27d30` (the `dc.w $aa52` candidate service)
with the pre-trap stack and candidate object, followed by the loaded
instruction corresponding to `+0x1d112` if selector 9 is reached. The latter
captures the relocated value of the literal `0xc1a2`; it is not needed by the
inert selector-9 rejection path but resolves the production callback question.

This plan therefore establishes pure 68K probe feasibility independently of
Mixed Mode while explicitly listing the remaining acceptance/status facts
rather than inventing them.
