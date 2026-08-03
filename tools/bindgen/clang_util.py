"""libclang plumbing for the dewfall binding generator.

Discovery of the libclang shared library, conversion of clang cursors/types
into the raw IR type descriptors, and comment/annotation extraction.
"""

import glob
import os
import re
import shlex
import subprocess
import sys

from clang import cindex

# ---------------------------------------------------------------------------
# libclang discovery
# ---------------------------------------------------------------------------


def find_libclang():
    """Locate and configure the libclang shared library. Returns the path used."""
    override = os.environ.get("DEW_LIBCLANG_PATH")
    if override:
        if os.path.isdir(override):
            cindex.Config.set_library_path(override)
        else:
            cindex.Config.set_library_file(override)
        return override

    # pip `libclang` wheel bundles the library next to the clang.native package
    try:
        import clang.native  # noqa: F401

        libdir = os.path.dirname(clang.native.__file__)
        for name in ("libclang.dylib", "libclang.so", "libclang.dll"):
            candidate = os.path.join(libdir, name)
            if os.path.exists(candidate):
                cindex.Config.set_library_file(candidate)
                return candidate
    except ImportError:
        pass

    for candidate in (
        "/Library/Developer/CommandLineTools/usr/lib/libclang.dylib",
        "/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/libclang.dylib",
        "/opt/homebrew/opt/llvm/lib/libclang.dylib",
    ):
        if os.path.exists(candidate):
            cindex.Config.set_library_file(candidate)
            return candidate

    sys.exit(
        "error: could not locate libclang. Fix one of:\n"
        "  - pip install libclang>=16\n"
        "  - set DEW_LIBCLANG_PATH to a libclang library file or directory\n"
        "  - install the Xcode command line tools"
    )


def system_include_args():
    """Arguments so libclang finds builtin headers (stddef.h) and the platform SDK.

    The pip libclang wheel ships only the shared library, so the compiler's
    resource include directory and (on macOS) the SDK sysroot must be pointed
    at explicitly. DEW_CLANG_EXTRA_ARGS appends/overrides.
    """
    args = []
    # Clang's builtin headers (stddef.h, stdint.h, stdbool.h). The pip libclang
    # wheel ships only the shared library, so these must come from a clang
    # installation -- or, failing that, from GCC, whose versions of these
    # headers clang parses fine. Without them the parse dies on the first
    # #include <stddef.h>.
    resource_globs = [
        # macOS
        "/Library/Developer/CommandLineTools/usr/lib/clang/*/include",
        "/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/clang/*/include",
        "/opt/homebrew/opt/llvm/lib/clang/*/include",
        # Linux clang. RedHat-family (AlmaLinux, Fedora, RHEL, and so the
        # manylinux images) installs to lib64.
        "/usr/lib64/clang/*/include",
        "/usr/lib/clang/*/include",
        "/usr/lib64/llvm-*/lib/clang/*/include",
        "/usr/lib/llvm-*/lib/clang/*/include",
        # Last resort: GCC's copies of the same freestanding headers.
        "/opt/rh/gcc-toolset-*/root/usr/lib/gcc/*/*/include",
        "/usr/lib/gcc/*/*/include",
        "/usr/lib64/gcc/*/*/include",
    ]
    for pattern in resource_globs:
        matches = sorted(glob.glob(pattern))
        if matches:
            args += ["-isystem", matches[-1]]
            break
    else:
        # Not fatal on Windows, where clang picks the MSVC/SDK headers up from
        # the INCLUDE environment variable instead.
        if sys.platform != "win32":
            print(
                "warning: no clang or gcc builtin-header directory found; the parse will "
                "likely fail on <stddef.h>. Install clang, or set DEW_CLANG_EXTRA_ARGS "
                "to point at one with -isystem.",
                file=sys.stderr,
            )
    if sys.platform == "darwin":
        try:
            sdk = subprocess.run(
                ["xcrun", "--show-sdk-path"], capture_output=True, text=True, check=True
            ).stdout.strip()
            if sdk:
                args += ["-isysroot", sdk]
        except (OSError, subprocess.CalledProcessError):
            pass
    extra = os.environ.get("DEW_CLANG_EXTRA_ARGS")
    if extra:
        args += shlex.split(extra)
    return args


# ---------------------------------------------------------------------------
# type conversion
# ---------------------------------------------------------------------------

_TK = cindex.TypeKind

_BUILTIN_KINDS = {
    _TK.VOID,
    _TK.BOOL,
    _TK.CHAR_U,
    _TK.UCHAR,
    _TK.USHORT,
    _TK.UINT,
    _TK.ULONG,
    _TK.ULONGLONG,
    _TK.CHAR_S,
    _TK.SCHAR,
    _TK.SHORT,
    _TK.INT,
    _TK.LONG,
    _TK.LONGLONG,
    _TK.FLOAT,
    _TK.DOUBLE,
}


def _typedef_name(t):
    """Name of the typedef `t` is spelled as, or None."""
    if t.kind == _TK.TYPEDEF:
        return re.sub(r"\bconst\b\s*", "", t.spelling).strip()
    if t.kind == _TK.ELABORATED:
        return _typedef_name(t.get_named_type())
    return None


def _size_align(canonical):
    try:
        size = canonical.get_size()
        align = canonical.get_align()
    except Exception:  # pragma: no cover - defensive against libclang quirks
        return {"size": None, "align": None}
    return {"size": size if size > 0 else None, "align": align if align > 0 else None}


def type_to_ir(t, registry):
    """Convert a clang Type into a recursive IR type descriptor.

    `registry` carries the name sets collected in the registration pass:
    records, opaques, enums, callbacks, and a `warnings` list.
    """
    canonical = t.get_canonical()
    kind = canonical.kind
    base = {
        "spelling": t.spelling,
        "canonical": canonical.spelling,
        "const": t.is_const_qualified() or canonical.is_const_qualified(),
    }

    typedef = _typedef_name(t)
    if typedef and typedef in registry["callbacks"]:
        return {"kind": "callback", "name": typedef, **base}

    if kind == _TK.CONSTANTARRAY:
        # Prefer the written (sugared) array type so the element keeps its
        # typedef name; flatten nested extents (double view[4][4] -> [4,4]).
        arr = t if t.kind == _TK.CONSTANTARRAY else canonical
        extents = []
        while True:
            arr_canonical = arr if arr.kind == _TK.CONSTANTARRAY else arr.get_canonical()
            if arr_canonical.kind != _TK.CONSTANTARRAY:
                break
            extents.append(arr_canonical.element_count)
            arr = arr_canonical.element_type
        element = type_to_ir(arr, registry)
        if element.get("const"):
            base["const"] = True
        return {"kind": "array", "extents": extents, "element": element, **base, **_size_align(canonical)}

    if kind == _TK.POINTER:
        written = t if t.kind == _TK.POINTER else canonical
        pointee_type = written.get_pointee()
        if pointee_type.get_canonical().kind == _TK.FUNCTIONPROTO:
            # Raw (non-typedef'd) function pointer: record it so semantics can
            # demand an annotation instead of silently mistranslating.
            return {"kind": "callback", "name": None, **base}
        pointee = type_to_ir(pointee_type, registry)
        return {"kind": "pointer", "pointee": pointee, **base}

    if kind == _TK.RECORD:
        name = canonical.get_declaration().spelling
        if name in registry["records"]:
            record_kind = "record"
        elif name in registry["opaques"]:
            record_kind = "opaque"
        else:
            registry["warnings"].append(f"record type '{name}' is not declared in any manifest header")
            record_kind = "record"
        return {"kind": record_kind, "name": name, **base, **_size_align(canonical)}

    if kind == _TK.ENUM:
        return {"kind": "enum", "name": canonical.get_declaration().spelling, **base}

    if kind == _TK.VOID:
        return {"kind": "void", "name": "void", **base}

    if kind in _BUILTIN_KINDS:
        name = re.sub(r"\bconst\b\s*", "", t.spelling).strip()
        if name == "_Bool":  # C mode spells stdbool.h's bool as _Bool
            name = "bool"
        return {"kind": "builtin", "name": name, **base, **_size_align(canonical)}

    registry["warnings"].append(f"unhandled type kind {kind} for '{t.spelling}'")
    return {"kind": "unknown", "name": t.spelling, **base}


# ---------------------------------------------------------------------------
# comments and //= annotations
# ---------------------------------------------------------------------------

_LICENSE_MARKERS = ("GNU Affero", "dewfall - point cloud")
# A directive line, allowing for any comment lead-in: "//", "/*", or the "*"
# continuation of a block comment. A trailing "*/" is not part of the value.
_ANNOTATION_RE = re.compile(
    r"^\s*(?://+|/\*+|\*+)?\s*//=\s*([A-Za-z_][\w.]*)\s*(?::\s*(.*?))?\s*(?:\*/)?\s*$"
)


def _fallback_comment(cursor, file_lines_cache):
    """Backward text scan for the comment block directly above a declaration.

    cursor.raw_comment attachment for plain (non-Doxygen) comments varies
    between libclang versions even with -fparse-all-comments, so this is the
    fallback whenever raw_comment comes back empty.
    """
    location = cursor.extent.start
    if location.file is None:
        return None
    path = location.file.name
    lines = file_lines_cache.get(path)
    if lines is None:
        with open(path, "r", encoding="utf-8") as f:
            lines = f.read().splitlines()
        file_lines_cache[path] = lines
    i = location.line - 2  # index of the line above the declaration
    if i < 0:
        return None
    if lines[i].strip().endswith("*/"):
        j = i
        while j >= 0 and "/*" not in lines[j]:
            j -= 1
        if j < 0:
            return None
        return "\n".join(lines[j : i + 1])
    collected = []
    while i >= 0 and lines[i].strip().startswith("//"):
        collected.insert(0, lines[i])
        i -= 1
    return "\n".join(collected) if collected else None


def _clean_comment_line(line):
    line = line.strip()
    for prefix in ("/**", "/*!", "/*", "///", "//!", "//", "**", "*"):
        if line.startswith(prefix):
            line = line[len(prefix) :]
            break
    if line.endswith("*/"):
        line = line[:-2]
    return line.rstrip()


def extract_doc_and_annotations(cursor, file_lines_cache):
    """Return (doc, annotations) for a declaration cursor.

    Annotation lines (`//= key: value`) are split out of the comment block;
    the remaining prose becomes the doc string. Repeated keys concatenate
    with ", ".
    """
    raw = cursor.raw_comment
    if not raw:
        raw = _fallback_comment(cursor, file_lines_cache)
    if not raw:
        return None, {}
    if any(marker in raw for marker in _LICENSE_MARKERS):
        return None, {}

    annotations = {}
    doc_lines = []
    for line in raw.splitlines():
        match = _ANNOTATION_RE.match(line)
        if match:
            key = match.group(1)
            value = match.group(2) if match.group(2) is not None else True
            if key in annotations and isinstance(value, str) and isinstance(annotations[key], str):
                annotations[key] = annotations[key] + ", " + value
            else:
                annotations[key] = value
        else:
            doc_lines.append(_clean_comment_line(line))

    while doc_lines and not doc_lines[0]:
        doc_lines.pop(0)
    while doc_lines and not doc_lines[-1]:
        doc_lines.pop()
    doc = "\n".join(doc_lines) if doc_lines else None
    return doc, annotations
