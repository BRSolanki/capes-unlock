Cosmos Capes & Skin Packs for LeviLaunchroid

A native LeviLaunchroid mod that recreates parts of the Bedrock Cosmos / Pocket Cosmos functionality for Minecraft Bedrock Edition.

This project adds and restores custom capes, classic skin packs, custom skin packs, and Marketplace/Dressing Room functionality using Bedrock Cosmos response data and Pocket Cosmos as the reference implementation.

Features

Custom Bedrock Cosmos capes
Restored classic capes
Custom skin packs
Restored classic skin packs
Marketplace and skin pack routing
Free access to supported skin packs
Dressing Room integration
Custom cape catalogue
PlayFab catalogue responses
Marketplace entitlement handling
Creator skin pack pages
Skin pack Get buttons
Dressing Room Persona Profile integration

How It Works

The mod works at the native HTTP interception layer.

Minecraft sends requests for Marketplace, PlayFab, Dressing Room, and entitlement data. The mod intercepts supported requests and supplies the appropriate Cosmos responses.

Requests that are not handled by the mod continue through Minecraft's normal network system.

Custom Capes

The cape catalogue is based on the Bedrock Cosmos cape catalogue.

When Minecraft requests the supported cape catalogue, the mod provides the local Cosmos response so the capes can appear inside the normal Bedrock Dressing Room.

Skin Packs

Skin packs use Minecraft's normal Marketplace and Dressing Room navigation.

Skin Pack
→ Marketplace Page
→ Item Detail
→ Skin Selector
→ Viewer / Skin Pack Data

The mod provides the required responses for supported classic and custom skin packs.

Get Buttons

Some Marketplace item responses do not contain the binary information Minecraft expects.

For supported packs, the mod supplies the corresponding binary information so Minecraft can correctly recognize the skin pack and display its Get functionality.

Skin Pack Entitlements

Changing the price displayed by a Marketplace page is not enough to make Minecraft allow a skin pack to be equipped.

Minecraft also checks its Marketplace entitlement inventory.

The mod handles the entitlement inventory request separately.

The original inventory request is forwarded to the Bedrock Cosmos inventory service, and the resulting inventory response is returned to Minecraft.

This allows supported skin packs to be recognized as owned by the game instead of only appearing free on the Marketplace page.

Dressing Room

The mod handles the Dressing Room Persona Profile response.

Instead of replacing the entire response, it preserves the existing Minecraft response and adds the required Cosmos Persona and Skin entries.

This helps preserve Minecraft's normal Dressing Room layout and navigation.

Classic Skins

Classic skin packs use Minecraft's existing classic skin navigation.

The mod provides the required skin pack and viewer responses while keeping the normal classic skin selector layout intact.

Append-based responses are handled differently from complete page responses because replacing an append response with an entire page can break the Dressing Room layout.

Creator Skin Packs

Supported creator packs include packs represented in the Bedrock Cosmos data.

BionicBen
Alleis
SRG64
Mono Casual Knights
YEH
ZeroMISTER_JA
Exports
Other supported Cosmos creator packs

The exact available packs depend on the response and catalogue data included with the current build.

Installation

Make sure LeviLaunchroid is installed and working.

Obtain the .levipack release of this mod.

Import the .levipack using LeviLaunchroid's native mod system.

Launch Minecraft through LeviLaunchroid.

Open the Minecraft Dressing Room or Marketplace.

Supported Cosmos capes and skin packs should appear through the normal Minecraft interfaces.

Important

This is a native LeviLaunchroid mod.

The release contains the native library used by LeviLaunchroid's native mod loader.

Do not manually stack multiple versions of the native library.

Use the native library included with the release.

Troubleshooting

Capes do not appear

Make sure the mod is loaded by LeviLaunchroid and check the native mod loading output or log.

A skin pack still shows as unavailable

A Marketplace page being displayed does not necessarily mean Minecraft has accepted the pack as owned. The entitlement inventory request must also be handled correctly.

Marketplace says it cannot access the Marketplace

The requested route may not be included in the local Cosmos response routing. Creator and classic skin packs require the appropriate Marketplace, Item Detail, and viewer or skin selector responses.

A Get button is missing

The Item Detail response may be missing its required binary information.

Classic Skins layout becomes broken

Append-based skin selector responses should not be replaced with complete page responses.

Credits

Bedrock Cosmos / Pocket Cosmos

Special thanks to the Bedrock Cosmos team for creating Bedrock Cosmos and Pocket Cosmos.

BionicBen — Bedrock Cosmos development and releases
Clyraz — Bedrock Cosmos Launcher contributions
Foxyz the bear — Windows Launcher contributions
zk_glxt837 — Russian translation
centerxx — German translation
staccato_music — Ukrainian translation
itzmehector_55171 — Bengali translation
new_vietnam_9000 — Vietnamese translation

Source Code

Bedrock Cosmos
https://github.com/Bedrock-Cosmos

Pocket Cosmos
https://github.com/Bedrock-Cosmos/PocketCosmosLevi

Bedrock Cosmos Launcher
https://github.com/Bedrock-Cosmos/Launcher

Bedrock Cosmos Capes
https://github.com/Bedrock-Cosmos/Capes

LeviLaunchroid
https://github.com/LiteLDev/LeviLaunchroid

License

See LICENSE for the license covering this project.

Third-party code, assets, response data, and other components remain subject to their respective original licenses and attribution requirements.

Disclaimer

This project is not affiliated with, endorsed by, or sponsored by Mojang Studios or Microsoft.

Minecraft is a trademark of Microsoft Corporation.