#!/usr/bin/env python3
"""Stage 1 of the dewfall binding generator.

Parses the public C headers (listed in public_headers.txt) with libclang and
emits dew_api.json: a `raw` layer (faithful extraction) plus a `semantic`
layer (derived object-oriented view, computed by semantics.py). The JSON is
the contract consumed by every generator (nanobind today, an inline C++
wrapper generator later).
"""

import argparse
import functools
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import clang_util
import ir
import semantics

from clang import cindex

CursorKind = cindex.CursorKind


def load_manifest(manifest_path, source_root):
    """Parse the manifest into (entries, explicitly_excluded_paths).

    A line beginning with '!' names a public header deliberately left out of
    the IR; it must still be acknowledged so audit_public_headers can tell
    "excluded on purpose" from "forgotten".
    """
    entries = []
    excluded = set()
    with open(manifest_path, "r", encoding="utf-8") as f:
        for raw_line in f:
            line = raw_line.split("#", 1)[0].strip()
            if not line:
                continue
            if line.startswith("!"):
                path = os.path.realpath(os.path.join(source_root, line[1:].strip()))
                excluded.add(path)
                continue
            module, _, include = line.partition("/")
            path = os.path.realpath(os.path.join(source_root, line))
            if not os.path.isfile(path):
                sys.exit(f"error: manifest header not found: {path}")
            entries.append({"module": module, "include": include, "path": path})
    if not entries:
        sys.exit(f"error: manifest is empty: {manifest_path}")
    return entries, excluded


def audit_public_headers(source_root, entries, excluded):
    """Warn about public headers that are in the tree but not in the manifest.

    Cursors outside the manifest are dropped silently during extraction, so an
    unlisted header contributes nothing to the IR and no count tripwire fires --
    the API would just quietly not exist in Python.
    """
    listed = {e["path"] for e in entries} | excluded
    warnings = []
    for module in sorted(os.listdir(source_root)):
        public_root = os.path.join(source_root, module, "dew")
        if not os.path.isdir(public_root):
            continue
        for directory, _, files in os.walk(public_root):
            for name in sorted(files):
                if not name.endswith((".h", ".hpp")):
                    continue
                path = os.path.realpath(os.path.join(directory, name))
                if path not in listed:
                    relative = os.path.relpath(path, source_root)
                    warnings.append(
                        f"public header '{relative}' is in neither the manifest nor its "
                        f"exclusion list; add it, or add '!{relative}' to acknowledge the omission"
                    )
    return warnings


def parse_translation_unit(entries, source_root):
    umbrella = "".join(f"#include <{e['include']}>\n" for e in entries)
    modules = list(dict.fromkeys(e["module"] for e in entries))
    clang_args = [
        "-x",
        "c",
        "-std=c11",
        "-fparse-all-comments",
        "-DDEW_COMMON_STATIC_DEFINE",
        "-DDEW_CONVERTER_STATIC_DEFINE",
        "-DDEW_RENDER_STATIC_DEFINE",
    ]
    clang_args += clang_util.system_include_args()
    clang_args += [f"-I{os.path.join(source_root, module)}" for module in modules]

    index = cindex.Index.create()
    tu = index.parse(
        "dew_bindgen_umbrella.c",
        args=clang_args,
        unsaved_files=[("dew_bindgen_umbrella.c", umbrella)],
        options=cindex.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD,
    )

    errors = [d for d in tu.diagnostics if d.severity >= cindex.Diagnostic.Error]
    for diagnostic in tu.diagnostics:
        severity = "error" if diagnostic.severity >= cindex.Diagnostic.Error else "warning"
        print(f"clang {severity}: {diagnostic}", file=sys.stderr)
    if errors:
        sys.exit(f"error: {len(errors)} clang error(s) parsing the public headers")
    return tu, clang_args, modules


@functools.lru_cache(maxsize=None)
def _realpath(path):
    return os.path.realpath(path)


def _manifest_entry(cursor, filemap):
    location_file = cursor.location.file
    if location_file is None:
        return None
    return filemap.get(_realpath(location_file.name))


def _is_callback_typedef(cursor):
    canonical = cursor.underlying_typedef_type.get_canonical()
    return (
        canonical.kind == cindex.TypeKind.POINTER
        and canonical.get_pointee().kind == cindex.TypeKind.FUNCTIONPROTO
    )


def _meta(cursor, entry, cache):
    doc, annotations = clang_util.extract_doc_and_annotations(cursor, cache)
    return {
        "name": cursor.spelling,
        "header": entry["include"],
        "module": entry["module"],
        "line": cursor.location.line,
        "doc": doc,
        "annotations": annotations,
    }


def _build_enum(cursor, registry, meta, cache):
    values = []
    for child in cursor.get_children():
        if child.kind != CursorKind.ENUM_CONSTANT_DECL:
            continue
        tokens = [token.spelling for token in child.get_tokens()]
        doc, _ = clang_util.extract_doc_and_annotations(child, cache)
        values.append(
            {"name": child.spelling, "value": child.enum_value, "explicit": "=" in tokens, "doc": doc}
        )
    return {**meta, "values": values}


def _build_struct(cursor, registry, meta, cache):
    fields = []
    for child in cursor.get_children():
        if child.kind != CursorKind.FIELD_DECL:
            continue
        doc, annotations = clang_util.extract_doc_and_annotations(child, cache)
        fields.append(
            {
                "name": child.spelling,
                "type": clang_util.type_to_ir(child.type, registry),
                "doc": doc,
                "annotations": annotations,
            }
        )
    size_align = clang_util._size_align(cursor.type.get_canonical())
    return {**meta, "fields": fields, **size_align}


def _build_callback(cursor, registry, meta):
    canonical = cursor.underlying_typedef_type.get_canonical()
    proto = canonical.get_pointee()
    proto_types = list(proto.argument_types())
    param_cursors = [c for c in cursor.get_children() if c.kind == CursorKind.PARM_DECL]
    params = []
    if len(param_cursors) == len(proto_types):
        for index, param in enumerate(param_cursors):
            params.append(
                {"name": param.spelling or f"arg{index}", "type": clang_util.type_to_ir(param.type, registry)}
            )
    else:  # pragma: no cover - defensive; typedef children normally match
        for index, param_type in enumerate(proto_types):
            params.append({"name": f"arg{index}", "type": clang_util.type_to_ir(param_type, registry)})
    return {**meta, "return_type": clang_util.type_to_ir(proto.get_result(), registry), "params": params}


def _build_function(cursor, registry, meta):
    params = []
    for index, argument in enumerate(cursor.get_arguments()):
        params.append(
            {"name": argument.spelling or f"arg{index}", "type": clang_util.type_to_ir(argument.type, registry)}
        )
    return {
        **meta,
        "export_macro": f"DEW_{meta['module'].upper()}_EXPORT",
        "return_type": clang_util.type_to_ir(cursor.result_type, registry),
        "params": params,
        "variadic": cursor.type.is_function_variadic(),
    }


def _build_macro_constant(cursor, meta):
    tokens = list(cursor.get_tokens())
    if len(tokens) != 2:
        return None
    literal = tokens[1].spelling
    if not (literal.startswith('"') and literal.endswith('"')):
        return None
    return {**meta, "value": literal[1:-1], "value_kind": "string"}


def extract_raw(tu, filemap):
    cache = {}
    tops = []
    for cursor in tu.cursor.get_children():
        entry = _manifest_entry(cursor, filemap)
        if entry is not None:
            tops.append((cursor, entry))

    registry = {"records": set(), "opaques": set(), "enums": set(), "callbacks": set(), "warnings": []}
    forwards = set()
    for cursor, _ in tops:
        if cursor.kind == CursorKind.STRUCT_DECL:
            if cursor.is_definition():
                registry["records"].add(cursor.spelling)
            else:
                forwards.add(cursor.spelling)
        elif cursor.kind == CursorKind.ENUM_DECL and cursor.is_definition():
            registry["enums"].add(cursor.spelling)
        elif cursor.kind == CursorKind.TYPEDEF_DECL and _is_callback_typedef(cursor):
            registry["callbacks"].add(cursor.spelling)
    registry["opaques"] = forwards - registry["records"]

    raw = {
        "enums": [],
        "opaque_types": [],
        "structs": [],
        "callbacks": [],
        "functions": [],
        "macro_constants": [],
    }
    seen = set()

    def once(bucket, name):
        key = (bucket, name)
        if key in seen:
            return False
        seen.add(key)
        return True

    for cursor, entry in tops:
        if cursor.kind == CursorKind.ENUM_DECL and cursor.is_definition():
            if once("enum", cursor.spelling):
                raw["enums"].append(_build_enum(cursor, registry, _meta(cursor, entry, cache), cache))
        elif cursor.kind == CursorKind.STRUCT_DECL:
            if cursor.is_definition():
                if once("struct", cursor.spelling):
                    raw["structs"].append(_build_struct(cursor, registry, _meta(cursor, entry, cache), cache))
            elif cursor.spelling in registry["opaques"]:
                if once("opaque", cursor.spelling):
                    raw["opaque_types"].append(_meta(cursor, entry, cache))
        elif cursor.kind == CursorKind.TYPEDEF_DECL and _is_callback_typedef(cursor):
            if once("callback", cursor.spelling):
                raw["callbacks"].append(_build_callback(cursor, registry, _meta(cursor, entry, cache)))
        elif cursor.kind == CursorKind.FUNCTION_DECL:
            if once("function", cursor.spelling):
                raw["functions"].append(_build_function(cursor, registry, _meta(cursor, entry, cache)))
        elif cursor.kind == CursorKind.MACRO_DEFINITION:
            if once("macro", cursor.spelling):
                macro = _build_macro_constant(cursor, _meta(cursor, entry, cache))
                if macro is not None:
                    raw["macro_constants"].append(macro)

    return raw, registry


def check_annotations(raw):
    warnings = []
    def scan(entity, context):
        for key in entity.get("annotations", {}):
            if "." in key:
                continue  # consumer-specific namespaced key, passes through
            if key not in ir.KNOWN_ANNOTATIONS:
                warnings.append(f"{context}: unknown annotation key '{key}'")

    for bucket, entities in raw.items():
        for entity in entities:
            scan(entity, f"{entity['header']}:{entity['line']} {entity['name']}")
            for field in entity.get("fields", []):
                scan(field, f"{entity['header']} {entity['name']}.{field['name']}")
    return warnings


def report_counts(raw):
    warnings = []
    actual = {
        "functions": len(raw["functions"]),
        "opaque_types": len(raw["opaque_types"]),
        "enums": len(raw["enums"]),
        "structs": len(raw["structs"]),
        "callbacks": len(raw["callbacks"]),
        "macro_constants": len(raw["macro_constants"]),
    }
    summary = "  ".join(f"{key}={value}" for key, value in actual.items())
    print(f"bindgen: {summary}", file=sys.stderr)
    for key, expected in ir.EXPECTED_COUNTS.items():
        if actual[key] != expected:
            warnings.append(
                f"count tripwire: {key} = {actual[key]}, expected {expected} "
                f"(update ir.EXPECTED_COUNTS if the API deliberately changed)"
            )
    return warnings


def build(source_root, manifest_path):
    """Full pipeline; returns (document, warnings, errors). Used by main() and tests."""
    source_root = os.path.realpath(source_root)
    clang_util.find_libclang()
    entries, excluded = load_manifest(manifest_path, source_root)
    filemap = {e["path"]: e for e in entries}

    tu, clang_args, modules = parse_translation_unit(entries, source_root)
    raw, registry = extract_raw(tu, filemap)

    warnings = list(registry["warnings"])
    warnings += audit_public_headers(source_root, entries, excluded)
    warnings += check_annotations(raw)
    warnings += report_counts(raw)

    semantic, errors, semantic_warnings = semantics.enrich(raw)
    warnings += semantic_warnings

    try:
        from importlib.metadata import version

        libclang_version = version("libclang")
    except Exception:
        libclang_version = None

    module_headers = {}
    for entry in entries:
        module_headers.setdefault(entry["module"], []).append(entry["include"])

    document = {
        "ir_version": ir.IR_VERSION,
        "producer": {
            "script": "parse_headers.py",
            "libclang_package": libclang_version,
            "arguments": clang_args,
        },
        "modules": [{"name": module, "headers": module_headers[module]} for module in modules],
        "raw": raw,
        "semantic": semantic,
    }
    return document, warnings, errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", required=True, help="path to the repo's src/ directory")
    parser.add_argument("--manifest", required=True, help="path to public_headers.txt")
    parser.add_argument("--output", help="path to write dew_api.json")
    parser.add_argument(
        "--check", action="store_true", help="parse and classify only; no output written; nonzero exit on problems"
    )
    args = parser.parse_args()
    if not args.check and not args.output:
        parser.error("--output is required unless --check is given")

    document, warnings, errors = build(args.source_root, args.manifest)

    for warning in warnings:
        print(f"bindgen warning: {warning}", file=sys.stderr)
    for error in errors:
        print(f"bindgen error: {error}", file=sys.stderr)
    if errors:
        sys.exit(
            f"error: {len(errors)} declaration(s) could not be classified; "
            "add //= annotations (or bind: skip) to the headers"
        )
    if args.check:
        if warnings:
            sys.exit(f"--check: {len(warnings)} warning(s)")
        print("bindgen: check passed", file=sys.stderr)
        return

    with open(args.output, "w", encoding="utf-8") as f:
        json.dump(document, f, indent=2)
        f.write("\n")
    print(f"bindgen: wrote {args.output}", file=sys.stderr)


if __name__ == "__main__":
    main()
