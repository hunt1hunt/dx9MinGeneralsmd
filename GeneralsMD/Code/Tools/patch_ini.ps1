# ============================================================
#  patch_ini.ps1 - Patch AmericaVehicle.ini to use EUTEVHUMVEE
#  Called by replace_humvee.bat. Kept ASCII-only.
#  Args: $args[0] = full path to AmericaVehicle.ini
# ============================================================

$p = $args[0]
if (-not $p) {
    Write-Output '[ERROR] no ini path given'
    exit 1
}
if (-not (Test-Path -LiteralPath $p)) {
    Write-Output "[ERROR] ini not found: $p"
    exit 1
}

# Read as raw text (preserves structure).
$t = [IO.File]::ReadAllText($p)

# 1) model name: APATAVGATTTANK_SKN -> EUTEVHUMVEE_SKN
$t = $t.Replace('APATAVGATTTANK_SKN', 'EUTEVHUMVEE_SKN')

# 2) bone name: bone_barrel01 -> bone_barrel
$t = $t.Replace('bone_barrel01', 'bone_barrel')

# 3) remove ONLY the humvee's Animation / IdleAnimation lines
#    (those referencing the old APATAVGATTTANK anim files). Other
#    units' Animation lines are left untouched.
$l = $t -split "\r?\n"
$o = @($l | Where-Object { $_ -notmatch '^\s*(Animation|IdleAnimation)\s*=\s*APATAVGATTTANK' })

# Write back with CRLF + UTF8 (no BOM). UTF8Encoding($false) = no BOM.
[IO.File]::WriteAllText($p, ($o -join "`r`n"), (New-Object System.Text.UTF8Encoding $false))
Write-Output '   [OK] ini patched'
