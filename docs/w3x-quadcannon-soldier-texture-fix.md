# W3X Quad Cannon Soldier Texture Fix (2026-08-27)

## Symptom

GLA Quad Cannon (`GLAVehicleQuadCannon`, model `GLATGVQUADCANN_SKN`) —
the soldier(s) riding on the vehicle rendered with the **vehicle** texture
(`TgvQuadCann2` camo) instead of the soldier texture. The vehicle itself was correct.

## Root Cause

The model embeds two soldier meshes skinned to the soldier skeleton:

| Mesh | Contents | Bones | Material (before) | Convention |
|------|----------|-------|-------------------|------------|
| BODY01 | vehicle body | 1–3 | `DiffuseTexture=TgvQuadCann2` | PBR ✓ |
| BODY02 | vehicle part | 1 | `DiffuseTexture=TgvQuadCann2` | PBR ✓ |
| BODY03 | **soldier body** | 8–25 (HIPS/SPINE/HEAD/limbs) | `DiffuseTexture=TgvQuadCann2` (**vehicle tex**) | PBR ✗ |
| BODY04 | **soldier head** | 10–11 (SPINE1/HEAD) | `Texture_0=TgiRifleS` (soldier tex) | BASIC ✓ |
| TREAD / WHEEL | tracks/wheels | 33–42 | `TunTrack` | PBR ✓ |

BODY03 was the **first** `SubObject`, and it used the PBR `DiffuseTexture`
convention. In `loadW3XModel`, the model-wide FX shader is chosen once from the
first non-default mesh: BODY03 → `isBasicConvention=false` →
`w3x_soviet.fx` (PBR). The PBR shader has no `Texture_0` parameter, so the
soldier meshes' `TgiRifleS` (declared as `Texture_0`, the BASIC/infantry
convention) could not bind — BODY03 bound the vehicle diffuse directly, and
BODY04 inherited the leaked vehicle texture. Soldier wore vehicle camo.

## Fix

1. **Engine — `GameEngineDevice/Source/W3DDevice/GameClient/Drawable/Draw/W3XModelDraw.cpp`**
   In `createRenderObject`, added a per-sub-mesh shader override: sub-meshes
   whose `.w3x` declares `FXShader ShaderName="infantry.fx"` route to the
   dedicated `Shaders\RA3\w3x_infantry.fx` (same mechanism as the existing
   tread override → `w3x_tread.fx`). The infantry shader renders the soldier's
   single `Texture_0` with the texture-alpha faction-color mask. This is the
   general fix for any *mixed* soldier+vehicle W3X model, where one model-level
   shader cannot serve both conventions.

2. **Data — `ART/W3X/GLATGVQUADCANN_SKN.SKIN_BODY03.w3x`**
   Replaced the PBR material constants (`DiffuseTexture`/`NormalMap`/`SpecMap`
   = `TgvQuadCann2*`) with the BASIC soldier set: single
   `Texture_0` = `TgiRifleS` (identical to BODY04).

3. **Data — `ART/W3X/GLATGVQUADCANN_SKN.w3x`** (container)
   Moved BODY01 (PBR vehicle body) to the **first** `SubObject` so the model-wide
   shader resolves to `w3x_soviet.fx` again — the vehicle keeps its correct PBR
   textures; the soldier meshes are overridden per-sub-mesh to `w3x_infantry.fx`.

## Result

- Soldiers render with `TgiRifleS` (soldier texture) + faction-color alpha.
- Vehicle textures (`TgvQuadCann2`, `TunTrack`) unchanged and correct.
- No PBR/vehicle shader is forced onto the soldier meshes.

## Files Changed

| File | Type | Needs rebuild |
|------|------|---------------|
| `GameEngineDevice/.../Drawable/Draw/W3XModelDraw.cpp` | engine | yes (GameEngineDevice → RTS.exe) |
| `ART/W3X/GLATGVQUADCANN_SKN.SKIN_BODY03.w3x` | game data | no (runtime-loaded) |
| `ART/W3X/GLATGVQUADCANN_SKN.w3x` | game data | no (runtime-loaded) |

## Side Effect

The engine change also routes the Technical truck (`GLATGVTECHNICAL`) soldier
meshes (`infantry.fx`) to `w3x_infantry.fx`, which is the correct behavior for
them as well.
