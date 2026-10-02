# libcapes_reconstructed — C++/JNI rewrite

This version rewrites the reconstructed native layer from Rust to C++ while preserving the original embedded `McInterceptor.dex` byte-for-byte and the recovered capes JSON payloads.

## Build on Termux / Android NDK

```sh
export ANDROID_NDK_HOME=$HOME/android-sdk/ndk/<version>
./build_android.sh
```

Or configure CMake directly.

## Notes

- `McInterceptor.dex` is embedded in the native library and loaded with `InMemoryDexClassLoader`.
- The native loader searches candidate Java class loaders and requires one that can resolve both `okhttp3.Interceptor` and `okhttp3.OkHttpClient` before creating the DEX loader.
- No writable DEX fallback is used; Android 14/API 34 rejects writable file-backed DEX loading in this scenario.
- `JNI_OnLoad` performs installation, while `__attribute__((constructor))` only performs native early logging.


## V20 skin-pack routing

Added the exact PocketCosmos UUID-specific `MultiItemPage_PersonaSkinSelector%7cSkinPack_<UUID>` viewer responses for the nine skin-pack viewer routes present in PocketCosmos `MainResponses.json`. The generic `MultiItemPage_PersonaSkinSelector` route remains untouched, so Classic/Custom layout handling is not replaced globally.

Routes added:
- 60a2047b-fe99-4a1d-9ccf-79d6a9b63adb — Doctor Who V1
- c65b1919-df90-4c9b-b3d5-a59c7d7a3979 — Doctor Who V2
- 62c68fd4-3075-40ed-8b7b-15dcc7b667ff — Festive
- 2f60a7a0-8010-49b1-8b57-851ac41f34cf — Skin Pack 4
- 80c85890-dad7-4d3a-b2ef-60071327b7cf — Skin Pack 5
- 4c61b1f5-2bbc-492f-8a2a-1f3b813c4113 — Skin Pack 6
- 1fc32dd0-b399-4255-a4c7-e6e262a40ab6 — Mario Mash-Up
- 358a8710-bcf4-4cd7-ae86-a63ffec500c8 — Summer of Arcade
- 5b13cb52-e2bf-438c-97f2-1105c54929d9 — Halloween Charity

These are exact embedded JSON responses copied from the PocketCosmos assets; no invented viewer schema was added.


V21: added every SkinPacks mapping from PocketCosmos MainResponses.json (149 routes, including creator pack productId/ItemDetail/packId pages). This fixes catalog items falling through to the real marketplace.


V23: normalized every embedded SkinPacks skinpack catalog/detail payload to the free/owned state used by the working Simpsons-style pages.
- `price.listPrice` is normalized to `-1` (no Minecoin price).
- `purchasable` is normalized to `false` where present.
- `ownership` is normalized to `Purchased` on item-detail catalog objects so the item-detail UI exposes the owned/Get path.
- Existing `purchaseInfoComp.binaries` and skinbinary URLs are preserved.
- Search/item-detail/binary payloads under `assets/cosmos/SkinPacks` were normalized when they identify a `skinpack`; non-skinpack marketplace payloads were not targeted.

V24 ENTITLEMENT FIX
-------------------
V23 corrected the visible marketplace metadata (price/ownership) but that is not the
actual Bedrock ownership gate. PocketCosmos also intercepts the real Minecraft
entitlement inventory request and forwards its request body to:
https://bedrock-cosmos.app/api/v1.0/player/inventory?includeReceipt=true
V24 ports that missing behavior into the native interceptor. The returned entitlement
inventory is supplied back to Minecraft, so skin packs such as Skin Pack 1/2/3/4 and
creator packs can be treated as owned by the same mechanism PocketCosmos uses.
