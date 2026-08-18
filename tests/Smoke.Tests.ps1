param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$executablePath = Join-Path $repositoryRoot "x64\$Configuration\my-engine-demo.exe"
$script:failureCount = 0

function Assert-ExitCode {
    param(
        [Parameter(Mandatory = $true, Position = 0)]
        [string[]]$Arguments,

        [Parameter(Mandatory = $true, Position = 1)]
        [int]$ExpectedExitCode
    )

    if (-not (Test-Path -LiteralPath $executablePath)) {
        throw "Executable not found: $executablePath"
    }

    $process = Start-Process `
        -FilePath $executablePath `
        -ArgumentList $Arguments `
        -Wait `
        -PassThru

    if ($process.ExitCode -ne $ExpectedExitCode) {
        $script:failureCount++
        Write-Error -ErrorAction Continue `
            "Expected exit code $ExpectedExitCode, got $($process.ExitCode): $($Arguments -join ' ')"
    }
}

Assert-ExitCode @('--pipeline', 'unknown', '--frames', '1') 2
Assert-ExitCode @('--pipeline', 'vertex', '--frames', 'bad') 2
Assert-ExitCode @('--pipeline', 'vertex', '--frames', '0') 2
Assert-ExitCode @('--frames', '1') 2
Assert-ExitCode @('--pipeline') 2
Assert-ExitCode @('--pipeline', 'vertex', '--pipeline', 'mesh', '--frames', '1') 2
Assert-ExitCode @('--pipeline', 'vertex', '--unknown', 'value', '--frames', '1') 2
Assert-ExitCode @('--pipeline', 'vertex', '--frames', '1') 0
Assert-ExitCode @('--pipeline', 'vertex', '--frames', '3') 0
Assert-ExitCode @('--pipeline', 'mesh', '--frames', '1') 0
Assert-ExitCode @('--pipeline', 'mesh', '--frames', '3') 0

if ($script:failureCount -ne 0) {
    throw "$script:failureCount smoke test case(s) failed."
}

Write-Host "Smoke tests passed for $Configuration."
