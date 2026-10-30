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

1. Generate (or recover) the identity.
2. Export the PFX and the public certificate.
3. Store an encrypted, access-controlled backup of the PFX.
4. Protect its password separately from the backup.
5. Verify that the backup opens with that password, holds the private key and matches the public certificate.
6. Configure the GitHub secrets.
7. Only then remove temporary working copies (the working PFX and the key in the certificate store).

Never print a private key, the password or the PFX Base64 text (logs, transcripts, chat, reports), and avoid the
clipboard.

## Creating the identity (offline, on an administrator's machine)

Run **outside** the repository. Only the public certificate is written into the checkout; the PFX stays in a private
working folder.

```powershell
$Repo = 'C:\path\to\IntelAvbFilter'                   # repository checkout: receives ONLY the public .cer
$Work = Join-Path $env:USERPROFILE 'hil-signer-work'   # private temporary folder, never inside the repository
New-Item -ItemType Directory -Path $Work -Force | Out-Null
$cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=IntelAvbFilter HIL Test Signing' `
          -KeyUsage DigitalSignature -KeyExportPolicy Exportable -NotAfter (Get-Date).AddYears(3) `
          -CertStoreLocation Cert:\CurrentUser\My -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3', '2.5.29.19={text}')
$pw = Read-Host -AsSecureString 'PFX password'
$pfx = Join-Path $Work 'hil-signer.pfx'
Export-PfxCertificate -Cert $cert -FilePath $pfx -Password $pw | Out-Null
Export-Certificate -Cert $cert -FilePath (Join-Path $Repo 'tools\ci\signing\hil-test-signer.cer') | Out-Null   # public part -> commit (PR)
"thumbprint $($cert.Thumbprint), valid $($cert.NotBefore.ToString('u')) .. $($cert.NotAfter.ToString('u'))"
```

Copy `$pfx` to the encrypted backup location and store the password separately (custody steps 3-4). Then verify the
**backup copy** (step 5) - it must open with the password, hold the private key, and be the key pair of the committed
certificate (a signature made with the backup's private key verifies with the committed public key):

```powershell
# Same PowerShell session as the creation step ($Repo, $pw, $pfx, $cert).
$Backup = 'X:\path\to\backup\hil-signer.pfx'          # the backup copy, not the working copy
$flags = [System.Security.Cryptography.X509Certificates.X509KeyStorageFlags]::EphemeralKeySet
$b = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2($Backup, $pw, $flags)
$pub = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2((Join-Path $Repo 'tools\ci\signing\hil-test-signer.cer'))
$data = [Text.Encoding]::UTF8.GetBytes('hil-signer backup check')
$sig = $b.GetRSAPrivateKey().SignData($data, [Security.Cryptography.HashAlgorithmName]::SHA256, [Security.Cryptography.RSASignaturePadding]::Pkcs1)
"backup thumbprint matches: $($b.Thumbprint -eq $pub.Thumbprint); has private key: $($b.HasPrivateKey); key pair matches: " +
  $pub.GetRSAPublicKey().VerifyData($data, $sig, [Security.Cryptography.HashAlgorithmName]::SHA256, [Security.Cryptography.RSASignaturePadding]::Pkcs1)
$b.Dispose()
```

All three must be `True`. Then configure the **repository** secrets without the clipboard and without printing them
(GitHub CLI, step 6; `gh secret set` reads the value from standard input, or prompts with masked input):

```powershell
[Convert]::ToBase64String([IO.File]::ReadAllBytes($pfx)) | gh secret set HIL_TEST_SIGNING_PFX_BASE64 -R zarfld/IntelAvbFilter
gh secret set HIL_TEST_SIGNING_PFX_PASSWORD -R zarfld/IntelAvbFilter    # type the PFX password at the masked prompt
gh secret list -R zarfld/IntelAvbFilter                                 # names and dates only
```

Only after the backup verification succeeded and both secrets are set, remove the temporary working copies (step 7):

```powershell
Remove-Item -LiteralPath $pfx
Remove-Item -LiteralPath "Cert:\CurrentUser\My\$($cert.Thumbprint)" -DeleteKey
```

## Trusting it on the HIL machine (elevated, once)

```powershell
$Repo = 'C:\path\to\IntelAvbFilter'
Import-Certificate -FilePath (Join-Path $Repo 'tools\ci\signing\hil-test-signer.cer') -CertStoreLocation Cert:\LocalMachine\Root
Import-Certificate -FilePath (Join-Path $Repo 'tools\ci\signing\hil-test-signer.cer') -CertStoreLocation Cert:\LocalMachine\TrustedPublisher
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
