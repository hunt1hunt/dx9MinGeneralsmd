# ============================================================
#  patch_pathfinder_ini.ps1 - Replace the US Pathfinder's
#  W3DModelDraw block with a W3XModelDraw for the RA3 Allied
#  Sniper (EUTEISINPER_SKN / GU_SNPRSH skeleton + animations).
#  Called by replace_pathfinder.bat. Kept ASCII-only.
#
#  Args:
#    $args[0] = full path to AmericaInfantry.ini
#
#  Behavior:
#    - Finds the <Object AmericaInfantryPathfinder> section.
#    - Replaces its first "  Draw = W3DModelDraw" block with a
#      W3XModelDraw block using the RA3 sniper model and a
#      GU_SNPRSH animation per condition state.
#    - Comments out every OTHER Draw block in the object (none
#      for the Pathfinder, but kept symmetric with the building
#      patcher).
#    - Backs up nothing here: the .bat makes the .bak before calling.
#    - Idempotent: a previous run's W3XModelDraw block is left alone.
# ============================================================

$ini = $args[0]

if (-not $ini) {
    Write-Output '[ERROR] usage: patch_pathfinder_ini.ps1 <AmericaInfantry.ini>'
    exit 1
}
if (-not (Test-Path -LiteralPath $ini)) {
    Write-Output "[ERROR] ini not found: $ini"
    exit 1
}

$lines = [System.IO.File]::ReadAllLines($ini)
if ($lines.Length -eq 0) {
    Write-Output "[ERROR] ini is empty: $ini"
    exit 1
}

# ---- 1) locate the Pathfinder <Object> start ----
$obj = 'AmericaInfantryPathfinder'
$objPat = '^\s*Object\s+' + [regex]::Escape($obj) + '\s*$'
$objStart = -1
for ($i = 0; $i -lt $lines.Length; $i++) {
    if ($lines[$i] -match $objPat) { $objStart = $i; break }
}
if ($objStart -lt 0) {
    Write-Output "[ERROR] object '$obj' not found in $ini"
    exit 1
}

# ---- 2) collect all "  Draw = ..." blocks in the object ----
# Object closing End is at column 0; a Draw block is 2-space key whose
# closing "  End" is also 2-space. State-level Ends are 4-space.
$blocks = New-Object System.Collections.Generic.List[object]
$i = $objStart + 1
while ($i -lt $lines.Length) {
    $ln = $lines[$i]
    if ($ln -match '^End\s*$') { break }              # object closing End
    if ($ln -match '^  Draw\s*=') {
        $j = $i + 1
        while ($j -lt $lines.Length -and $lines[$j] -notmatch '^  End\s*$') { $j++ }
        if ($j -ge $lines.Length) {
            Write-Output "[WARNING] unterminated Draw block at line $($i + 1); skipping"
            $i++
            continue
        }
        $blocks.Add(@($i, $j))
        $i = $j + 1
    } else {
        $i++
    }
}

if ($blocks.Count -eq 0) {
    Write-Output "[ERROR] no '  Draw = ' block found for '$obj'"
    exit 1
}
Write-Output "   [OK] found $($blocks.Count) Draw block(s) in '$obj'"

# ---- 3) preserve the first block's ModuleTag name ----
$tag = 'ModuleTag_01'
if ($lines[$blocks[0][0]] -match 'ModuleTag_\w+') { $tag = $matches[0] }

# ---- 4) build the replacement W3XModelDraw block ----
# GU_SNPRSH animation mapping for the Pathfinder condition states:
#   NONE (stand)            -> AIDA (calm stand idle, LOOP)
#   MOVING                  -> SMVA (walk cycle, LOOP)
#   FIRING_A                -> ATKA (sniper shot recoil, ONCE)
#   BETWEEN_FIRING_SHOTS_A  -> AIDA (hold still between shots, LOOP)
#   DYING                   -> DIEA (fall, ONCE)
#   DYING EXPLODED_FLAILING -> DIEA (loop flail)
#   DYING EXPLODED_BOUNCING -> DIEB (bounce, ONCE)
#   FREEFALL / PARACHUTING  -> FLYA (limbs spread, LOOP)
$w3xBlock = New-Object System.Collections.Generic.List[string]
$w3xBlock.Add("  Draw = W3XModelDraw $tag")
$w3xBlock.Add("    DefaultModelName = EUTEISINPER_SKN")
$w3xBlock.Add("")
$w3xBlock.Add("    ConditionState = NONE")
$w3xBlock.Add("      Model = EUTEISINPER_SKN")
$w3xBlock.Add("      Animation = GU_SNPRSH_AIDA")
$w3xBlock.Add("      AnimationMode = LOOP")
$w3xBlock.Add("      WeaponFireFXBone = PRIMARY b_weapona_fx")
$w3xBlock.Add("      WeaponMuzzleFlash = PRIMARY b_weapona_fx")
$w3xBlock.Add("    End")
$w3xBlock.Add("")
$w3xBlock.Add("    ConditionState = MOVING")
$w3xBlock.Add("      Model = EUTEISINPER_SKN")
$w3xBlock.Add("      Animation = GU_SNPRSH_SMVA")
$w3xBlock.Add("      AnimationMode = LOOP")
$w3xBlock.Add("      ParticleSysBone = None InfantryDustTrails")
$w3xBlock.Add("    End")
$w3xBlock.Add("")
$w3xBlock.Add("    ConditionState = FIRING_A")
$w3xBlock.Add("      Model = EUTEISINPER_SKN")
$w3xBlock.Add("      Animation = GU_SNPRSH_ATKA")
$w3xBlock.Add("      AnimationMode = ONCE")
$w3xBlock.Add("    End")
$w3xBlock.Add("")
$w3xBlock.Add("    ConditionState = BETWEEN_FIRING_SHOTS_A")
$w3xBlock.Add("      Model = EUTEISINPER_SKN")
$w3xBlock.Add("      Animation = GU_SNPRSH_AIDA")
$w3xBlock.Add("      AnimationMode = LOOP")
$w3xBlock.Add("    End")
$w3xBlock.Add("")
$w3xBlock.Add("    ConditionState = DYING")
$w3xBlock.Add("      Model = EUTEISINPER_SKN")
$w3xBlock.Add("      Animation = GU_SNPRSH_DIEA")
$w3xBlock.Add("      AnimationMode = ONCE")
$w3xBlock.Add("    End")
$w3xBlock.Add("")
$w3xBlock.Add("    ConditionState = DYING EXPLODED_FLAILING")
$w3xBlock.Add("      Model = EUTEISINPER_SKN")
$w3xBlock.Add("      Animation = GU_SNPRSH_DIEA")
$w3xBlock.Add("      AnimationMode = LOOP")
$w3xBlock.Add("    End")
$w3xBlock.Add("")
$w3xBlock.Add("    ConditionState = DYING EXPLODED_BOUNCING")
$w3xBlock.Add("      Model = EUTEISINPER_SKN")
$w3xBlock.Add("      Animation = GU_SNPRSH_DIEB")
$w3xBlock.Add("      AnimationMode = ONCE")
$w3xBlock.Add("    End")
$w3xBlock.Add("")
$w3xBlock.Add("    ConditionState = FREEFALL")
$w3xBlock.Add("      Model = EUTEISINPER_SKN")
$w3xBlock.Add("      Animation = GU_SNPRSH_FLYA")
$w3xBlock.Add("      AnimationMode = LOOP")
$w3xBlock.Add("    End")
$w3xBlock.Add("")
$w3xBlock.Add("    ConditionState = PARACHUTING")
$w3xBlock.Add("      Model = EUTEISINPER_SKN")
$w3xBlock.Add("      Animation = GU_SNPRSH_FLYA")
$w3xBlock.Add("      AnimationMode = LOOP")
$w3xBlock.Add("    End")
$w3xBlock.Add("  End")

# ---- 5) splice: replace first block with W3X, comment out the rest ----
$out = New-Object System.Collections.Generic.List[string]

for ($i = 0; $i -lt $blocks[0][0]; $i++) { $out.Add($lines[$i]) }
foreach ($b in $w3xBlock) { $out.Add($b) }

$cursor = $blocks[0][1] + 1
for ($k = 1; $k -lt $blocks.Count; $k++) {
    $start = $blocks[$k][0]
    $end   = $blocks[$k][1]
    for ($i = $cursor; $i -lt $start; $i++) { $out.Add($lines[$i]) }
    $head = $lines[$start].Trim()
    $out.Add("  ; DISABLED by patch_pathfinder_ini (W3D draw replaced by RA3 sniper): $head")
    $out.Add("  ;   (block lines $($start + 1)-$($end + 1) removed; see .bak for original)")
    $cursor = $end + 1
}
for ($i = $cursor; $i -lt $lines.Length; $i++) { $out.Add($lines[$i]) }

# ---- 6) write back CRLF + UTF-8 no BOM ----
$enc = New-Object System.Text.UTF8Encoding $false
[System.IO.File]::WriteAllLines($ini, $out.ToArray(), $enc)
Write-Output "   [OK] '$obj' -> W3XModelDraw (EUTEISINPER_SKN), $($blocks.Count - 1) other Draw(s) disabled"
