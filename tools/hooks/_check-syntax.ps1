$errors = $null
$tokens = $null
[void][System.Management.Automation.Language.Parser]::ParseFile(
    'C:\Users\User\IntelAvbFilter\tools\hooks\pre-commit.ps1',
    [ref]$tokens,
    [ref]$errors
)
if ($errors.Count -eq 0) {
    Write-Host "NO SYNTAX ERRORS"
} else {
    $errors | ForEach-Object {
        Write-Host ("L{0}:{1} {2}" -f $_.Extent.StartLineNumber, $_.Extent.StartColumnNumber, $_.Message)
    }
}
