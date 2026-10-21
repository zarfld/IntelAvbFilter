# HIL test-signing identity (one-time administration)

Automatic HIL (`.github/workflows/hil.yml`) installs the driver packages produced by `deploy-staging`
(`.github/workflows/ci-standards-compliance.yml`). Every package must be signed by **one stable, controlled
test-signing certificate** that the HIL machine already trusts. CI never imports or trusts a certificate.

| Item | Where | Who |
|---|---|---|
| Private key + certificate (PFX) | repository secrets `HIL_TEST_SIGNING_PFX_BASE64`, `HIL_TEST_SIGNING_PFX_PASSWORD` | repository admin, once |
| Public certificate (pinned signer) | `tools/ci/signing/hil-test-signer.cer` (committed via pull request) | repository admin, once |
| Trust on the HIL machine | `LocalMachine\Root` + `LocalMachine\TrustedPublisher` of the runner machine | machine administrator, once |
| Test signing mode | `bcdedit /set testsigning on` (already required for any test-signed driver) | machine administrator, once |

Until all four exist, `deploy-staging` fails ("Stable signer not configured" / "Pinned signer") and HIL is not
executed. Nothing falls back to an ephemeral WDK certificate: hosted builders create a different `WDKTestCert` in
every job, which the HIL machine does not (and must not automatically) trust.

## Creating the identity (offline, on an administrator's machine)

```powershell
$cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=IntelAvbFilter HIL Test Signing' `
          -KeyUsage DigitalSignature -KeyExportPolicy Exportable -NotAfter (Get-Date).AddYears(3) `
          -CertStoreLocation Cert:\CurrentUser\My -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3', '2.5.29.19={text}')
$pw = Read-Host -AsSecureString 'PFX password'
Export-PfxCertificate -Cert $cert -FilePath .\hil-signer.pfx -Password $pw
Export-Certificate -Cert $cert -FilePath .\tools\ci\signing\hil-test-signer.cer   # public part only -> commit (PR)
[Convert]::ToBase64String([IO.File]::ReadAllBytes('.\hil-signer.pfx')) | Set-Clipboard   # -> secret HIL_TEST_SIGNING_PFX_BASE64
# PFX password -> secret HIL_TEST_SIGNING_PFX_PASSWORD; then delete hil-signer.pfx and remove the key from the store.
```

## Trusting it on the HIL machine (elevated, once)

```powershell
Import-Certificate -FilePath .\tools\ci\signing\hil-test-signer.cer -CertStoreLocation Cert:\LocalMachine\Root
Import-Certificate -FilePath .\tools\ci\signing\hil-test-signer.cer -CertStoreLocation Cert:\LocalMachine\TrustedPublisher
```

## Chain of custody (enforced, `tools/lib/DriverPackage.ps1`)

1. `deploy-staging` (master push only; secrets are never available to pull requests or forks) builds each
   configuration, re-signs the `.sys` and the regenerated catalog with the stable signer
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

Rotation: replace the secrets and the committed `.cer` in one pull request, then trust the new certificate on the
HIL machine; packages signed by the previous certificate are rejected from then on.
