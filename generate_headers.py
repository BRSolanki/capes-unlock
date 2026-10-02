#!/usr/bin/env python3
"""
generate_headers.py — Convert PocketCosmos JSON assets into C++ byte-array headers.

This script reads all required JSON files from the PocketCosmos asset tree and
the LegalBedrockCosmos assets, then generates the C++ .hpp header files that
embed those payloads as constexpr byte arrays in the native library.

Key change vs. the legal variant:
  - The cape catalogue (kPage_Capes) uses the FULL PocketCosmos Capes.json
    (all 260 cape entries) instead of the stripped 238-cape legal version.

Usage:
  python generate_headers.py --cosmos-assets <path> --legal-assets <path> --output-dir <path>
"""

import argparse
import os
import sys
import json
import re

def file_to_hex_array(filepath: str) -> tuple[bytes, str]:
    """Read a file and return (raw_bytes, C-hex-literal-string)."""
    with open(filepath, "rb") as f:
        data = f.read()
    lines = []
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        hex_vals = ", ".join(f"0x{b:02x}" for b in chunk)
        lines.append(f"    {hex_vals},")
    return data, "\n".join(lines)

def bytes_to_hex_array(data: bytes) -> str:
    """Convert raw bytes to C hex array literal body."""
    lines = []
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        hex_vals = ", ".join(f"0x{b:02x}" for b in chunk)
        lines.append(f"    {hex_vals},")
    return "\n".join(lines)

def write_single_header(output_path: str, var_name: str, filepath: str):
    """Write a single C++ header with one embedded byte array."""
    data, hex_body = file_to_hex_array(filepath)
    with open(output_path, "w", encoding="utf-8") as f:
        f.write(f"#pragma once\n")
        f.write(f"#include <cstddef>\n")
        f.write(f"#include <cstdint>\n\n")
        f.write(f"inline constexpr std::uint8_t {var_name}[] = {{\n")
        f.write(hex_body)
        f.write(f"\n}};\n")
        f.write(f"inline constexpr std::size_t {var_name}_size = sizeof({var_name});\n")
    print(f"  Generated {output_path} ({len(data)} bytes payload)")

def sanitize_var_name(name: str) -> str:
    """Turn a filename into a valid C identifier."""
    name = re.sub(r'[^a-zA-Z0-9_]', '_', name)
    if name[0].isdigit():
        name = '_' + name
    return name

def main():
    parser = argparse.ArgumentParser(description="Generate C++ headers from PocketCosmos/Legal assets")
    parser.add_argument("--cosmos-assets", required=True, help="Path to PocketCosmos cosmos/ assets dir")
    parser.add_argument("--legal-assets", required=True, help="Path to LegalBedrockCosmos assets/ dir")
    parser.add_argument("--legal-src", required=True, help="Path to LegalBedrockCosmos src/ dir")
    parser.add_argument("--output-dir", required=True, help="Output directory for generated headers")
    args = parser.parse_args()

    cosmos = args.cosmos_assets
    legal = args.legal_assets
    legal_src = args.legal_src
    outdir = args.output_dir
    os.makedirs(outdir, exist_ok=True)

    # ── 1. Copy DEX headers as-is from legal source (byte-for-byte identical) ──
    for dex_hpp in ["mc_interceptor_java_dex.hpp", "mc_interceptor_native_dex.hpp"]:
        src = os.path.join(legal_src, dex_hpp)
        dst = os.path.join(outdir, dex_hpp)
        if os.path.exists(src):
            import shutil
            shutil.copy2(src, dst)
            print(f"  Copied {dex_hpp}")
        else:
            print(f"  WARNING: {src} not found, skipping")

    # ── 2. Generate full capes header from PocketCosmos (ALL capes) ──
    full_capes = os.path.join(cosmos, "MainPages", "Capes.json")
    if not os.path.exists(full_capes):
        print(f"ERROR: Full capes file not found: {full_capes}")
        sys.exit(1)

    # ── 3. Generate all_main_pages.hpp ──
    # This maps the exact PocketCosmos MainPages files, using the full Capes.json
    main_pages_map = {
        "kPage_Capes": os.path.join(cosmos, "MainPages", "Capes.json"),
        "kPage_MultiItemPage_PersonaSkinSelector_append": os.path.join(cosmos, "MainPages", "MultiItemPage_PersonaSkinSelector_append.json"),
        "kPage_MainMarketplaceButton_append": os.path.join(cosmos, "MainPages", "MainMarketplaceButton_append.json"),
        "kPage_MainMarketplacePage": os.path.join(cosmos, "MainPages", "MainMarketplacePage.json"),
        "kPage_LegacyVault": os.path.join(cosmos, "MainPages", "LegacyVault.json"),
        "kPage_MinecraftRestored": os.path.join(cosmos, "MainPages", "MinecraftRestored.json"),
        "kPage_Cosmos_Creator_Hub": os.path.join(cosmos, "MainPages", "Cosmos_Creator_Hub.json"),
        "kPage_MinecraftCosmos_PersonaCreatorPage": os.path.join(cosmos, "MainPages", "MinecraftCosmos_PersonaCreatorPage.json"),
        "kPage_SpinOffCapes_PersonaCreatorPage": os.path.join(cosmos, "MainPages", "SpinOffCapes_PersonaCreatorPage.json"),
        "kPage_BedrockCosmos_PersonaCreatorPage": os.path.join(cosmos, "MainPages", "BedrockCosmos_PersonaCreatorPage.json"),
        "kPage_HiddenDLC": os.path.join(cosmos, "MainPages", "HiddenDLC.json"),
        "kPage_FallbackMarketplaceButton_append": os.path.join(cosmos, "MainPages", "FallbackMarketplaceButton_append.json"),
        "kPage_VerticalLineDivider_append": os.path.join(cosmos, "MainPages", "VerticalLineDivider_append.json"),
        "kPage_CurrentNews_append": os.path.join(cosmos, "MainPages", "CurrentNews_append.json"),
        "kPage_PagedList_Wishlist": os.path.join(cosmos, "MainPages", "PagedList_Wishlist.json"),
    }

    # Add creator pages from MainPages/Creators/ (recurse subdirectories)
    creators_dir = os.path.join(cosmos, "MainPages", "Creators")
    if os.path.isdir(creators_dir):
        for root, dirs, files in os.walk(creators_dir):
            for fname in sorted(files):
                if fname.endswith(".json"):
                    base = fname[:-5]
                    var = "kPage_" + sanitize_var_name(base)
                    main_pages_map[var] = os.path.join(root, fname)

    # Add persona categories (recurse)
    persona_dir = os.path.join(cosmos, "MainPages", "PersonaCategories")
    if os.path.isdir(persona_dir):
        for root, dirs, files in os.walk(persona_dir):
            for fname in sorted(files):
                if fname.endswith(".json"):
                    base = fname[:-5]
                    var = "kPage_" + sanitize_var_name(base)
                    main_pages_map[var] = os.path.join(root, fname)

    # Add DressingRoom special pages from Other/ directory
    # These are referenced directly in libcapes.cpp
    other_dir = os.path.join(cosmos, "Other")
    dressing_room_pages = {
        "kPage_CreeperCrunchCereal": "DressingRoom_CreeperCrunchCereal",
        "kPage_MinecraftDungeons": "DressingRoom_MinecraftDungeons",
        "kPage_MinecraftEarth": "DressingRoom_MinecraftEarth",
        "kPage_RedeemableItems": "DressingRoom_RedeemableItems",
        "kPage_TinyTakeover": "DressingRoom_TinyTakeover",
    }
    # These special pages may exist in Other/ or as separate files
    # Check both Other/ directory items and the legal source as fallback
    if os.path.isdir(other_dir):
        for root, dirs, files in os.walk(other_dir):
            for fname in sorted(files):
                if fname.endswith(".json"):
                    base = fname[:-5]
                    var = "kPage_" + sanitize_var_name(base)
                    # Only add if not already in the map
                    if var not in main_pages_map:
                        main_pages_map[var] = os.path.join(root, fname)

    # Add persona dropdown append if available
    persona_dropdown = os.path.join(cosmos, "MainPages", "PersonaCategories", "PersonaDropdown_append.json")
    if os.path.exists(persona_dropdown):
        main_pages_map["kPage_PersonaDropdown_append"] = persona_dropdown

    # Write all_main_pages.hpp
    all_pages_path = os.path.join(outdir, "all_main_pages.hpp")
    with open(all_pages_path, "w", encoding="utf-8") as f:
        f.write("#pragma once\n")
        f.write("#include <cstddef>\n")
        f.write("#include <cstdint>\n\n")
        for var_name, filepath in main_pages_map.items():
            if not os.path.exists(filepath):
                print(f"  WARNING: {filepath} not found, skipping {var_name}")
                continue
            data, hex_body = file_to_hex_array(filepath)
            f.write(f"static const std::uint8_t {var_name}[] = {{\n")
            f.write(hex_body)
            f.write(f"\n}};\n")
            f.write(f"static constexpr std::size_t {var_name}_size = {len(data)};\n\n")
            print(f"  Embedded {var_name} ({len(data)} bytes) from {os.path.basename(filepath)}")
    print(f"  Generated {all_pages_path}")

    # ── 4. Generate profile persona/skins append headers ──
    persona_append = os.path.join(cosmos, "MainPages", "DressingRoom_PersonaProfile_Persona_append.json")
    skins_append = os.path.join(cosmos, "MainPages", "DressingRoom_PersonaProfile_Skins_append.json")

    persona_hpp = os.path.join(outdir, "profile_persona_append.hpp")
    with open(persona_hpp, "w", encoding="utf-8") as f:
        f.write("#pragma once\n#include <cstddef>\n#include <cstdint>\n\n")
        if os.path.exists(persona_append):
            data, hex_body = file_to_hex_array(persona_append)
            f.write(f"inline constexpr std::uint8_t kProfilePersonaAppend[] = {{\n{hex_body}\n}};\n")
            f.write(f"inline constexpr std::size_t kProfilePersonaAppend_size = sizeof(kProfilePersonaAppend);\n")
            print(f"  Generated profile_persona_append.hpp ({len(data)} bytes)")
        else:
            # Fallback: copy from legal
            src = os.path.join(legal_src, "profile_persona_append.hpp")
            if os.path.exists(src):
                import shutil
                shutil.copy2(src, persona_hpp)
                print(f"  Copied profile_persona_append.hpp from legal")

    skins_hpp = os.path.join(outdir, "profile_skins_append.hpp")
    with open(skins_hpp, "w", encoding="utf-8") as f:
        f.write("#pragma once\n#include <cstddef>\n#include <cstdint>\n\n")
        if os.path.exists(skins_append):
            data, hex_body = file_to_hex_array(skins_append)
            f.write(f"inline constexpr std::uint8_t kProfileSkinsAppend[] = {{\n{hex_body}\n}};\n")
            f.write(f"inline constexpr std::size_t kProfileSkinsAppend_size = sizeof(kProfileSkinsAppend);\n")
            print(f"  Generated profile_skins_append.hpp ({len(data)} bytes)")
        else:
            src = os.path.join(legal_src, "profile_skins_append.hpp")
            if os.path.exists(src):
                import shutil
                shutil.copy2(src, skins_hpp)
                print(f"  Copied profile_skins_append.hpp from legal")

    # ── 5. Generate SkinPack viewer headers ──
    # Scan the cosmos SkinPacks directory for viewer JSON files
    skinpacks_dir = os.path.join(cosmos, "SkinPacks")
    viewer_files = {}
    if os.path.isdir(skinpacks_dir):
        for root, dirs, files in os.walk(skinpacks_dir):
            for fname in files:
                if fname.endswith(".json"):
                    full_path = os.path.join(root, fname)
                    # Read the relative path for routing
                    viewer_files[full_path] = fname

    # Also grab viewer files from legal (these are already proven to work)
    legal_viewers = {}
    for fname in os.listdir(legal_src):
        if fname.startswith("skinpack_viewer_") and fname.endswith(".hpp"):
            legal_viewers[fname] = os.path.join(legal_src, fname)

    # Copy existing viewer .hpp files from legal source
    for fname, src_path in legal_viewers.items():
        dst = os.path.join(outdir, fname)
        import shutil
        shutil.copy2(src_path, dst)
        print(f"  Copied viewer {fname}")

    # ── 6. Generate playfab headers ──
    # Parse PlayfabGetPublishItemResponses.json and PlayfabSearchResponses.json
    playfab_items_json = os.path.join(cosmos, "LauncherJsons", "PlayfabGetPublishItemResponses.json")
    playfab_search_json = os.path.join(cosmos, "LauncherJsons", "PlayfabSearchResponses.json")

    def strip_comments(text: str) -> str:
        return re.sub(r'(?m)^\s*//[^\n]*', '', text)

    def generate_playfab_header(json_path: str, header_name: str, namespace_name: str, map_name: str):
        if not os.path.exists(json_path):
            print(f"  WARNING: {json_path} not found")
            return

        with open(json_path, "r", encoding="utf-8") as f:
            raw = strip_comments(f.read())

        entries = json.loads(raw)
        out_path = os.path.join(outdir, header_name)

        with open(out_path, "w", encoding="utf-8") as f:
            f.write("#pragma once\n")
            f.write("#include <cstddef>\n")
            f.write("#include <cstdint>\n")
            f.write("#include <string>\n")
            f.write("#include <string_view>\n")
            f.write("#include <unordered_map>\n\n")
            f.write(f"namespace {namespace_name} {{\n\n")

            count = 0
            valid_entries = {}
            for entry in entries:
                uuid = entry.get("uuid", "")
                response_path = entry.get("response", "")
                if not uuid or not response_path:
                    continue
                if not response_path.endswith(".json") or response_path.startswith("Processed"):
                    continue

                # Normalize path
                response_path = response_path.replace("\\", "/")
                full_path = os.path.join(cosmos, response_path)

                if not os.path.exists(full_path):
                    continue

                with open(full_path, "r", encoding="utf-8") as rf:
                    content = rf.read()

                valid_entries[uuid] = content
                count += 1

            # Write as a map of string_view -> string_view
            f.write(f"inline const std::unordered_map<std::string, std::string_view> {map_name} = {{\n")
            for uuid, content in valid_entries.items():
                # Escape the content for C++ string literal
                escaped = content.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t")
                # Use raw byte arrays instead for large content
                var_name = f"kPF_{sanitize_var_name(uuid.replace('-','_'))}"
                f.write(f'    {{"{uuid}", std::string_view(reinterpret_cast<const char*>({var_name}), {var_name}_size)}},\n')

            f.write("};\n\n")
            f.write(f"}} // namespace {namespace_name}\n")

        # Now we need to generate the byte arrays for each entry
        # Rewrite the header with byte arrays first, then the map
        with open(out_path, "w", encoding="utf-8") as f:
            f.write("#pragma once\n")
            f.write("#include <cstddef>\n")
            f.write("#include <cstdint>\n")
            f.write("#include <string>\n")
            f.write("#include <string_view>\n")
            f.write("#include <unordered_map>\n\n")
            f.write(f"namespace {namespace_name} {{\n\n")

            for uuid, content in valid_entries.items():
                var_name = f"kPF_{sanitize_var_name(uuid.replace('-','_'))}"
                data = content.encode("utf-8")
                hex_body = bytes_to_hex_array(data)
                f.write(f"static const std::uint8_t {var_name}[] = {{\n{hex_body}\n}};\n")
                f.write(f"static constexpr std::size_t {var_name}_size = {len(data)};\n\n")

            f.write(f"inline const std::unordered_map<std::string, std::string_view> {map_name} = {{\n")
            for uuid, content in valid_entries.items():
                var_name = f"kPF_{sanitize_var_name(uuid.replace('-','_'))}"
                f.write(f'    {{"{uuid}", std::string_view(reinterpret_cast<const char*>({var_name}), {var_name}_size)}},\n')
            f.write("};\n\n")
            f.write(f"}} // namespace {namespace_name}\n")

        print(f"  Generated {header_name} with {count} entries")

    generate_playfab_header(playfab_items_json, "playfab_items.hpp", "cosmos", "kPlayfabItems")
    generate_playfab_header(playfab_search_json, "playfab_search.hpp", "cosmos", "kPlayfabSearch")

    # ── 7. Generate skinpack_pages.hpp from MainResponses.json SkinPacks entries ──
    main_responses_json = os.path.join(cosmos, "LauncherJsons", "MainResponses.json")
    if os.path.exists(main_responses_json):
        with open(main_responses_json, "r", encoding="utf-8") as f:
            raw = strip_comments(f.read())
        responses = json.loads(raw)

        skinpack_entries = []
        base_url = "https://store.mktpl.minecraft-services.net"

        for entry in responses:
            url = entry.get("url", "")
            response_path = entry.get("response", "")

            if not response_path or not response_path.endswith(".json"):
                continue
            if response_path.startswith("Processed") or response_path.startswith("Forwarded"):
                continue
            if "MainPages" in response_path:
                continue  # Already handled in all_main_pages.hpp

            # This catches SkinPacks/, Other/, Persona/ etc.
            norm_path = response_path.replace("\\", "/")
            full_path = os.path.join(cosmos, norm_path)
            if not os.path.exists(full_path):
                continue

            # Extract the URL suffix (strip the base URL)
            if url.startswith(base_url):
                suffix = url[len(base_url):]
            else:
                suffix = url

            skinpack_entries.append((suffix, full_path))

        # Also scan Other/ directory for additional ItemDetail files
        other_dir = os.path.join(cosmos, "Other")
        if os.path.isdir(other_dir):
            for fname in sorted(os.listdir(other_dir)):
                if fname.startswith("ItemDetail_") and fname.endswith(".json"):
                    item_id = fname[len("ItemDetail_"):-5]
                    suffix = f"/api/v2.0/layout/pages/ItemDetail_{item_id}"
                    full_path = os.path.join(other_dir, fname)
                    # Only add if not already present
                    existing_suffixes = {s for s, _ in skinpack_entries}
                    if suffix not in existing_suffixes:
                        skinpack_entries.append((suffix, full_path))

        # Write skinpack_pages.hpp
        skinpack_hpp = os.path.join(outdir, "skinpack_pages.hpp")
        with open(skinpack_hpp, "w", encoding="utf-8") as f:
            f.write("#pragma once\n")
            f.write("#include <cstddef>\n")
            f.write("#include <cstdint>\n")
            f.write("#include <string_view>\n\n")
            f.write("namespace cosmos {\n\n")
            f.write("struct SkinPackPage {\n")
            f.write("    const char* suffix;\n")
            f.write("    const std::uint8_t* data;\n")
            f.write("    std::size_t size;\n")
            f.write("};\n\n")

            # Embed each skinpack response
            for i, (suffix, filepath) in enumerate(skinpack_entries):
                var = f"kSP_{i}"
                data, hex_body = file_to_hex_array(filepath)
                f.write(f"static const std::uint8_t {var}[] = {{\n{hex_body}\n}};\n")
                f.write(f"static constexpr std::size_t {var}_size = {len(data)};\n\n")

            # Write the routing table
            f.write(f"inline constexpr SkinPackPage kSkinPackPages[] = {{\n")
            for i, (suffix, filepath) in enumerate(skinpack_entries):
                var = f"kSP_{i}"
                escaped_suffix = suffix.replace('"', '\\"')
                f.write(f'    {{"{escaped_suffix}", {var}, {var}_size}},\n')
            f.write("};\n")
            f.write(f"inline constexpr std::size_t kSkinPackPagesCount = {len(skinpack_entries)};\n\n")
            f.write("} // namespace cosmos\n")

        print(f"  Generated skinpack_pages.hpp with {len(skinpack_entries)} entries")

    # ── 8. Generate creator_pages.hpp ──
    # This embeds creator persona/marketplace page data
    creator_hpp = os.path.join(outdir, "creator_pages.hpp")
    creators_dir = os.path.join(cosmos, "MainPages", "Creators")
    if os.path.isdir(creators_dir):
        with open(creator_hpp, "w", encoding="utf-8") as f:
            f.write("#pragma once\n#include <cstddef>\n#include <cstdint>\n\n")
            for fname in sorted(os.listdir(creators_dir)):
                if not fname.endswith(".json"):
                    continue
                base = fname[:-5]
                var = "kCreator_" + sanitize_var_name(base)
                filepath = os.path.join(creators_dir, fname)
                data, hex_body = file_to_hex_array(filepath)
                f.write(f"static const std::uint8_t {var}[] = {{\n{hex_body}\n}};\n")
                f.write(f"static constexpr std::size_t {var}_size = {len(data)};\n\n")
        print(f"  Generated creator_pages.hpp")
    else:
        # Copy from legal
        src = os.path.join(legal_src, "creator_pages.hpp")
        if os.path.exists(src):
            import shutil
            shutil.copy2(src, creator_hpp)
            print(f"  Copied creator_pages.hpp from legal")

    # ── 9. Generate capes_v1.hpp and capes_v2.hpp (both use full capes now) ──
    for name in ["capes_v1.hpp", "capes_v2.hpp"]:
        var = name.replace(".hpp", "").replace("capes_", "kCapes").replace("v1", "V1").replace("v2", "V2") + "Bytes"
        write_single_header(os.path.join(outdir, name), var, full_capes)

    print("\n✅ All headers generated successfully!")
    print(f"   Output directory: {outdir}")

if __name__ == "__main__":
    main()

