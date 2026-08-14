# ============================================================
#  patch_building_ini.ps1 - Replace a building's W3DModelDraw
#  blocks with a single W3XModelDraw for an RA3 (.w3x) model.
#  Called by replace_building.bat. Kept ASCII-only.
#
#  Args:
#    $args[0] = full path to the building ini (e.g. FactionBuilding.ini)
#    $args[1] = target Object name      (e.g. AmericaWarFactory)
#    $args[2] = new RA3 model name      (e.g. EUWARFACTORY_SKN)
#
#  Behavior:
#    - Finds the <Object> section.
#    - Replaces its FIRST "  Draw = W3DModelDraw" block with a
#      minimal W3XModelDraw block (DefaultModelName + NONE and
#      REALLYDAMAGED RUBBLE states, all pointing at the new model).
#    - Comments out every OTHER W3DModelDraw block in the object
#      (door / animated sub-parts) so the old W3D geometry cannot
#      overlap the RA3 model.
#    - Backs up nothing here: the .bat makes the .bak before calling.
#    - Idempotent: a previous run's W3XModelDraw block is left alone
#      (only W3DModelDraw blocks are matched).
# ============================================================

$ini   = $args[0]
$obj   = $args[1]
$model = $args[2]

if (-not $ini -or -not $obj -or -not $model) {
    Write-Output '[ERROR] usage: patch_building_ini.ps1 <ini> <ObjectName> <NewModel>'
    exit 1
}
if (-not (Test-Path -LiteralPath $ini)) {
    Write-Output "[ERROR] ini not found: $ini"
    exit 1
}

# Read as UTF-8 no-BOM (the target ini is pure ASCII; safe).
$lines = [System.IO.File]::ReadAllLines($ini)
if ($lines.Length -eq 0) {
    Write-Output "[ERROR] ini is empty: $ini"
    exit 1
}

# ---- 1) locate the <Object> start ----
$objPat = '^\s*Object\s+' + [regex]::Escape($obj) + '\s*$'
$objStart = -1
for ($i = 0; $i -lt $lines.Length; $i++) {
    if ($lines[$i] -match $objPat) { $objStart = $i; break }
}
if ($objStart -lt 0) {
    Write-Output "[ERROR] object '$obj' not found in $ini"
    exit 1
}

# ---- 2) collect all "  Draw = W3DModelDraw" blocks in the object ----
# The object's own closing End is at column 0; every Draw block is a
# 2-space key whose closing "  End" is also 2-space. State-level Ends
# are 4-space, so they never match. Scan until the 0-indent End.
$blocks = New-Object System.Collections.Generic.List[object]
$i = $objStart + 1
while ($i -lt $lines.Length) {
    $ln = $lines[$i]
    if ($ln -match '^End\s*$') { break }              # object closing End
    if ($ln -match '^  Draw\s*=\s*W3DModelDraw') {
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
    Write-Output "[ERROR] no '  Draw = W3DModelDraw' block found for '$obj'"
    exit 1
}
Write-Output "   [OK] found $($blocks.Count) W3DModelDraw block(s) in '$obj'"

# ---- 3) preserve the first block's ModuleTag name ----
$tag = 'ModuleTag_01'
if ($lines[$blocks[0][0]] -match 'ModuleTag_\w+') { $tag = $matches[0] }

# ---- 4) build the replacement W3XModelDraw block ----
$w3xBlock = New-Object System.Collections.Generic.List[string]
$w3xBlock.Add("  Draw = W3XModelDraw $tag")
$w3xBlock.Add("    DefaultModelName = $model")
$w3xBlock.Add("    ConditionState = NONE")
$w3xBlock.Add("      Model = $model")
$w3xBlock.Add("    End")
$w3xBlock.Add("    ConditionState = REALLYDAMAGED RUBBLE")
$w3xBlock.Add("      Model = $model")
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
    $out.Add("  ; DISABLED by patch_building_ini (W3D sub-draw replaced by RA3 model): $head")
    $out.Add("  ;   (block lines $($start + 1)-$($end + 1) removed; see .bak for original)")
    $cursor = $end + 1
}
for ($i = $cursor; $i -lt $lines.Length; $i++) { $out.Add($lines[$i]) }

# ---- 6) write back CRLF + UTF-8 no BOM ----
$enc = New-Object System.Text.UTF8Encoding $false
[System.IO.File]::WriteAllLines($ini, $out.ToArray(), $enc)
Write-Output "   [OK] '$obj' -> W3XModelDraw ($model), $($blocks.Count - 1) sub-draw(s) disabled"
