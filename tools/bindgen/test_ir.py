"""Assertions about the binding IR parsed from the real dewfall headers.

Run:  .venv/bin/python -m pytest tools/bindgen/test_ir.py
These lock in the inference rules (array extents, class grouping, error
conventions, blocking, string pairing) so libclang or header drift fails
loudly instead of silently mistranslating the API.
"""

import os
import pathlib
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import ir
import parse_headers

_REPO = os.path.realpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))


@pytest.fixture(scope="session")
def document():
    document, warnings, errors = parse_headers.build(
        os.path.join(_REPO, "src"), os.path.join(_REPO, "tools", "bindgen", "public_headers.txt")
    )
    assert errors == [], errors
    assert warnings == [], warnings
    return document


@pytest.fixture(scope="session")
def raw(document):
    return document["raw"]


@pytest.fixture(scope="session")
def api(document):
    """The whole document, which is what the C++ generators consume."""
    return document


@pytest.fixture(scope="session")
def semantic(document):
    return document["semantic"]


def _function(raw, name):
    return next(f for f in raw["functions"] if f["name"] == name)


def _class(semantic, bound_name):
    return next(c for c in semantic["classes"] if c["bound_name"] == bound_name)


def _method(cls, bound_name):
    return next(m for m in cls["methods"] if m["bound_name"] == bound_name)


def test_counts(raw):
    for key, expected in ir.EXPECTED_COUNTS.items():
        assert len(raw[key]) == expected, key


def test_fixed_array_extents(raw):
    fn = _function(raw, "dew_camera_get_view_matrix")
    assert fn["params"][1]["type"]["kind"] == "array"
    assert fn["params"][1]["type"]["extents"] == [16]

    fn = _function(raw, "dew_camera_look_at")
    eye = fn["params"][1]["type"]
    assert eye["kind"] == "array" and eye["extents"] == [3] and eye["const"]

    frame_camera = next(s for s in raw["structs"] if s["name"] == "dew_frame_camera_t")
    view = next(f for f in frame_camera["fields"] if f["name"] == "view")
    assert view["type"]["extents"] == [4, 4]


def test_docs_attached(raw):
    fn = _function(raw, "dew_converter_set_node_point_limit")
    assert fn["doc"] and "octree node" in fn["doc"]
    fn = _function(raw, "dew_converter_set_tree_scale")
    assert fn["doc"] and "octree coordinate scale" in fn["doc"]


def test_annotations_round_trip(raw):
    fn = _function(raw, "dew_converter_add_data_file")
    assert fn["annotations"].get("arrays") == "buffers[buffer_count]"
    stats = next(s for s in raw["structs"] if s["name"] == "dew_converter_stats_t")
    attrs = next(f for f in stats["fields"] if f["name"] == "attributes")
    assert attrs["annotations"].get("arrays") == "attributes[attribute_count]"


def test_converter_class(semantic):
    converter = _class(semantic, "Converter")
    assert converter["handle_type"] == "dew_converter_t"
    assert converter["destructor"]["function"] == "dew_converter_destroy"
    assert converter["destructor"]["blocking"] is True

    suffixes = {c["create_suffix"] for c in converter["constructors"]}
    assert suffixes == {"", "with_connection", "with_destination"}
    primary = next(c for c in converter["constructors"] if c["create_suffix"] == "")
    assert primary["error_convention"] == ir.ERROR_OUT_OWNED

    cache_filename = primary["args"][0]
    assert cache_filename["role"] == "string_in"
    assert cache_filename["len_type"]["name"] == "uint64_t"

    wait_idle = _method(converter, "wait_idle")
    assert wait_idle["blocking"] is True

    add_data_file = _method(converter, "add_data_file")
    assert add_data_file["args"][0]["role"] == "buffer_array_in"
    assert add_data_file["args"][0]["c_params"] == [1, 2]

    runtime = _method(converter, "set_runtime_callbacks")
    assert runtime["args"][0]["role"] == "callback_struct"
    assert runtime["args"][0]["has_user_ptr"] is True

    file_callbacks = _method(converter, "set_file_converter_callbacks")
    assert file_callbacks["args"][0]["role"] == "callback_struct"
    assert file_callbacks["args"][0]["has_user_ptr"] is False


def test_converter_data_source_class(semantic):
    cds = _class(semantic, "ConverterDataSource")
    ctor = next(c for c in cds["constructors"] if c["create_suffix"] == "")
    assert ctor["error_convention"] == ir.ERROR_ARG_CALLER_OWNED
    assert [a["role"] for a in ctor["args"]] == ["string_in", "handle_in"]

    name_method = _method(cds, "get_attribute_name")
    out = next(r for r in name_method["results"] if r["role"] == "out_string")
    assert out["c_params"] == [2, 3]
    assert out["returns_length"] is True

    request = _method(cds, "request_aabb")
    assert request["args"][0]["role"] == "callback_fn"
    assert request["args"][0]["has_user_ptr"] is True

    tight = _method(cds, "get_tight_aabb")
    assert [r["role"] for r in tight["results"]] == ["array_out", "array_out"]

    memory_stats = _method(cds, "get_memory_stats")
    assert len([r for r in memory_stats["results"] if r["role"] == "out_param"]) == 7


def test_camera_class(semantic):
    camera = _class(semantic, "Camera")
    look_at_aabb = _method(camera, "look_at_aabb")
    assert look_at_aabb["args"][0]["role"] == "struct_in"

    get_view = _method(camera, "get_view_matrix")
    assert get_view["results"][0]["role"] == "array_out"
    set_view = _method(camera, "set_view_matrix")
    assert set_view["args"][0]["role"] == "array_in"

    props = _method(camera, "perspective_properties")
    assert [r["role"] for r in props["results"]] == ["out_param"] * 4

    arcball = _class(semantic, "Arcball")
    ctor = arcball["constructors"][0]
    assert [a["role"] for a in ctor["args"]] == ["handle_in", "array_in"]


def test_enums(semantic):
    by_name = {e["bound_name"]: e for e in semantic["enums"]}
    types = by_name["Type"]
    assert [v["bound_name"] for v in types["values"]][:3] == ["u8", "i8", "u16"]

    components = by_name["Components"]
    assert [v["bound_name"] for v in components["values"]] == [
        "components_1",
        "components_2",
        "components_3",
        "components_4",
        "components_4x4",
    ]

    compression = by_name["ConverterCompression"]
    assert {v["bound_name"]: v["value"] for v in compression["values"]} == {"none": 0, "zstd": 2, "huff0": 3}


def test_bool_gated_stats_getter(semantic):
    converter = _class(semantic, "Converter")
    stats = _method(converter, "get_compression_stats")
    assert stats["results"][0]["source"] == "return"
    assert stats["results"][0]["type"]["name"] == "bool"
    assert stats["results"][1]["role"] == "struct_out"


def test_free_functions(semantic):
    laszip = next(f for f in semantic["free_functions"] if f["function"] == "dew_laszip_callbacks")
    assert laszip["results"][0]["role"] == "struct_value"
    assert laszip["results"][0]["type"]["name"] == "dew_converter_file_convert_callbacks_t"


def test_constants(semantic):
    constants = {c["bound_name"]: c["value"] for c in semantic["constants"]}
    assert constants["ATTRIBUTE_XYZ"] == "xyz"
    assert len(constants) == ir.EXPECTED_COUNTS["macro_constants"]


def test_py_skip_passthrough(semantic):
    renderer = _class(semantic, "Renderer")
    # py.skip is consumer-specific: the semantic layer keeps these, tagged.
    frame = _method(renderer, "frame")
    assert frame["annotations"].get("py.skip") is True
    assert _class(semantic, "Buffer")["annotations"].get("py.skip") is True


def test_data_source_get_returns_token_struct(semantic):
    cds = _class(semantic, "ConverterDataSource")
    get = _method(cds, "get")
    assert get["results"][0]["role"] == "struct_value"
    assert get["results"][0]["type"]["name"] == "dew_data_source_t"


def test_awaitable_annotations(semantic):
    # The `//= awaitable:` annotation is the whole protocol: keep looking until poll() stops
    # reporting `pending`. Both generated wrappers (C++ bindings/cpp/dew/await.hpp, and dew.aio) rest on
    # it, so a drift here is a drift in both at once.
    request = _class(semantic, "Request")
    assert request["awaitable"] == {
        "poll": "dew_request_status",
        "pending": "dew_request_pending",
        "release": "dew_request_release",
    }
    dataset = _class(semantic, "Dataset")
    assert dataset["awaitable"]["poll"] == "dew_dataset_state"
    assert dataset["awaitable"]["pending"] == "dew_dataset_opening"
    # A dataset is closed, not released: `release` is optional and absent here.
    assert dataset["awaitable"]["release"] is None
    # Nothing else claims to be awaitable; a stray annotation would silently grow the generated API.
    awaitables = sorted(c["bound_name"] for c in semantic["classes"] if c.get("awaitable"))
    assert awaitables == ["Dataset", "Request"]


def test_generated_dewpp_header_is_up_to_date(semantic, api):
    # Same reasoning as the await header: bindings/cpp/dew/dewpp.hpp is checked in because generating
    # it needs libclang and an ordinary C++ build must not, so it can drift from the C API without
    # anything noticing. It wraps the WHOLE surface, so a drift is a whole missing method.
    import generate_cpp

    root = pathlib.Path(__file__).resolve().parents[2]
    checked_in = root / "bindings" / "cpp" / "dew" / "dewpp.hpp"
    assert checked_in.exists(), f"{checked_in} is missing"
    expected = generate_cpp.Generator(api).generate()
    assert checked_in.read_text() == expected, (
        "bindings/cpp/dew/dewpp.hpp is stale -- rerun tools/bindgen/generate_cpp.py"
    )


def test_dewpp_wraps_the_whole_surface(api):
    # The generator SKIPS anything whose shape it cannot express, and a silent skip is how a wrapper
    # quietly stops covering the API. Pin the exception list so a new one has to be looked at.
    import generate_cpp

    generator = generate_cpp.Generator(api)
    generator.generate()
    assert [fn for _, fn in generator.problems] == ["dew_request_copy_attribute"], (
        f"unexpected unwrapped functions: {generator.problems}"
    )


def test_dewpp_rejects_a_name_collision(api):
    # Enums, structs and handles are named independently from their own bound_name, so two of them
    # CAN land on the same dewpp:: name. Left unchecked that is a redeclaration error inside generated
    # code, pointing nowhere near the header that caused it.
    import copy

    import generate_cpp

    doctored = copy.deepcopy(api)
    enums = doctored["semantic"]["enums"]
    classes = doctored["semantic"]["classes"]
    enums[0]["bound_name"] = classes[-1]["bound_name"]
    with pytest.raises(generate_cpp.EmitError, match="declared twice"):
        generate_cpp.Generator(doctored).generate()


def test_generated_cpp_await_header_is_up_to_date(semantic):
    # bindings/cpp/dew/await.hpp is CHECKED IN, because generating it needs libclang and an ordinary
    # C++ build must not. That means it can go stale against the annotations, and this is what notices --
    # otherwise the first symptom would be a consumer awaiting a handle the header does not know.
    import generate_cpp_await

    root = pathlib.Path(__file__).resolve().parents[2]
    checked_in = root / "bindings" / "cpp" / "dew" / "await.hpp"
    assert checked_in.exists(), f"{checked_in} is missing"
    expected = generate_cpp_await.generate(semantic)
    assert checked_in.read_text() == expected, (
        "bindings/cpp/dew/await.hpp is stale -- rerun tools/bindgen/generate_cpp_await.py"
    )


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-q"]))
