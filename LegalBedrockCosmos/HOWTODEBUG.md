HOWTODEBUG.md

Cosmos Capes & Skin Packs for LeviLaunchroid

This file explains how to debug the mod, what the included files do, and how to add custom capes.

Project Files

src/libcapes.cpp

This is the main native mod source.

It contains the native HTTP interception logic, request routing, PlayFab handling, entitlement handling, Dressing Room handling, and the logic that loads the embedded Cosmos responses.

assets/

This contains the Cosmos response data used by the mod.

assets/cosmos/

This is the main Cosmos data directory.

assets/cosmos/LauncherJsons/

Contains the Marketplace, PlayFab, entitlement, and other launcher response data.

assets/cosmos/MainPages/

Contains Marketplace and Dressing Room page responses.

assets/cosmos/SkinPacks/

Contains the skin pack responses used for classic and custom skin packs.

assets/cosmos/Persona/

Contains Persona-related response data.

src/skinpack_pages.hpp

This is the generated C++ header containing embedded skin pack response data.

If you add or modify skin pack JSON files, this file needs to be regenerated before compiling.

CMakeLists.txt

The CMake build configuration for the native library.

capes/manifest.json

The LeviLaunchroid native mod manifest.

The native library name and mod information are defined here.

libcapes.so

The compiled ARM64 native library used by LeviLaunchroid.

Do not manually combine multiple libcapes.so files. The release should contain one native library.

Debugging

First check that the mod is actually being loaded.

If the native library does not load at all, check the LeviLaunchroid native mod log before debugging Marketplace responses.

If the library loads but a feature does not work, determine which Minecraft request is responsible for that feature.

Custom Capes

The cape catalogue is provided through the Dressing Room cape response.

The main cape catalogue is:

assets/cosmos/MainPages/Capes.json

Each cape is represented as an item in the JSON catalogue.

When adding a custom cape, you need the cape's catalogue information and its corresponding asset or response information.

The important cape information includes the cape ID, display information, piece type, texture information, and any required Cosmos or PlayFab information.

The existing catalogue uses:

pieceType = persona_capes

Do not randomly change the structure of an existing cape entry. Copy the structure of an existing working cape and change only the values required for the new cape.

Adding A Custom Cape

Open:

assets/cosmos/MainPages/Capes.json

Find the items array.

Add the new cape using the same JSON structure as an existing working cape.

Give the cape its own unique ID.

Set its pieceType to:

persona_capes

Add the display name and other required display information.

If the cape uses a Cosmos PlayFab item, add the corresponding PlayFab mapping in:

assets/cosmos/LauncherJsons/PlayfabGetPublishItemResponses.json

The PlayFab item must point to the correct cape asset/content information.

If the cape has a corresponding binary or asset response, make sure that response is also included in the appropriate Cosmos response directory.

After changing the JSON, rebuild the native response data so the modified catalogue is embedded into the native library.

Important Custom Cape Notes

Adding an item to Capes.json alone does not guarantee that the cape will work.

Minecraft may request additional information for the item.

If the cape appears in the Dressing Room but does not equip, debug the PlayFab/item response for that cape.

If the cape does not appear at all, debug the Dressing Room cape response first.

If selecting Equip stays on the loading spinner, debug the item/content and entitlement-related requests rather than changing the UI catalogue again.

PlayFab Debugging

PlayFab requests are handled by the native interceptor.

The important endpoints include:

https://20ca2.playfabapi.com/Catalog/GetPublishedItem

https://20ca2.playfabapi.com/Catalog/Search

GetPublishedItem requests are matched against the requested ItemId.

Search requests are matched against the search body.

When debugging a custom item, first verify that the requested ID is actually present in the local PlayFab response data.

Entitlement Debugging

The entitlement request is separate from the Marketplace page.

Minecraft requests:

https://entitlements.mktpl.minecraft-services.net/api/v1.0/player/inventory?includeReceipt=true

The mod forwards the request to the Bedrock Cosmos inventory service:

https://bedrock-cosmos.app/api/v1.0/player/inventory?includeReceipt=true

If a skin pack appears free but Minecraft still refuses to equip it, check entitlement handling first.

Changing listPrice or ownership in an Item Detail JSON does not by itself guarantee that Minecraft considers the item owned.

Dressing Room Debugging

The main Persona Profile request is:

https://store.mktpl.minecraft-services.net/api/v2.0/layout/pages/DressingRoom_PersonaProfile

The mod modifies the existing response rather than replacing the entire page.

The Persona append data is:

assets/cosmos/MainPages/DressingRoom_PersonaProfile_Persona_append.json

The Skin append data is:

assets/cosmos/MainPages/DressingRoom_PersonaProfile_Skins_append.json

Be careful with files ending in:

_append.json

These are not necessarily complete Marketplace pages.

Treating an append response as a complete page can break Minecraft's Dressing Room layout.

Skin Pack Debugging

Skin pack routing is generally:

Marketplace listing
→ productId
→ Item Detail
→ Skin Selector
→ Viewer

The relevant responses are stored under:

assets/cosmos/SkinPacks/

and the routing information comes from the Marketplace response data.

If a pack opens but the skin selector does not load, check the viewer route.

If the Item Detail page does not display the Get button, check the purchaseInfoComp and binaries data.

If the Get button appears but the pack cannot be equipped, check entitlement handling.

Logs

When debugging, record:

The URL Minecraft requested

The request body when the request is body-dependent

Whether the native interceptor matched the request

Whether a local response was returned

The HTTP status returned

The size of the returned response

Whether Minecraft displayed the expected page

Whether the item could actually be equipped

Do not assume that a successful Marketplace page means the entire feature is working.

A feature can successfully load its catalogue while failing later during item resolution, binary loading, or entitlement validation.

Build Debugging

If CMake fails, read the first actual compiler error rather than the final ninja failure.

For example:

error: use of undeclared identifier

usually means a function or symbol is being used before its declaration.

If a header contains generated response data, regenerate the header instead of manually editing thousands of embedded bytes.

If the native library compiles but Minecraft crashes, inspect the crash/tombstone and determine whether the crash is inside the mod or Minecraft.

Do not assume that every crash after loading the mod was caused by the mod.

Testing Changes

Test one feature at a time.

For a custom cape:

1. Confirm the cape appears.
2. Open the cape.
3. Select Equip.
4. Confirm the request is handled.
5. Confirm the cape actually equips.
6. Restart Minecraft and verify that the cape still behaves correctly.

For a skin pack:

1. Open the Marketplace listing.
2. Open Item Detail.
3. Confirm the Get button.
4. Open the skin selector.
5. Select a skin.
6. Equip it.
7. Restart Minecraft.
8. Test the pack again.

Do not consider a feature fixed just because its Marketplace page loads.

Generating The Native Library

The source files are compiled into the native library using the project's CMake configuration.

The final release library is:

libcapes.so

The library must be built for the Android ARM64 architecture used by the target LeviLaunchroid environment.

After compiling, place the resulting library in the location expected by the LeviLaunchroid native mod manifest.

Custom Cape Checklist

Before adding a cape:

Make sure you have the cape's unique ID.

Make sure you have the cape texture/content information.

Make sure the cape is represented correctly in Capes.json.

Make sure any required PlayFab response exists.

Make sure any required content/binary response exists.

Make sure the route is registered.

Regenerate any generated C++ response headers.

Compile the native library.

Install the new .levipack.

Test the cape from the actual Minecraft Dressing Room.

Do not rely only on the JSON looking correct. Minecraft must successfully resolve and equip the item.

Reference Implementation

The original Pocket Cosmos implementation is the primary reference for this project.

Pocket Cosmos source:

https://github.com/Bedrock-Cosmos/PocketCosmosLevi

Bedrock Cosmos:

https://github.com/Bedrock-Cosmos

When implementing a new feature, inspect the corresponding Pocket Cosmos implementation first instead of inventing a different request flow.

The native implementation is intended to reproduce the behavior of the original Pocket Cosmos system while integrating it into LeviLaunchroid's native mod architecture.