param(
    [Parameter(Mandatory = $true)][string]$Setup,
    [Parameter(Mandatory = $true)][string]$FixtureDir,
    [string]$AssociationProbe
)
$ErrorActionPreference = 'Stop'
$Setup = (Resolve-Path -LiteralPath $Setup).Path
$FixtureDir = (Resolve-Path -LiteralPath $FixtureDir).Path
$root = Join-Path ([System.IO.Path]::GetTempPath()) ("kadro-install-" + [guid]::NewGuid().ToString('N'))
$install = Join-Path $root 'Проверка установки'
$identity = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Kadro_is1'
$classes = 'HKCU:\Software\Classes'
$capabilities = 'HKCU:\Software\Kadro\Capabilities'
$registered = 'HKCU:\Software\RegisteredApplications'
$choice = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.png\UserChoice'
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

    if ($AssociationProbe) {
        if ($env:GITHUB_ACTIONS -ne 'true') { throw 'Registry probe requires a disposable GitHub Actions runner' }
        $defaultBefore = Get-ItemPropertyValue -LiteralPath $choice -Name ProgId -ErrorAction SilentlyContinue
        $env:KADRO_CI_INSTALLED_EXE = Join-Path $install 'bin\Kadro.exe'
        $previousQpa = $env:QT_QPA_PLATFORM
        $probeLog = Join-Path $FixtureDir 'windows-native-probe.xml'
        try {
            $env:QT_QPA_PLATFORM = 'windows'
            $probeExecutable = (Resolve-Path -LiteralPath $AssociationProbe).Path
            $probeArgs = 'windowsAssociation windowsTrashOnDisposableRunner -o "' + $probeLog + ',xunitxml"'
            $probe = Start-Process -FilePath $probeExecutable -ArgumentList $probeArgs -PassThru
            if (-not $probe.WaitForExit(30000)) {
                Stop-Process -Id $probe.Id -ErrorAction SilentlyContinue
                throw 'Windows native probe timed out'
            }
            if ($probe.ExitCode -ne 0 -or !(Test-Path -LiteralPath $probeLog)) {
                throw "Windows native probe did not pass (exit $($probe.ExitCode)). See the XML artifact."
            }
            [xml]$result = Get-Content -LiteralPath $probeLog -Raw
            $association = $result.testsuite.testcase | Where-Object name -eq 'windowsAssociation'
            $trash = $result.testsuite.testcase | Where-Object name -eq 'windowsTrashOnDisposableRunner'
            if (!$association -or $association.skipped -or !$trash) { throw 'Missing required results from the Windows native probe' }
            if ($trash.skipped) {
                Write-Output 'Recycle-bin move unavailable on this runner; the file was preserved'
            } else {
                Write-Output 'PASS synthetic file moved to the Windows Recycle Bin'
            }
            $command = (Get-Item -LiteralPath "$classes\Kadro.Media\shell\open\command").GetValue('')
            if ($command -ne ('"' + $env:KADRO_CI_INSTALLED_EXE + '" "%1"')) {
                throw 'The installed executable is missing from the Windows registration'
            }
        } finally {
            $env:QT_QPA_PLATFORM = $previousQpa
            Remove-Item Env:KADRO_CI_INSTALLED_EXE
        }
    }

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
    if ($AssociationProbe) {
        foreach ($key in @("$classes\Kadro.Media", "$classes\Applications\Kadro.exe", $capabilities)) {
            if (Test-Path -LiteralPath $key) { throw "An owned association key was retained: $key" }
        }
        if (Get-ItemProperty -LiteralPath $registered -Name 'KADRO' -ErrorAction SilentlyContinue) {
            throw 'The owned RegisteredApplications value was retained'
        }
        if (Get-ItemProperty -LiteralPath "$classes\.png\OpenWithProgids" -Name 'Kadro.Media' -ErrorAction SilentlyContinue) {
            throw 'The owned PNG OpenWith value was retained'
        }
        $defaultAfter = Get-ItemPropertyValue -LiteralPath $choice -Name ProgId -ErrorAction SilentlyContinue
        if ($defaultAfter -ne $defaultBefore) { throw 'The selected PNG application was modified' }
        Write-Output 'PASS explicit registration and uninstall through the same installed path; UserChoice preserved'
    }
    Write-Output 'PASS Windows per-user install, installed app smoke, uninstall and untracked file preservation'
} finally {
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}
