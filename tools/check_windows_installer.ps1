param(
    [Parameter(Mandatory = $true)][string]$Setup,
    [Parameter(Mandatory = $true)][string]$FixtureDir
)
$ErrorActionPreference = 'Stop'
$Setup = (Resolve-Path -LiteralPath $Setup).Path
$FixtureDir = (Resolve-Path -LiteralPath $FixtureDir).Path
$root = Join-Path ([System.IO.Path]::GetTempPath()) ("kadro-install-" + [guid]::NewGuid().ToString('N'))
$install = Join-Path $root 'Проверка установки'
$identity = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Kadro_is1'
New-Item -ItemType Directory -Path $root | Out-Null

function Invoke-Installer($File, $Arguments) {
    $process = Start-Process -FilePath $File -ArgumentList $Arguments -PassThru
    if (-not $process.WaitForExit(60000)) {
        Stop-Process -Id $process.Id -ErrorAction SilentlyContinue
        throw "Installer timed out: $File"
    }
    if ($process.ExitCode -ne 0) { throw "Installer returned $($process.ExitCode): $File" }
}

try {
    if (Test-Path -LiteralPath $identity) { throw 'Use a disposable Windows user without a previous installation' }
    Invoke-Installer $Setup "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /DIR=`"$install`""
    if (!(Test-Path -LiteralPath $identity)) { throw 'Missing HKCU uninstall entry' }
    $marker = Join-Path $install 'keep-this-file.txt'
    Set-Content -LiteralPath $marker -Value 'owned test fixture'
    python "${PSScriptRoot}\check_package.py" $install $FixtureDir
    if ($LASTEXITCODE -ne 0) { throw 'Installed app smoke failed' }

    $uninstaller = Get-ChildItem -LiteralPath $install -Filter 'unins*.exe' | Select-Object -First 1
    if (!$uninstaller) { throw 'Missing uninstaller' }
    Invoke-Installer $uninstaller.FullName '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART'
    # The Inno bootstrap can return before its temporary uninstaller exits.
    $limit = [DateTime]::UtcNow.AddSeconds(15)
    while ((Test-Path -LiteralPath $identity) -and [DateTime]::UtcNow -lt $limit) {
        Start-Sleep -Milliseconds 200
    }
    if (Test-Path -LiteralPath $identity) { throw 'Uninstall entry was retained' }
    if (Test-Path -LiteralPath (Join-Path $install 'bin\Kadro.exe')) { throw 'Installed executable was retained' }
    if ((Get-Content -LiteralPath $marker) -ne 'owned test fixture') { throw 'Uninstall modified an untracked file' }
    Write-Output 'PASS Windows per-user install, installed app smoke, uninstall and untracked file preservation'
} finally {
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}
