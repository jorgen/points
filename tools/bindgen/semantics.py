"""Raw -> semantic enrichment for the dewfall binding IR.

Pure python (no libclang dependency): consumes the `raw` layer and derives
the object-oriented `semantic` layer that every generator consumes. The
semantic layer is generator-agnostic -- it names things (`bound_name`) and
classifies parameter *roles*; it never mentions Python/nanobind/C++ details.

Classification is deliberately strict: anything it cannot classify with
confidence becomes an error demanding a `//=` annotation in the header,
so API growth can never be silently dropped or mistranslated.
"""

import keyword
import re

import ir

_PAIR_RE = re.compile(r"([A-Za-z_]\w*)\s*\[\s*([A-Za-z_]\w*)\s*\]")
_INT_CANONICALS = {
    "short",
    "unsigned short",
    "int",
    "unsigned int",
    "long",
    "unsigned long",
    "long long",
    "unsigned long long",
    "signed char",
    "unsigned char",
}
_STRING_LEN_SUFFIXES = ("_size", "_len", "_length")


# ---------------------------------------------------------------------------
# naming
# ---------------------------------------------------------------------------


def strip_type_stem(name):
    """dew_converter_data_source_t -> converter_data_source"""
    stem = name
    if stem.startswith("dew_"):
        stem = stem[4:]
    if stem.endswith("_t"):
        stem = stem[:-2]
    return stem


def camel(snake):
    return "".join(part.capitalize() for part in snake.split("_") if part)


def handle_prefix(handle):
    """dew_converter_t -> dew_converter_"""
    stem = handle[:-2] if handle.endswith("_t") else handle
    return stem + "_"


def method_bound_name(function_name, handle):
    """Strip the longest token-aligned prefix shared with the handle's prefix.

    dew_converter_data_get_attribute_name @ dew_converter_data_source_t
      -> get_attribute_name (shares dew_converter_data)
    """
    class_tokens = handle_prefix(handle).rstrip("_").split("_")
    fn_tokens = function_name.split("_")
    shared = 0
    while shared < min(len(class_tokens), len(fn_tokens) - 1) and class_tokens[shared] == fn_tokens[shared]:
        shared += 1
    if shared == 0:
        return None
    return "_".join(fn_tokens[shared:])


def _valid_identifier(name):
    return bool(name) and name.isidentifier() and not keyword.iskeyword(name)


# ---------------------------------------------------------------------------
# type predicates on IR type descriptors
# ---------------------------------------------------------------------------


def _pointee(t):
    return t["pointee"] if t["kind"] == "pointer" else None


def _is_opaque_ptr(t, name=None):
    p = _pointee(t)
    return bool(p) and p["kind"] == "opaque" and (name is None or p["name"] == name)


def _is_error_ptr(t):
    return _is_opaque_ptr(t, "dew_error_t")


def _is_error_ptr_ptr(t):
    p = _pointee(t)
    return bool(p) and p["kind"] == "pointer" and p["pointee"].get("name") == "dew_error_t"


def _is_integer(t):
    if t["kind"] != "builtin":
        return False
    canonical = t["canonical"].replace("const ", "").strip()
    return canonical in _INT_CANONICALS


def _is_char_ptr(t, const):
    p = _pointee(t)
    return bool(p) and p["kind"] == "builtin" and p["name"] == "char" and bool(p["const"]) == const


def _is_void_ptr(t):
    p = _pointee(t)
    return bool(p) and p["kind"] == "void"


# ---------------------------------------------------------------------------
# annotations
# ---------------------------------------------------------------------------


def _ann_list(annotations, key):
    value = annotations.get(key)
    if not isinstance(value, str):
        return []
    return [item.strip() for item in value.split(",") if item.strip()]


def _ann_pairs(annotations, key):
    """'buffers[buffer_count], x[n]' -> {'buffers': 'buffer_count', 'x': 'n'}"""
    value = annotations.get(key)
    if not isinstance(value, str):
        return {}
    return {m.group(1): m.group(2) for m in _PAIR_RE.finditer(value)}


def _is_skipped(entity):
    return entity.get("annotations", {}).get("bind") == "skip"


# ---------------------------------------------------------------------------
# function classification
# ---------------------------------------------------------------------------


def _string_len_index(params, consumed, i):
    """Index of the length param paired with the string param at i, or None."""
    if i + 1 >= len(params) or consumed[i + 1]:
        return None
    stem = params[i]["name"]
    next_name = params[i + 1]["name"]
    if not _is_integer(params[i + 1]["type"]):
        return None
    for suffix in _STRING_LEN_SUFFIXES:
        if next_name == stem + suffix:
            return i + 1
    # generic fallback: str/str_len style where the stem repeats
    if next_name.startswith(stem) and next_name != stem:
        return i + 1
    return None


def _classify_function(fn, class_handle, is_constructor, ctx):
    """Return (args, results, error_convention, error_param, self_param, problems)."""
    annotations = fn["annotations"]
    arrays_map = _ann_pairs(annotations, "arrays")
    string_map = _ann_pairs(annotations, "string")
    out_string_map = _ann_pairs(annotations, "out_string")
    forced_out = set(_ann_list(annotations, "out"))
    forced_in = set(_ann_list(annotations, "in"))
    nullable = set(_ann_list(annotations, "nullable"))

    params = fn["params"]
    index_of = {p["name"]: idx for idx, p in enumerate(params)}
    consumed = [False] * len(params)
    args, results, problems = [], [], []
    error_convention = ir.ERROR_NONE
    error_param = None
    self_param = None
    return_consumed = False
    fn_tokens = set(fn["name"].split("_"))

    def add_arg(role, i, c_params, type_desc, **extra):
        entry = {
            "bound_name": params[i]["name"],
            "role": role,
            "c_params": c_params,
            "type": type_desc,
            "nullable": params[i]["name"] in nullable,
        }
        entry.update(extra)
        args.append(entry)
        for c in c_params:
            consumed[c] = True

    def add_result(role, i, c_params, type_desc, **extra):
        entry = {"bound_name": params[i]["name"], "source": "param", "role": role, "c_params": c_params, "type": type_desc}
        entry.update(extra)
        results.append(entry)
        for c in c_params:
            consumed[c] = True

    for i, param in enumerate(params):
        if consumed[i]:
            continue
        t = param["type"]
        name = param["name"]

        # self
        if i == 0 and not is_constructor and class_handle and _is_opaque_ptr(t, class_handle):
            self_param = 0
            consumed[0] = True
            continue

        # error conventions (never on the Error class's own methods)
        if class_handle != "dew_error_t":
            if _is_error_ptr_ptr(t):
                error_convention = ir.ERROR_OUT_OWNED
                error_param = i
                consumed[i] = True
                continue
            if _is_error_ptr(t) and name == "error":
                error_convention = ir.ERROR_ARG_CALLER_OWNED
                error_param = i
                consumed[i] = True
                continue

        # annotated out_string: buf[size]
        if name in out_string_map:
            size_name = out_string_map[name]
            size_index = index_of.get(size_name)
            if size_index is None:
                problems.append(f"out_string references unknown param '{size_name}'")
                continue
            add_result("out_string", i, [i, size_index], t, returns_length=True)
            return_consumed = True
            continue

        # annotated buffer arrays: ptr[count]
        if name in arrays_map:
            count_name = arrays_map[name]
            count_index = index_of.get(count_name)
            if count_index is None:
                problems.append(f"arrays references unknown param '{count_name}'")
                continue
            element = _pointee(t) or t
            add_arg(
                "buffer_array_in",
                i,
                [i, count_index],
                t,
                element_type=element,
                count_type=params[count_index]["type"],
            )
            continue

        # strings: const char* + length (annotated or inferred)
        if name in string_map or _is_char_ptr(t, const=True):
            length_index = index_of.get(string_map[name]) if name in string_map else _string_len_index(params, consumed, i)
            if length_index is not None:
                add_arg("string_in", i, [i, length_index], t, len_type=params[length_index]["type"])
                continue
            if name in string_map:
                problems.append(f"string annotation references unknown length for '{name}'")
                continue
            problems.append(f"param '{name}': const char* without a recognizable length param; annotate //= string: or //= arrays:")
            continue

        # bare callback function pointer (+ optional trailing user_ptr)
        if t["kind"] == "callback":
            c_params = [i]
            if i + 1 < len(params) and _is_void_ptr(params[i + 1]["type"]) and params[i + 1]["name"].endswith("user_ptr"):
                c_params.append(i + 1)
            add_arg("callback_fn", i, c_params, t, callback=t["name"], has_user_ptr=len(c_params) == 2)
            continue

        # by-value struct of callbacks (+ optional trailing user_ptr)
        if t["kind"] == "record" and t["name"] in ctx["callback_structs"]:
            c_params = [i]
            if i + 1 < len(params) and _is_void_ptr(params[i + 1]["type"]) and params[i + 1]["name"].endswith("user_ptr"):
                c_params.append(i + 1)
            add_arg("callback_struct", i, c_params, t, callback_struct=t["name"], has_user_ptr=len(c_params) == 2)
            continue

        # fixed-extent arrays
        if t["kind"] == "array":
            if t["const"] or name in forced_in:
                add_arg("array_in", i, [i], t)
            elif name in forced_out or "get" in fn_tokens:
                add_result("array_out", i, [i], t)
            else:
                problems.append(f"param '{name}': non-const fixed array direction is ambiguous; annotate //= in: or //= out:")
            continue

        # plain values
        if t["kind"] == "enum":
            add_arg("enum", i, [i], t)
            continue
        if t["kind"] == "builtin":
            add_arg("scalar", i, [i], t)
            continue
        if t["kind"] == "record":
            add_arg("struct_in", i, [i], t, by_value=True)
            continue

        if t["kind"] == "pointer":
            pointee = t["pointee"]
            if pointee["kind"] == "opaque":
                add_arg("handle_in", i, [i], t, handle=pointee["name"])
                continue
            if pointee["kind"] == "record":
                if pointee["const"] or name in forced_in:
                    add_arg("struct_in", i, [i], t, by_value=False)
                elif name in forced_out or "get" in fn_tokens:
                    add_result("struct_out", i, [i], t)
                else:
                    problems.append(
                        f"param '{name}': non-const struct pointer direction is ambiguous; annotate //= in: or //= out:"
                    )
                continue
            if pointee["kind"] == "void":
                problems.append(f"param '{name}': raw void* is unbindable; annotate or //= bind: skip the function")
                continue
            if pointee["kind"] in ("builtin", "enum"):
                if pointee["const"] or name in forced_in:
                    problems.append(f"param '{name}': const scalar pointer of unknown length; annotate //= arrays:")
                    continue
                if _is_char_ptr(t, const=False):
                    problems.append(f"param '{name}': mutable char* is ambiguous; annotate //= out_string: or //= arrays:")
                    continue
                add_result("out_param", i, [i], t)
                continue
            if pointee["kind"] == "pointer":
                # Only a borrowed-string out-param is understood (const char**,
                # as in dew_error_get_info). Any other T** is ambiguous -- it
                # could be an out-handle, an array of pointers, or an in/out --
                # so demand an annotation rather than guessing.
                inner = pointee["pointee"]
                if inner["kind"] == "builtin" and inner["name"] == "char" and inner["const"]:
                    add_result("out_param", i, [i], t, borrowed=True)
                else:
                    problems.append(
                        f"param '{name}': pointer-to-pointer '{t['spelling']}' is ambiguous; annotate or //= bind: skip"
                    )
                continue

        problems.append(f"param '{name}': unclassifiable type '{t['spelling']}'")

    # return value
    rt = fn["return_type"]
    if is_constructor:
        pass  # the constructor's result IS the new handle; error convention covers failure
    elif return_consumed:
        # The return value IS the out_string length, so it must actually be one.
        # Anything else (a status enum, a bool) would be silently swallowed and
        # then reinterpreted as a character count.
        if not (_is_integer(rt) or (rt["kind"] == "builtin" and rt["canonical"].startswith("unsigned"))):
            problems.append(
                f"out_string requires an integer length return, got '{rt['spelling']}'; "
                f"the status/return value would be silently discarded"
            )
    elif rt["kind"] != "void":
        role = {
            "builtin": "scalar",
            "enum": "enum",
            "record": "struct_value",
        }.get(rt["kind"])
        if role is None and rt["kind"] == "pointer":
            problems.append(f"return type '{rt['spelling']}' needs //= returns: annotation")
        elif role is None:
            problems.append(f"unclassifiable return type '{rt['spelling']}'")
        else:
            results.insert(0, {"bound_name": "return", "source": "return", "role": role, "c_params": [], "type": rt})

    return args, results, error_convention, error_param, self_param, problems


def _is_blocking(fn, is_destructor):
    if is_destructor:
        return True
    if fn["annotations"].get("blocking") is True:
        return True
    return "wait" in fn["name"].split("_")


# ---------------------------------------------------------------------------
# entity building
# ---------------------------------------------------------------------------


def _method_entry(fn, bound_name, class_handle, is_constructor, ctx):
    args, results, error_convention, error_param, self_param, problems = _classify_function(
        fn, class_handle, is_constructor, ctx
    )
    entry = {
        "function": fn["name"],
        "bound_name": fn["annotations"].get("rename") or bound_name,
        "header": fn["header"],
        "line": fn["line"],
        "doc": fn["doc"],
        "annotations": fn["annotations"],
        "blocking": _is_blocking(fn, is_destructor=False),
        "error_convention": error_convention,
        "error_param": error_param,
        "self_param": self_param,
        "args": args,
        "results": results,
    }
    return entry, problems


def _enum_semantic(enum):
    names = [v["name"] for v in enum["values"]]
    token_lists = [n.split("_") for n in names]
    shared = 0
    if len(names) > 1:
        limit = min(len(tokens) for tokens in token_lists) - 1
        while shared < limit and len({tokens[shared] for tokens in token_lists}) == 1:
            shared += 1
    stripped = ["_".join(tokens[shared:]) for tokens in token_lists]
    if not all(_valid_identifier(s) for s in stripped):
        stripped = [n[4:] if n.startswith("dew_") else n for n in names]
    return {
        "c_name": enum["name"],
        "bound_name": enum["annotations"].get("rename") or camel(strip_type_stem(enum["name"])),
        "module": enum["module"],
        "header": enum["header"],
        "doc": enum["doc"],
        "annotations": enum["annotations"],
        "values": [
            {"c_name": v["name"], "bound_name": s, "value": v["value"], "doc": v["doc"]}
            for v, s in zip(enum["values"], stripped)
        ],
    }


def _struct_semantic(struct, ctx):
    fields = []
    for field in struct["fields"]:
        entry = {
            "c_name": field["name"],
            "bound_name": field["name"],
            "type": field["type"],
            "doc": field["doc"],
            "annotations": field["annotations"],
        }
        pairs = _ann_pairs(field["annotations"], "arrays")
        if field["name"] in pairs:
            entry["count_field"] = pairs[field["name"]]
        fields.append(entry)
    return {
        "c_name": struct["name"],
        "bound_name": struct["annotations"].get("rename") or camel(strip_type_stem(struct["name"])),
        "module": struct["module"],
        "header": struct["header"],
        "doc": struct["doc"],
        "annotations": struct["annotations"],
        "is_callback_struct": struct["name"] in ctx["callback_structs"],
        "fields": fields,
    }


# ---------------------------------------------------------------------------
# entry point
# ---------------------------------------------------------------------------


def enrich(raw):
    """Return (semantic, errors, warnings)."""
    errors, warnings = [], []

    records = {s["name"]: s for s in raw["structs"]}
    ctx = {
        "callback_structs": {
            name for name, s in records.items() if s["fields"] and all(f["type"]["kind"] == "callback" for f in s["fields"])
        }
    }

    skipped = {
        "functions": [f["name"] for f in raw["functions"] if _is_skipped(f)],
        "classes": [o["name"] for o in raw["opaque_types"] if _is_skipped(o)],
        "structs": [s["name"] for s in raw["structs"] if _is_skipped(s)],
        "callbacks": [c["name"] for c in raw["callbacks"] if _is_skipped(c)],
    }
    skipped_classes = set(skipped["classes"])
    skipped_functions = set(skipped["functions"])
    skipped_structs = set(skipped["structs"])

    classes = {}
    for opaque in raw["opaque_types"]:
        if opaque["name"] in skipped_classes:
            continue
        classes[opaque["name"]] = {
            "handle_type": opaque["name"],
            "bound_name": opaque["annotations"].get("rename") or camel(strip_type_stem(opaque["name"])),
            "module": opaque["module"],
            "header": opaque["header"],
            "doc": opaque["doc"],
            "annotations": opaque["annotations"],
            "constructors": [],
            "destructor": None,
            "methods": [],
        }

    free_functions = []
    for fn in raw["functions"]:
        if fn["name"] in skipped_functions:
            continue
        location = f"{fn['header']}:{fn['line']} {fn['name']}"

        # explicit class override
        override = fn["annotations"].get("class")
        handle = None
        is_constructor = False
        if isinstance(override, str):
            handle = override
        else:
            rt = fn["return_type"]
            if rt["kind"] == "pointer" and rt["pointee"]["kind"] == "opaque":
                candidate = rt["pointee"]["name"]
                if fn["name"].startswith(handle_prefix(candidate) + "create"):
                    handle = candidate
                    is_constructor = True
            if handle is None and fn["params"]:
                first = fn["params"][0]["type"]
                if _is_opaque_ptr(first):
                    handle = first["pointee"]["name"]

        if handle is not None and handle in skipped_classes:
            warnings.append(f"{location}: dropped because its class {handle} is bind: skip")
            continue
        if handle is not None and handle not in classes:
            errors.append(f"{location}: refers to unknown handle type '{handle}'")
            continue

        if handle is None:
            entry, problems = _method_entry(fn, fn["name"].removeprefix("dew_"), None, False, ctx)
            for problem in problems:
                errors.append(f"{location}: {problem}")
            free_functions.append(entry)
            continue

        cls = classes[handle]
        prefix = handle_prefix(handle)

        if is_constructor:
            entry, problems = _method_entry(fn, cls["bound_name"], handle, True, ctx)
            suffix = fn["name"][len(prefix) + len("create") :].lstrip("_")
            entry["create_suffix"] = suffix
            for problem in problems:
                errors.append(f"{location}: {problem}")
            cls["constructors"].append(entry)
            continue

        if fn["name"] == prefix + "destroy" and len(fn["params"]) == 1:
            cls["destructor"] = {
                "function": fn["name"],
                "blocking": True,
                "doc": fn["doc"],
                "annotations": fn["annotations"],
            }
            continue

        bound = method_bound_name(fn["name"], handle)
        if bound is None:
            errors.append(f"{location}: cannot derive a method name for class {handle}; add //= rename:")
            continue
        entry, problems = _method_entry(fn, bound, handle, False, ctx)
        entry["blocking"] = _is_blocking(fn, is_destructor=False)
        for problem in problems:
            errors.append(f"{location}: {problem}")
        cls["methods"].append(entry)

    # consistency: no surviving method may reference a skipped struct
    def references_skipped(type_desc):
        if type_desc.get("name") in skipped_structs:
            return type_desc["name"]
        for key in ("pointee", "element"):
            if key in type_desc:
                found = references_skipped(type_desc[key])
                if found:
                    return found
        return None

    for cls in classes.values():
        for method in cls["constructors"] + cls["methods"]:
            for item in method["args"] + method["results"]:
                found = references_skipped(item["type"])
                if found:
                    errors.append(
                        f"{method['function']}: uses skipped struct '{found}'; skip the function or unskip the struct"
                    )

    # A collision would silently shadow one binding with another, so it is an
    # error: resolve with //= rename:.
    def check_unique(names, what):
        seen_names = {}
        for bound, origin in names:
            if bound in seen_names:
                errors.append(f"{what}: bound name '{bound}' claimed by both {seen_names[bound]} and {origin}")
            else:
                seen_names[bound] = origin

    for cls in classes.values():
        check_unique(
            [(m["bound_name"], m["function"]) for m in cls["methods"]]
            + [(c["create_suffix"], c["function"]) for c in cls["constructors"] if c["create_suffix"]],
            f"class {cls['bound_name']}",
        )
    semantic_enums = [_enum_semantic(e) for e in raw["enums"] if not _is_skipped(e)]
    semantic_structs = [_struct_semantic(s, ctx) for s in raw["structs"] if not _is_skipped(s)]
    check_unique(
        [(c["bound_name"], c["handle_type"]) for c in classes.values()]
        + [(e["bound_name"], e["c_name"]) for e in semantic_enums]
        + [(s["bound_name"], s["c_name"]) for s in semantic_structs]
        + [(m["name"].removeprefix("DEW_"), m["name"]) for m in raw["macro_constants"] if not _is_skipped(m)],
        "module scope",
    )
    for semantic_enum in semantic_enums:
        check_unique(
            [(v["bound_name"], v["c_name"]) for v in semantic_enum["values"]], f"enum {semantic_enum['bound_name']}"
        )

    semantic = {
        "classes": list(classes.values()),
        "free_functions": free_functions,
        "enums": semantic_enums,
        "value_structs": semantic_structs,
        "constants": [
            {
                "macro": m["name"],
                "bound_name": m["name"].removeprefix("DEW_"),
                "value": m["value"],
                "module": m["module"],
                "doc": m["doc"],
            }
            for m in raw["macro_constants"]
            if not _is_skipped(m)
        ],
        "skipped": skipped,
    }
    return semantic, errors, warnings
