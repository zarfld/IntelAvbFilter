$ErrorActionPreference = 'Stop'

$cer = '.\hil-test-signer.cer'
$expected = 'add fingerprint'

$c = [Security.Cryptography.X509Certificates.X509Certificate2]::new($cer)

try {
    if ($c.Thumbprint -ne $expected -or $c.HasPrivateKey) {
        throw 'STOP: Incorrect public certificate.'
    }

    foreach ($store in 'Root', 'TrustedPublisher') {
        $destination = "Cert:\LocalMachine\$store"

        $existing = @(Get-ChildItem $destination |
            Where-Object Thumbprint -EQ $expected)

        if ($existing.Count -eq 0) {
            Import-Certificate -FilePath $cer `
                -CertStoreLocation $destination `
                -ErrorAction Stop | Out-Null
        }

        $verified = @(Get-ChildItem $destination |
            Where-Object Thumbprint -EQ $expected)

        if ($verified.Count -ne 1) {
            throw "STOP: Certificate not verified in $destination"
        }

        Write-Host "PASS: $destination"
    }
}
finally {
    $c.Dispose()
}