// w3x_nano.fx - Empire nano-buildup variant (from FXFXH buildingsjapanbuildup.fx).
// Enables the IS_NANO_BUILDUP path already present in the ARPBR core
// (hp_nano_build: alpha-cut sweep + faction-colored glow driven by the
// vertex-color build-percentage channel). Route a buildup sub-mesh here via
// W3XShaderVariant naming ("nano") when an Empire-style construction model
// needs the animated nanite reveal instead of the plain opacity fade.
#define FORBID_CLIPPING_CONSTANT
static const bool AlphaTestEnable = 1 ;
#define OPACITY_OVERRIDE_OUTPUT
#define IS_NANO_BUILDUP

#include "Shaders/RA3/PBR5-10-objects-ARPBR.FX"
