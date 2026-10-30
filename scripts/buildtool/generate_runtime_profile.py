"""Generate one build's public C++ disc profile; never reads game content.

Identity comes from regions.json; audited runtime metadata comes from the profile
it names. Missing addresses/layouts are configuration errors, not PAL fallbacks.
"""

import argparse
import json
import re
from pathlib import Path

# The remaining features still contain PAL addresses/layouts. Move a feature to
# REQUIREMENTS only after its implementation uses translated addresses/layouts.
LEGACY_FEATURES = (
    "ControlsMenu",
    "FrameRate",
    "DualWield",
    "SoundDebug",
    "AdvancedInput",
    "Widescreen",
    "InventoryTypography",
    "SceneHooks",
    "UiPrompts",
    "Menus",
    "Subtitles",
)
BASELINE = (0x101710, 0x4C3798, 0x4CE028, 0x518E94, 0x4B91D8, 0x516F80, 0x517000, 0x517050, 0x56DF00, 0x4FB60C)
REQUIREMENTS = {
    "BasicInput": (0x4B0948, 0x4B0BA8, 0x4B0EF8, 0x4B0F78, 0x4B1490, 0x4B1548, 0x4FB60C),
    "MoviePacing": (0x1767A0,),
    "MovieGuard": (0x440610, 0x4FB600),
    "StereoSync": (0x4DB4F0, 0x4FB6D0),
    "InputFlow": (0x1E3970, 0x4FB60C, 0x4FBAFC, 0x5565D4),
    "OptionsSettings": (0x4424E0,),
    "MovementInput": (0x4E90C0, 0x131FA8, 0x4AF7F8),
    "DirectMouseLook": (0x44E4A8, 0x41F848, 0x13EE90, 0x4E8C48, 0x4E9594, 0x4E9558, 0x32CDA8, 0x276E40),
}
FEATURES = (*LEGACY_FEATURES, *REQUIREMENTS)
MOVIE_FIELDS = ("fps_index", "frame_count", "screen_entries", "screen_count", "fps_numerator", "fps_denominator")
AUDIO_FIELDS = ("option_stereo", "manager_stereo")
FLOW_FIELDS = (
    "screen_id",
    "title_screen",
    "pause_screen",
    "menu_screens",
    "cursor_screens",
    "context_camera",
    "handle_inner",
    "inner_data",
    "camera_mode",
    "gameplay_modes",
)
CAMERA_FIELDS = (
    "context_camera",
    "context_actor",
    "context_resources",
    "context_targets",
    "context_target_count",
    "handle_inner",
    "inner_data",
    "handle_resource",
    "resource_count",
    "resource_table",
    "resource_stride",
    "resource_class",
    "dragon_class",
    "camera_flags",
    "camera_pitch",
    "camera_alternate_pitch",
    "camera_alternate_yaw",
    "camera_pitch_rate",
    "camera_yaw_rate",
    "camera_roll_rate",
    "camera_transform",
    "camera_mode",
    "camera_swing",
    "camera_swing_target",
    "camera_alternate_view",
    "camera_follow_rate",
    "camera_blend_timer",
    "player_transform",
    "player_flags",
    "player_flags2",
    "player_target",
    "player_aim_anchor",
    "player_anchor_y",
    "player_vtable",
    "vtable_turn",
    "vtable_turn_this",
    "vtable_pitch",
    "vtable_pitch_this",
    "transform_position",
    "transform_right",
    "transform_back",
    "camera_orbit",
)
OBSERVER_FIELDS = (
    "rynn_class",
    "npc_self",
    "npc_health",
    "npc_max_health",
    "player_weapon",
    "player_active_weapon",
    "player_attack_state",
    "player_guard_flags",
    "player_animation",
    "dragon_state",
    "stats_hp",
    "stats_max_hp",
    "walk_mask",
    "guard_block_mask",
    "dragon_primary_mask",
    "dragon_secondary_mask",
    "transform_back_y",
    "transform_back_z",
)
SETTINGS_FIELDS = ("widget_id", "adjust_screen_widget", "adjust_screen_text")


def load_profile(registry_path, root, region):
    registry = json.loads(registry_path.read_text(encoding="utf-8"))
    if region not in registry["regions"]:
        raise ValueError(f"Unknown disc profile {region!r}; choose from {', '.join(registry['regions'])}")
    identity = registry["regions"][region]
    runtime_path = root / identity["runtime_profile"]
    runtime = json.loads(runtime_path.read_text(encoding="utf-8"))
    validate(region, identity, runtime)
    return identity, runtime


def number(value):
    return int(value, 0) if isinstance(value, str) else value


def validate(region, identity, runtime):
    if not re.fullmatch(r"[a-z][a-z0-9_-]*", region):
        raise ValueError("Invalid disc profile id")
    if identity["vmode"] not in ("PAL", "NTSC"):
        raise ValueError("Unknown video mode")
    identity_addresses = runtime["identity_addresses"]
    if type(identity_addresses) is not bool:
        raise ValueError("identity_addresses must be a boolean")
    # Only this exact reference executable may use identity translation and the
    # unported features. A new disc cannot silently inherit PAL object layouts.
    if (
        identity_addresses
        and identity["elf_sha256"] != "7d790b35733e3e7204df8db84f0ce55a81aa667d9b75c5f121066fc565883b8e"
    ):
        raise ValueError("Identity addresses require the verified reference ELF")
    features = runtime["features"]
    if len(features) != len(set(features)) or set(features) - set(FEATURES):
        raise ValueError("Duplicate or unknown feature")
    if not identity_addresses and set(features) & set(LEGACY_FEATURES):
        raise ValueError("Unported features still require the reference ELF layouts")
    if "BasicInput" in features and "AdvancedInput" in features:
        raise ValueError("BasicInput and AdvancedInput install competing pad hooks")
    if (
        set(features) & {"BasicInput", "AdvancedInput", "OptionsSettings", "DirectMouseLook"}
        and "InputFlow" not in features
    ):
        raise ValueError("Settings and basic input require InputFlow")
    if "DirectMouseLook" in features and not set(features) & {"BasicInput", "AdvancedInput"}:
        raise ValueError("DirectMouseLook requires an input integration")
    if "MovementInput" in features and "DirectMouseLook" not in features:
        raise ValueError("MovementInput requires DirectMouseLook")
    if type(runtime.get("language_count")) is not int or not 1 <= runtime["language_count"] <= 5:
        raise ValueError("Invalid language count")
    for feature, name, fields in (
        ("InputFlow", "input_flow", FLOW_FIELDS),
        ("OptionsSettings", "settings", SETTINGS_FIELDS),
        ("DirectMouseLook", "camera", CAMERA_FIELDS),
    ):
        if feature not in features:
            continue
        layout = runtime.get(name, {})
        if set(layout) != set(fields):
            raise ValueError(f"{feature} requires an explicit {name} layout")
        values = {key: number(value) for key, value in layout.items()}
        if any(type(n) is not int or n < 0 or n > 0xFFFFFFFF for n in values.values()):
            raise ValueError(f"Invalid {name} layout")
        if feature == "InputFlow":
            for key in ("screen_id", "context_camera", "handle_inner", "inner_data", "camera_mode"):
                if values[key] > 65532 or values[key] % 4:
                    raise ValueError("Invalid input flow offset")
            for key in ("title_screen", "pause_screen"):
                if values[key] >= 32 or not values["menu_screens"] & (1 << values[key]):
                    raise ValueError("Title and pause must be interactive screens")
            if values["menu_screens"] & ~values["cursor_screens"] or not values["gameplay_modes"]:
                raise ValueError("Invalid input flow masks")
        elif feature == "DirectMouseLook":
            for key, value in values.items():
                if key == "dragon_class":
                    continue
                alignment = (
                    2 if key in ("player_target", "handle_resource", "vtable_turn_this", "vtable_pitch_this") else 4
                )
                if value > 65535 or value % alignment:
                    raise ValueError("Invalid camera offset")
            for key in ("context_camera", "handle_inner", "inner_data", "camera_mode"):
                if values[key] != number(runtime["input_flow"][key]):
                    raise ValueError("Camera and input flow layouts disagree")
            if not values["resource_stride"]:
                raise ValueError("Invalid resource stride")
        elif (
            values["widget_id"] > 65534
            or values["widget_id"] % 2
            or not 0 < values["adjust_screen_widget"] <= 65535
            or not 0 < values["adjust_screen_text"] <= 65535
        ):
            raise ValueError("Invalid settings layout")
    addresses = runtime["addresses"]
    pairs = [(number(a), number(b)) for a, b in addresses.items()]
    if len({a for a, _ in pairs}) != len(pairs):
        raise ValueError("Duplicate canonical address")
    if any(type(n) is not int or n < 0x100000 or n >= 0x2000000 or n % 4 for pair in pairs for n in pair):
        raise ValueError("Addresses must be aligned nonzero EE addresses")
    fixture = runtime.get("save_fixture")
    if fixture is not None:
        if set(fixture) != {"player_class", "camera_classes"} or not isinstance(fixture["camera_classes"], list):
            raise ValueError("Invalid save fixture metadata")
        classes = [fixture["player_class"], *fixture["camera_classes"]]
        if any(type(n) is not int or not 0 < n <= 0xFFFFFFFF for n in classes) or len(set(classes)) != len(classes):
            raise ValueError("Invalid serialized actor classes")
    observer = runtime.get("combat_observer")
    if observer is not None:
        if set(observer) != set(OBSERVER_FIELDS):
            raise ValueError("Incomplete combat observer layout")
        for key, value in observer.items():
            value = number(value)
            if type(value) is not int or value < 0 or value > 0xFFFFFFFF:
                raise ValueError("Invalid combat observer value")
            if key != "rynn_class" and not key.endswith("_mask") and (value > 65532 or value % 4):
                raise ValueError("Invalid combat observer offset")
    required = set(BASELINE)
    if observer is not None:
        required.update((0x4FBAC8, 0x4FE86C, 0x4FBB34))
    for feature in features:
        required.update(REQUIREMENTS.get(feature, ()))
    if not identity_addresses and (missing := required - {a for a, _ in pairs}):
        raise ValueError(f"Missing verified addresses: {', '.join(hex(a) for a in sorted(missing))}")
    movie = runtime.get("movie", {})
    if set(features) & {"MoviePacing", "MovieGuard"}:
        if set(movie) != set(MOVIE_FIELDS):
            raise ValueError("Movie features require an explicit movie layout and frame rate")
        values = {key: number(value) for key, value in movie.items()}
        if any(type(n) is not int or n <= 0 or n > 1000000 for n in values.values()):
            raise ValueError("Invalid movie layout or frame rate")
        if any(values[key] % 4 for key in ("fps_index", "frame_count", "screen_entries")):
            raise ValueError("Movie offsets must be word aligned")
        if (
            values["screen_count"] > 256
            or max(values[k] for k in ("fps_index", "frame_count", "screen_entries")) > 65536
        ):
            raise ValueError("Movie layout exceeds supported bounds")
        if not 1 <= values["fps_numerator"] / values["fps_denominator"] <= 120:
            raise ValueError("Movie rate must be between 1 and 120 fps")
    if "StereoSync" in features:
        audio = runtime.get("audio", {})
        if set(audio) != set(AUDIO_FIELDS):
            raise ValueError("StereoSync requires an explicit audio layout")
        if any(type(n := number(value)) is not int or n <= 0 or n > 65536 or n % 4 for value in audio.values()):
            raise ValueError("Invalid audio layout")
        mapping = dict(pairs)
        for canonical, field in ((0x4DB4F0, "manager_stereo"), (0x4FB6D0, "option_stereo")):
            base = canonical if identity_addresses else mapping[canonical]
            if base + number(audio[field]) + 4 > 0x2000000:
                raise ValueError("Audio field exceeds EE memory")


def render(region, identity, runtime):
    strings = [region, identity["boot"], identity["serial"], identity["save_id"], identity["label"]]
    fields = ", ".join(json.dumps(s, ensure_ascii=True) for s in strings)
    features = " | ".join(f"featureBit(Feature::{f})" for f in runtime["features"]) or "0u"
    movie = ", ".join(str(number(runtime.get("movie", {}).get(key, 0))) + "u" for key in MOVIE_FIELDS)
    audio = ", ".join(str(number(runtime.get("audio", {}).get(key, 0))) + "u" for key in AUDIO_FIELDS)
    flow = ", ".join(str(number(runtime.get("input_flow", {}).get(key, 0))) + "u" for key in FLOW_FIELDS)
    settings = ", ".join(str(number(runtime.get("settings", {}).get(key, 0))) + "u" for key in SETTINGS_FIELDS)
    camera = ", ".join(str(number(runtime.get("camera", {}).get(key, 0))) + "u" for key in CAMERA_FIELDS)
    pairs = sorted((number(a), number(b)) for a, b in runtime["addresses"].items())
    rows = "\n".join(f"    {{0x{a:08x}u, 0x{b:08x}u}}," for a, b in pairs)
    return (
        "// Generated from public disc metadata by generate_runtime_profile.py. Do not edit.\n#pragma once\n"
        f'static_assert(sizeof(drakan_input::CameraLayout) == {len(CAMERA_FIELDS)} * sizeof(uint32_t), "Update disc camera fields");\n'
        "namespace drakan_region {\n"
        f"inline constexpr Profile kProfile{{{fields}, {str(identity['vmode'] == 'NTSC').lower()},\n"
        f"    {str(runtime['identity_addresses']).lower()}, {features}, {{{movie}}}, {{{audio}}},\n"
        f"    {{{flow}}}, {{{settings}}}, {runtime['language_count']}u, {{{camera}}}}};\n"
        f"inline constexpr std::array<AddressPair, {len(pairs)}> kAddressPairs = {{{{\n{rows}\n}}}};\n"
        "} // namespace drakan_region\n"
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--registry", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--region", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        identity, runtime = load_profile(args.registry, args.root, args.region)
        content = render(args.region, identity, runtime)
    except (ValueError, KeyError, TypeError, OSError) as error:
        parser.exit(1, f"Disc profile: {error}\n")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text(encoding="utf-8") != content:
        args.output.write_text(content, encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
