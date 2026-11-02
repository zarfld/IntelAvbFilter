# HIL test-signing identity (one-time administration)

Automatic HIL (`.github/workflows/hil.yml`) installs the driver packages produced by `deploy-staging`
(`.github/workflows/ci-standards-compliance.yml`). Every package must be signed by **one stable, controlled
test-signing certificate** that the HIL machine already trusts. CI never imports or trusts a certificate.

| Item | Where | Who |
|---|---|---|
| Private key + certificate (PFX) | **repository-level** Actions secrets `HIL_TEST_SIGNING_PFX_BASE64`, `HIL_TEST_SIGNING_PFX_PASSWORD` (not environment secrets: `deploy-staging` declares no `environment`, so environment secrets never reach it) | repository admin, once |
| Recoverable backup of the PFX | encrypted, access-controlled storage outside the repository and outside GitHub; its password protected separately | repository admin, once (and on rotation) |
| Public certificate (pinned signer) | `tools/ci/signing/hil-test-signer.cer` (committed via pull request; the only `.cer` that `.gitignore` admits) | repository admin, once |
| Trust on the HIL machine | `LocalMachine\Root` + `LocalMachine\TrustedPublisher` of the runner machine | machine administrator, once |
| Test signing mode | `bcdedit /set testsigning on` (already required for any test-signed driver) | machine administrator, once |

Until the secrets, the committed certificate and the HIL-machine trust exist, `deploy-staging` fails ("Stable signer
not configured" / "Pinned signer") and HIL is not executed. Nothing falls back to an ephemeral WDK certificate: hosted
builders create a different `WDKTestCert` in every job, which the HIL machine does not (and must not automatically)
trust.

**Who receives the secrets.** `deploy-staging` runs for pushes to `master`, for `v*` tags, and for same-repository
pull requests into `master` authored by the repository OWNER (the mandatory pre-merge HIL candidate). GitHub passes
repository secrets to same-repository `pull_request` runs, never to pull requests from forks; fork pull requests and
pull requests by anyone but the OWNER are never staged and never receive these secrets.

## Private-key custody

GitHub secrets are **not** a backup: their values cannot be read back. Never destroy the only recoverable copy of the
private key. Required order:

1. Generate the identity in memory (it never enters a certificate store; no key container is persisted).
2. Export the password-protected PFX and the public certificate directly into the encrypted backup location.
3. The backup location is encrypted (BitLocker volume or EFS directory), NTFS and access-controlled (no access for
   Everyone / Users / Authenticated Users), outside the repository.
4. Protect the PFX password separately from the backup (at least 16 characters).
5. Verify that the backup opens with that password, holds the private key and matches the public certificate.
6. Configure the GitHub secrets.
7. There is no working copy to remove: the PFX exists only in the backup location. Clear the clipboard if it was used.

Never print a private key, the password or the PFX Base64 text (logs, transcripts, chat, reports). Never use the PFX
with `signtool /f` on a persistent machine: signtool imports the key and leaves it under
`%APPDATA%\Microsoft\Crypto\Keys` (observed 2026-10-30). The PFX signs only on the ephemeral GitHub-hosted runners of
`deploy-staging`.

## Creating the identity (offline, Windows PowerShell 5.1)

Run outside the repository. `$Backup` must already exist on the encrypted, access-controlled location (custody step 3);
an unencrypted local folder is not acceptable. Only the public certificate is written into the checkout. Existing
certificates, keys and local signing (WDK test signing, `Build-Driver.ps1 -Sign`) are not touched.

```powershell
$Repo   = 'C:\path\to\IntelAvbFilter'          # receives ONLY the public certificate
$Backup = 'X:\path\to\encrypted\backup'         # existing, encrypted, access-controlled, outside the repository
$pw = Read-Host -AsSecureString 'PFX password (input hidden, at least 16 characters)'
# In-memory identity: ephemeral RSA 3072 key, SHA-256, end entity (CA=false), DigitalSignature, Code Signing.
$rsa = New-Object System.Security.Cryptography.RSACng 3072
$req = New-Object System.Security.Cryptography.X509Certificates.CertificateRequest('CN=IntelAvbFilter HIL Test Signing', $rsa,
         [Security.Cryptography.HashAlgorithmName]::SHA256, [Security.Cryptography.RSASignaturePadding]::Pkcs1)
$req.CertificateExtensions.Add((New-Object System.Security.Cryptography.X509Certificates.X509BasicConstraintsExtension($false, $false, 0, $true)))
$req.CertificateExtensions.Add((New-Object System.Security.Cryptography.X509Certificates.X509KeyUsageExtension([Security.Cryptography.X509Certificates.X509KeyUsageFlags]::DigitalSignature, $true)))
$eku = New-Object System.Security.Cryptography.OidCollection; [void]$eku.Add((New-Object System.Security.Cryptography.Oid '1.3.6.1.5.5.7.3.3'))
$req.CertificateExtensions.Add((New-Object System.Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension($eku, $false)))
$req.CertificateExtensions.Add((New-Object System.Security.Cryptography.X509Certificates.X509SubjectKeyIdentifierExtension($req.PublicKey, $false)))
$cert = $req.CreateSelfSigned([DateTimeOffset]::UtcNow.AddMinutes(-5), [DateTimeOffset]::UtcNow.AddYears(3))
$pfx = Join-Path $Backup "IntelAvbFilter-HIL-Test-Signing-$($cert.Thumbprint).pfx"
$cer = Join-Path $Backup "IntelAvbFilter-HIL-Test-Signing-$($cert.Thumbprint).cer"
[IO.File]::WriteAllBytes($pfx, $cert.Export([Security.Cryptography.X509Certificates.X509ContentType]::Pfx, $pw))
[IO.File]::WriteAllBytes($cer, $cert.Export([Security.Cryptography.X509Certificates.X509ContentType]::Cert))
"thumbprint $($cert.Thumbprint), valid $($cert.NotBefore.ToString('u')) .. $($cert.NotAfter.ToString('u'))"
$cert.Dispose(); $rsa.Dispose()
```

Verify the **backup** (custody step 5): it must open with the password, hold the private key, and be the key pair of
the public certificate (a signature made with the backup's private key verifies with the public key). Managed
cryptography only - no signtool:

```powershell
# Same PowerShell session ($Repo, $pw, $pfx, $cer).
$flags = [System.Security.Cryptography.X509Certificates.X509KeyStorageFlags]::EphemeralKeySet
$b = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2($pfx, $pw, $flags)
$pub = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2($cer)
$data = [Text.Encoding]::UTF8.GetBytes('hil-signer backup check')
# GetRSAPrivateKey/GetRSAPublicKey are extension methods on .NET Framework: PowerShell 5.1 must call them statically.
$ext = [System.Security.Cryptography.X509Certificates.RSACertificateExtensions]
$sig = $ext::GetRSAPrivateKey($b).SignData($data, [Security.Cryptography.HashAlgorithmName]::SHA256, [Security.Cryptography.RSASignaturePadding]::Pkcs1)
"backup thumbprint matches: $($b.Thumbprint -eq $pub.Thumbprint); has private key: $($b.HasPrivateKey); key pair matches: " +
  $ext::GetRSAPublicKey($pub).VerifyData($data, $sig, [Security.Cryptography.HashAlgorithmName]::SHA256, [Security.Cryptography.RSASignaturePadding]::Pkcs1)
$b.Dispose()
```

All three must be `True`. Only then copy the public certificate into the checkout (commit it via pull request):

```powershell
Copy-Item -LiteralPath $cer -Destination (Join-Path $Repo 'tools\ci\signing\hil-test-signer.cer')
```

## Configuring the repository secrets (custody step 6)

Both are **repository** secrets (Settings > Secrets and variables > Actions > Secrets > Repository secrets > New
repository secret), never environment secrets: `HIL_TEST_SIGNING_PFX_BASE64` (the PFX file as Base64) and
`HIL_TEST_SIGNING_PFX_PASSWORD` (typed, never copied).

With the GitHub CLI (no clipboard): `gh secret set` reads the value from standard input or a masked prompt.

```powershell
[Convert]::ToBase64String([IO.File]::ReadAllBytes($pfx)) | gh secret set HIL_TEST_SIGNING_PFX_BASE64 -R zarfld/IntelAvbFilter
gh secret set HIL_TEST_SIGNING_PFX_PASSWORD -R zarfld/IntelAvbFilter    # type the PFX password at the masked prompt
```

With the web UI the Base64 has to pass through the clipboard. Before: Settings > System > Clipboard - "Clipboard
history" off and sharing across devices off; no remote-desktop session and no clipboard manager running. Then copy,
paste into the secret value, save, and clear the clipboard at once:

```powershell
Set-Clipboard -Value ([Convert]::ToBase64String([IO.File]::ReadAllBytes($pfx)))
# ... paste into HIL_TEST_SIGNING_PFX_BASE64, "Add secret" ...
Add-Type -AssemblyName System.Windows.Forms; [System.Windows.Forms.Clipboard]::Clear()
"clipboard empty: $([string]::IsNullOrEmpty((Get-Clipboard -Raw)))"
```

Residual risk: had clipboard history or cross-device sharing been on, the entry stays in Win+V history (clear it there)
or may have been synchronized; any process watching the clipboard could have read it. The Base64 is the
password-protected PFX, so it is not usable without the separately typed password.

## Trusting it on the HIL machine (elevated, once, additive)

Adds the public certificate only; existing trust entries are never removed or replaced. Kept separate from any driver
installation or HIL run.

```powershell
$Repo = 'C:\path\to\IntelAvbFilter'
$cerPath = Join-Path $Repo 'tools\ci\signing\hil-test-signer.cer'
$tp = (New-Object System.Security.Cryptography.X509Certificates.X509Certificate2($cerPath)).Thumbprint
foreach ($store in 'Root', 'TrustedPublisher') {
    if (Test-Path "Cert:\LocalMachine\$store\$tp") { "${store}: $tp already present - unchanged" }
    else { $null = Import-Certificate -FilePath $cerPath -CertStoreLocation "Cert:\LocalMachine\$store"; "${store}: imported $tp" }
}
```

## Chain of custody (enforced, `tools/lib/DriverPackage.ps1`)

1. `deploy-staging` (master pushes, `v*` tags and same-repository OWNER pull requests into master; the
   repository-level secrets never reach fork pull requests) builds each configuration from exactly the candidate
   revision, re-signs the `.sys` and the regenerated catalog with the stable signer
   (`Build-Driver.ps1 -SigningPfx`), verifies both signer thumbprints, writes `avb-driver-package.json`
   (revision, repository, workflow, run id, run attempt, configuration, SHA256 of every file, driver SHA256, signer)
   and self-checks it against the committed `hil-test-signer.cer` before uploading exactly the package.
2. `hil.yml` `authorize` (GitHub-hosted) downloads both packages **from the authorized upstream run id**, checks
   the manifest identity against the authorized revision / run / attempt / configuration and derives the staged
   SHA256 from the `.sys` itself.
3. The HIL job re-verifies on the machine, before anything touches the installed driver: exact file set, every
   SHA256, manifest identity, staged SHA256 from `authorize`, `.sys` and `.cat` signed by the pinned thumbprint,
   signature **trusted on this machine** (`Get-AuthenticodeSignature` status `Valid`).
4. After installation: installed `.sys` == staged SHA256 == service ImagePath file; service released then RUNNING
   during this deployment (the loaded image itself is not hashed - disclosed in the evidence).

Rotation: create and back up the new identity first (custody steps 1-5), then replace the secrets and the committed
`.cer` in one pull request, then trust the new certificate on the HIL machine; packages signed by the previous
certificate are rejected from then on. Keep the previous backup until the new identity has staged successfully.
