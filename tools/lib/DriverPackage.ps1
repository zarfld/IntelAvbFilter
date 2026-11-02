# DriverPackage.ps1 - staged driver package identity: manifest (staging) and verification (HIL runner).
# Dot-source; no side effects.
#
# A staged package is the WDK package directory (IntelAvbFilter.inf/.sys/.cat/.man) plus avb-driver-package.json:
#   schema avb-driver-package/1; configuration; source revision; producing repository/workflow/run id/run attempt;
#   SHA256 of every package file; driver (.sys) SHA256; signer thumbprint + subject of the .sys and the .cat.
# The manifest is produced by deploy-staging AFTER signing and checked again before upload; the HIL runner verifies
# the downloaded package against the manifest, the authorization (revision, run, attempt, configuration, staged
# SHA256) and the pinned HIL test-signing certificate BEFORE anything touches the installed driver.
#
# Signer trust: the pinned certificate is the committed public certificate tools/ci/signing/hil-test-signer.cer
# (thumbprint derived from the file, reviewed through pull requests).  The runner must already trust it (one-time
# operator administration); this code never imports or trusts a certificate.

$AvbPackageFiles   = @('IntelAvbFilter.inf', 'IntelAvbFilter.sys', 'intelavbfilter.cat', 'IntelAvbFilter.man')
$AvbPackageSchema  = 'avb-driver-package/1'
$AvbPackageManifestName = 'avb-driver-package.json'

function Get-AvbFileSignature {
    param([string]$Path)
    $s = Get-AuthenticodeSignature -LiteralPath $Path
    $c = $s.SignerCertificate
    return @{ Status = [string]$s.Status; Thumbprint = $(if ($c) { $c.Thumbprint.ToUpperInvariant() } else { $null }); Subject = $(if ($c) { $c.Subject } else { $null }) }
}

# Pinned signer from the committed public certificate.  @{ Ok; Thumbprint; Subject; Error }
function Get-AvbPinnedSigner {
    param([string]$CertificatePath)
    if (-not (Test-Path -LiteralPath $CertificatePath)) {
        return @{ Ok = $false; Thumbprint = $null; Subject = $null; Error = "pinned HIL signer certificate not found: '$CertificatePath' (one-time setup: commit the public test-signing certificate)" }
    }
    try {
        $c = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2 -ArgumentList $CertificatePath
        if ($c.HasPrivateKey) { return @{ Ok = $false; Thumbprint = $null; Subject = $null; Error = "'$CertificatePath' contains a private key; only the public certificate may be committed" } }
        return @{ Ok = $true; Thumbprint = $c.Thumbprint.ToUpperInvariant(); Subject = $c.Subject; Error = '' }
    } catch {
        return @{ Ok = $false; Thumbprint = $null; Subject = $null; Error = "pinned HIL signer certificate unreadable: $($_.Exception.Message)" }
    }
}

# Locate a package file case-insensitively (the WDK writes the catalog in lower case).
function Get-AvbPackageFile {
    param([string]$PackageDir, [string]$Name)
    $hit = @(Get-ChildItem -LiteralPath $PackageDir -File -ErrorAction SilentlyContinue | Where-Object { $_.Name -ieq $Name })
    if ($hit.Count -eq 1) { return $hit[0].FullName }
    return $null
}

function New-AvbDriverPackageManifest {
    param([string]$PackageDir, [string]$Configuration, [string]$SourceRevision, [string]$Repository, [string]$Workflow,
          [string]$RunId, [string]$RunAttempt, [scriptblock]$GetSignature = ${function:Get-AvbFileSignature})
    $files = [ordered]@{}
    foreach ($n in $AvbPackageFiles) {
        $p = Get-AvbPackageFile -PackageDir $PackageDir -Name $n
        if (-not $p) { throw "package file missing: $n in '$PackageDir'" }
        $files[$n] = (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash
    }
    $extra = @(Get-ChildItem -LiteralPath $PackageDir -Force | Where-Object { $_.Name -ne $AvbPackageManifestName -and -not ($AvbPackageFiles -icontains $_.Name) })
    if ($extra.Count -gt 0) { throw "unexpected entries in the package directory: $(($extra | ForEach-Object { $_.Name }) -join ', ')" }
    $sysSig = & $GetSignature (Get-AvbPackageFile $PackageDir 'IntelAvbFilter.sys')
    $catSig = & $GetSignature (Get-AvbPackageFile $PackageDir 'intelavbfilter.cat')
    $m = [ordered]@{
        schema = $AvbPackageSchema; configuration = $Configuration; sourceRevision = $SourceRevision
        repository = $Repository; workflow = $Workflow; runId = [string]$RunId; runAttempt = [string]$RunAttempt
        createdUtc = (Get-Date).ToUniversalTime().ToString('o')
        driverSha256 = $files['IntelAvbFilter.sys']; files = $files
        signer = [ordered]@{ sysThumbprint = $sysSig.Thumbprint; catThumbprint = $catSig.Thumbprint; subject = $catSig.Subject }
    }
    ($m | ConvertTo-Json -Depth 5) | Set-Content -LiteralPath (Join-Path $PackageDir $AvbPackageManifestName) -Encoding UTF8
    return $m
}

# Full verification of a downloaded package.  Every expectation is mandatory; @{ Ok; Reasons; Manifest; DriverSha256 }.
# -RequireTrusted: the signature must also be trusted on THIS machine (HIL runner; never on the hosted builder).
function Test-AvbDriverPackage {
    param([string]$PackageDir, [string]$Configuration, [string]$ExpectedRevision, [string]$ExpectedRepository,
          [string]$ExpectedRunId, [string]$ExpectedRunAttempt, [string]$ExpectedDriverSha256, [string]$PinnedThumbprint,
          [switch]$RequireTrusted, [scriptblock]$GetSignature = ${function:Get-AvbFileSignature})
    $r = @()
    $res = @{ Ok = $false; Reasons = @(); Manifest = $null; DriverSha256 = $null }
    if (-not $PinnedThumbprint -or $PinnedThumbprint -notmatch '^[0-9A-Fa-f]{40}$') { $r += 'no pinned HIL signer thumbprint' }
    if (-not $ExpectedDriverSha256 -or $ExpectedDriverSha256 -notmatch '^[0-9A-Fa-f]{64}$') { $r += 'no authorized staged driver SHA256' }
    $mp = Join-Path $PackageDir $AvbPackageManifestName
    if (-not (Test-Path -LiteralPath $mp)) { $res.Reasons = $r + "package manifest missing: $mp"; return $res }
    try { $m = Get-Content -LiteralPath $mp -Raw | ConvertFrom-Json } catch { $res.Reasons = $r + "package manifest unreadable: $($_.Exception.Message)"; return $res }
    $res.Manifest = $m
    if ($m.schema -ne $AvbPackageSchema) { $r += "manifest schema '$($m.schema)' is not $AvbPackageSchema" }
    foreach ($pair in @(@('configuration', $Configuration), @('sourceRevision', $ExpectedRevision), @('repository', $ExpectedRepository),
                        @('runId', $ExpectedRunId), @('runAttempt', $ExpectedRunAttempt))) {
        if ([string]$m.($pair[0]) -cne [string]$pair[1]) { $r += "manifest $($pair[0]) '$($m.($pair[0]))' is not the authorized '$($pair[1])'" }
    }
    # Exact file set: every declared file present with its hash, nothing else in the directory.
    $declared = @()
    if ($m.files) { $declared = @($m.files.PSObject.Properties | ForEach-Object { $_.Name }) }
    foreach ($n in $AvbPackageFiles) { if ($declared -notcontains $n) { $r += "manifest does not declare $n" } }
    foreach ($n in $declared) {
        if ($AvbPackageFiles -notcontains $n) { $r += "manifest declares unexpected file $n"; continue }
        $p = Get-AvbPackageFile -PackageDir $PackageDir -Name $n
        if (-not $p) { $r += "package file missing: $n"; continue }
        $h = (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash
        if ($h -ne ([string]$m.files.$n).ToUpperInvariant()) { $r += "SHA256 of $n is $h, manifest says $($m.files.$n)" }
    }
    $extra = @(Get-ChildItem -LiteralPath $PackageDir -Force | Where-Object { $_.Name -ne $AvbPackageManifestName -and -not ($AvbPackageFiles -icontains $_.Name) })
    if ($extra.Count -gt 0) { $r += "unexpected entries in the package: $(($extra | ForEach-Object { $_.Name }) -join ', ')" }
    $sys = Get-AvbPackageFile -PackageDir $PackageDir -Name 'IntelAvbFilter.sys'
    if ($sys) {
        $res.DriverSha256 = (Get-FileHash -LiteralPath $sys -Algorithm SHA256).Hash
        if ($ExpectedDriverSha256 -and $res.DriverSha256 -ne $ExpectedDriverSha256.ToUpperInvariant()) { $r += "driver SHA256 $($res.DriverSha256) is not the authorized staged $($ExpectedDriverSha256.ToUpperInvariant())" }
        if ($m.driverSha256 -and $res.DriverSha256 -ne ([string]$m.driverSha256).ToUpperInvariant()) { $r += "driver SHA256 $($res.DriverSha256) differs from manifest driverSha256 $($m.driverSha256)" }
    }
    foreach ($n in 'IntelAvbFilter.sys', 'intelavbfilter.cat') {
        $p = Get-AvbPackageFile -PackageDir $PackageDir -Name $n
        if (-not $p) { continue }
        $sig = & $GetSignature $p
        if (-not $sig.Thumbprint) { $r += "$n is not signed"; continue }
        if ($PinnedThumbprint -and $sig.Thumbprint -ne $PinnedThumbprint.ToUpperInvariant()) { $r += "$n is signed by $($sig.Thumbprint) ($($sig.Subject)), not the pinned HIL signer $($PinnedThumbprint.ToUpperInvariant())" }
        if ($RequireTrusted -and $sig.Status -ne 'Valid') { $r += "$n signature status '$($sig.Status)' on this machine (the pinned signer must already be trusted; it is never imported automatically)" }
    }
    $res.Reasons = $r
    $res.Ok = ($r.Count -eq 0)
    return $res
}
