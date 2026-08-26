[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string] $QuetooRoot,

  [Parameter(Mandatory = $true)]
  [string] $DataRoot,

  [Parameter(Mandatory = $true)]
  [string] $OutputRoot,

  [string] $PortabilityFixture = '',

  [ValidateRange(30, 600)]
  [int] $TimeoutSeconds = 240
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0


function Get-FullPath([string] $Path) {
  return [System.IO.Path]::GetFullPath($Path).TrimEnd(
    [System.IO.Path]::DirectorySeparatorChar,
    [System.IO.Path]::AltDirectorySeparatorChar)
}

function Quote-Argument([string] $Value) {
  return '"' + $Value.Replace('"', '\"') + '"'
}

function Assert-FileHash([string] $Path, [string] $Expected) {
  if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
    throw "Runtime smoke input is missing: $Path"
  }
  $actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
  if ($actual -ne $Expected) {
    throw "Unexpected SHA-256 for ${Path}: expected $Expected, got $actual"
  }
}

function Assert-PeX64([string] $Path) {
  $stream = [System.IO.File]::OpenRead($Path)
  try {
    $reader = [System.IO.BinaryReader]::new($stream)
    try {
      if ($reader.ReadUInt16() -ne 0x5a4d) {
        throw "Not a PE image: $Path"
      }
      $stream.Position = 0x3c
      $peOffset = $reader.ReadInt32()
      $stream.Position = $peOffset
      if ($reader.ReadUInt32() -ne 0x00004550) {
        throw "Invalid PE signature: $Path"
      }
      if ($reader.ReadUInt16() -ne 0x8664) {
        throw "PE image is not Windows x86-64: $Path"
      }
    } finally {
      $reader.Dispose()
    }
  } finally {
    $stream.Dispose()
  }
}

function Add-Waits(
  [System.Collections.Generic.List[string]] $Commands,
  [int] $Count
) {
  for ($index = 0; $index -lt $Count; $index++) {
    $Commands.Add('wait')
  }
}

function Write-SmokeConfig(
  [string] $Path,
  [bool] $Screenshot,
  [bool] $Portability
) {
  $commands = [System.Collections.Generic.List[string]]::new()
  $commands.Add('debug net server client game')
  $commands.Add('time_scale 1')
  $commands.Add('sv_public 0')
  $commands.Add('sv_max_clients 2')
  $commands.Add('cl_max_fps 60')
  if ($Portability) {
    $commands.Add('set guid "01234567-89ab-4cde-8f01-23456789abcd"')
    $commands.Add('name "Runner"')
    $commands.Add('set g_race_physics quetoo-common-v1')
    $commands.Add('map edge')
  } else {
    $commands.Add('map potato')
  }
  Add-Waits $commands 600
  if ($Portability) {
    $commands.Add('race replay wr')
    $commands.Add('race replay pause')
    Add-Waits $commands 120
    $commands.Add('race replay step_forward')
    Add-Waits $commands 120
    $commands.Add('race replay speed 2')
    Add-Waits $commands 120
    $commands.Add('race replay resume')
    Add-Waits $commands 600
    $commands.Add('raceline wr')
    Add-Waits $commands 120
    if ($Screenshot) {
      $commands.Add('r_screenshot view')
    }
    Add-Waits $commands 120
    $commands.Add('raceline off')
    $commands.Add('echo RACE_WINDOWS_PERSISTENCE_READY')
  } else {
    if ($Screenshot) {
      $commands.Add('r_screenshot view')
    }
    $commands.Add('echo RACE_WINDOWS_SMOKE_READY')
  }
  Add-Waits $commands 120
  $commands.Add('quit')
  [System.IO.File]::WriteAllLines($Path, $commands,
    [System.Text.UTF8Encoding]::new($false))
}

function Invoke-SmokeProcess(
  [string] $Executable,
  [string] $WriteRoot,
  [string] $ConfigName,
  [string] $StdoutPath,
  [string] $StderrPath,
  [bool] $Hidden
) {
  $arguments = @(
    '--wpath', (Quote-Argument $WriteRoot),
    '-p', (Quote-Argument $DataRoot),
    '+set', 'version', '-1',
    '+set', 'sv_public', '0',
    '+game', 'race',
    '+exec', $ConfigName
  ) -join ' '

  $start = @{
    FilePath = $Executable
    WorkingDirectory = (Split-Path -Parent $Executable)
    ArgumentList = $arguments
    PassThru = $true
    RedirectStandardOutput = $StdoutPath
    RedirectStandardError = $StderrPath
  }
  if ($Hidden) {
    $start.WindowStyle = 'Hidden'
  }

  $process = Start-Process @start
  [void] $process.Handle
  if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
    try {
      $process.Kill()
      $process.WaitForExit()
    } catch {
      # Preserve the original bounded timeout as the failure.
    }
    throw "Runtime smoke timed out: $Executable"
  }
  $process.Refresh()
  if ($process.ExitCode -ne 0) {
    throw "Runtime smoke exited $($process.ExitCode): $Executable"
  }
}

function Assert-Log([string] $Path, [string[]] $Markers) {
  if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
    throw "Runtime log is missing: $Path"
  }
  $text = [System.IO.File]::ReadAllText($Path)
  foreach ($marker in $Markers) {
    if (-not $text.Contains($marker)) {
      throw "Runtime log lacks '$marker': $Path"
    }
  }
  if ($text -match '(?i)(access violation|fatal signal|sanitizer|stack trace)') {
    throw "Runtime log contains a crash marker: $Path"
  }
}

function Read-SharedText([string] $Path) {
  if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
    return ''
  }
  $stream = [System.IO.FileStream]::new(
    $Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read,
    [System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete)
  try {
    $reader = [System.IO.StreamReader]::new($stream)
    try {
      return $reader.ReadToEnd()
    } finally {
      $reader.Dispose()
    }
  } finally {
    $stream.Dispose()
  }
}

function Wait-LogContains(
  [System.Diagnostics.Process] $Process,
  [string] $Path,
  [string] $Marker,
  [DateTime] $NotBefore
) {
  $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
  while ([DateTime]::UtcNow -lt $deadline) {
    $Process.Refresh()
    if ($Process.HasExited) {
      throw "Runtime smoke exited before log marker '$Marker': pid=$($Process.Id) exit=$($Process.ExitCode)"
    }
    if (Test-Path -LiteralPath $Path -PathType Leaf) {
      $item = Get-Item -LiteralPath $Path
      if ($item.LastWriteTimeUtc -ge $NotBefore.AddSeconds(-1) -and
          (Read-SharedText $Path).Contains($Marker)) {
        return
      }
    }
    Start-Sleep -Milliseconds 100
  }
  throw "Runtime smoke timed out waiting for '$Marker': $Path"
}

function Send-RconCommand([int] $Port, [string] $Password, [string] $Command) {
  $body = [System.Text.Encoding]::ASCII.GetBytes(
    "rcon $Password $Command`0")
  $packet = [byte[]]::new(4 + $body.Length)
  for ($index = 0; $index -lt 4; $index++) {
    $packet[$index] = 0xff
  }
  [System.Array]::Copy($body, 0, $packet, 4, $body.Length)

  $udp = [System.Net.Sockets.UdpClient]::new()
  try {
    $sent = $udp.Send($packet, $packet.Length, '127.0.0.1', $Port)
    if ($sent -ne $packet.Length) {
      throw "RCON datagram was truncated: sent=$sent expected=$($packet.Length)"
    }
  } finally {
    $udp.Dispose()
  }
}

$QuetooRoot = Get-FullPath $QuetooRoot
$DataRoot = Get-FullPath $DataRoot
$OutputRoot = Get-FullPath $OutputRoot
$portabilityEnabled = -not [string]::IsNullOrWhiteSpace($PortabilityFixture)
if ($portabilityEnabled) {
  $PortabilityFixture = Get-FullPath $PortabilityFixture
  foreach ($name in @(
    'profile.profile',
    'map.state',
    'gset.cfg',
    'replay.qrpl',
    'wire.bin'
  )) {
    $path = Join-Path $PortabilityFixture $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
      throw "Portability fixture input is missing: $path"
    }
  }
}

$client = Join-Path $QuetooRoot 'bin\quetoo.exe'
$dedicated = Join-Path $QuetooRoot 'bin\quetoo-dedicated.exe'
$game = Join-Path $QuetooRoot 'lib\race\game.dll'
$cgame = Join-Path $QuetooRoot 'lib\race\cgame.dll'
$playerSkin = Join-Path $DataRoot 'default\players\qforcer\default.skin'
$potato = Join-Path $QuetooRoot 'share\race\maps\potato.bsp'
$edge = Join-Path $DataRoot 'default\maps\edge.bsp'


Assert-PeX64 $client
Assert-PeX64 $dedicated
Assert-PeX64 $game
Assert-PeX64 $cgame
$runtimeInputs = @($client, $dedicated, $game, $cgame, $playerSkin, $potato)
if ($portabilityEnabled) {
  $runtimeInputs += $edge
}
foreach ($required in $runtimeInputs) {
  if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
    throw "Runtime smoke input is missing: $required"
  }
}

if (Test-Path -LiteralPath $OutputRoot) {
  $existing = @(Get-ChildItem -LiteralPath $OutputRoot -Force)
  if ($existing.Count) {
    throw "Runtime smoke output must be empty: $OutputRoot"
  }
} else {
  [System.IO.Directory]::CreateDirectory($OutputRoot) | Out-Null
}

$dedicatedRoot = Join-Path $OutputRoot 'dedicated'
$clientRoot = Join-Path $OutputRoot 'client'
[System.IO.Directory]::CreateDirectory($dedicatedRoot) | Out-Null
[System.IO.Directory]::CreateDirectory($clientRoot) | Out-Null
if ($portabilityEnabled) {
  $profileDirectory = Join-Path $clientRoot 'profiles'
  $stateDirectory = Join-Path $clientRoot 'state\quetoo-common-v1'
  $replayDirectory = Join-Path $clientRoot 'replays\quetoo-common-v1\65646765'
  $raceDirectory = Join-Path $clientRoot 'race'
  foreach ($directory in @(
    $profileDirectory,
    $stateDirectory,
    $replayDirectory,
    $raceDirectory
  )) {
    [System.IO.Directory]::CreateDirectory($directory) | Out-Null
  }
  Copy-Item -LiteralPath (Join-Path $PortabilityFixture 'profile.profile') `
    -Destination (Join-Path $profileDirectory '01234567-89ab-4cde-8f01-23456789abcd.profile')
  Copy-Item -LiteralPath (Join-Path $PortabilityFixture 'map.state') `
    -Destination (Join-Path $stateDirectory '65646765.state')
  Copy-Item -LiteralPath (Join-Path $PortabilityFixture 'replay.qrpl') `
    -Destination (Join-Path $replayDirectory 'replay-dba8d53d1dc24cab.ghost')
  Copy-Item -LiteralPath (Join-Path $PortabilityFixture 'gset.cfg') `
    -Destination (Join-Path $raceDirectory 'gset.cfg')
  Copy-Item -LiteralPath (Join-Path $PortabilityFixture 'wire.bin') `
    -Destination (Join-Path $OutputRoot 'portability-wire.bin')
}
Write-SmokeConfig (Join-Path $clientRoot 'race-windows-client.cfg') `
  $true $portabilityEnabled

$dedicatedStdout = Join-Path $OutputRoot 'dedicated.stdout.log'
$dedicatedStderr = Join-Path $OutputRoot 'dedicated.stderr.log'
$dedicatedRuntimeLog = Join-Path $env:APPDATA `
  'WickedOldGames\Quetoo\default\quetoo.log'
$dedicatedEvidenceLog = Join-Path $OutputRoot 'dedicated.quetoo.log'
$dedicatedPort = 27981
$rconPassword = 'race-smoke-local'
$dedicatedArguments = @(
  '--wpath', (Quote-Argument $dedicatedRoot),
  '-p', (Quote-Argument $DataRoot),
  '+set', 'version', '-1',
  '+set', 'sv_public', '0',
  '+set', 'net_port', $dedicatedPort,
  '+set', 'rcon_password', $rconPassword,
  '+game', 'race',
  '+map', 'potato'
) -join ' '

$dedicatedStdin = Join-Path $OutputRoot 'dedicated.stdin'
[System.IO.File]::WriteAllBytes($dedicatedStdin, [byte[]]::new(0))
$dedicatedProcess = $null
try {
  $dedicatedStarted = [DateTime]::UtcNow
  $dedicatedProcess = Start-Process `
    -FilePath $dedicated `
    -WorkingDirectory (Split-Path -Parent $dedicated) `
    -ArgumentList $dedicatedArguments `
    -WindowStyle Hidden `
    -PassThru `
    -RedirectStandardInput $dedicatedStdin `
    -RedirectStandardOutput $dedicatedStdout `
    -RedirectStandardError $dedicatedStderr
  [void] $dedicatedProcess.Handle
  Wait-LogContains $dedicatedProcess $dedicatedRuntimeLog `
    'Server initialized' $dedicatedStarted
  # Stock background workers may still be retiring immediately after startup.
  # A bounded quiescence interval keeps shutdown on the normal host path.
  Start-Sleep -Milliseconds 1000
  Send-RconCommand $dedicatedPort $rconPassword 'quit'
  if (-not $dedicatedProcess.WaitForExit(30000)) {
    throw 'Dedicated runtime did not exit after authenticated local RCON quit.'
  }
  $dedicatedProcess.Refresh()
  if ($dedicatedProcess.ExitCode -ne 0) {
    throw "Dedicated runtime exited $($dedicatedProcess.ExitCode)."
  }
  [System.IO.File]::Copy($dedicatedRuntimeLog, $dedicatedEvidenceLog, $true)
} finally {
  if ($dedicatedProcess -and -not $dedicatedProcess.HasExited) {
    $dedicatedProcess.Kill()
    $dedicatedProcess.WaitForExit()
  }

}

Assert-Log $dedicatedEvidenceLog @(
  'lib/race/game.dll',
  'Race course: map=potato',
  'Server initialized',
  'Rcon from 127.0.0.1:'
)

Invoke-SmokeProcess `
  $client $clientRoot 'race-windows-client.cfg' `
  (Join-Path $OutputRoot 'client.stdout.log') `
  (Join-Path $OutputRoot 'client.stderr.log') $true

$clientMarkers = @(
  'lib/race/cgame.dll',
  'lib/race/game.dll',
  'Server initialized'
)
$clientLogPath = Join-Path $OutputRoot 'client.stdout.log'
if ($portabilityEnabled) {
  $clientMarkers += @(
    'Race course: map=edge',
    'RACE_WINDOWS_PERSISTENCE_READY',
    'Race global cvars: source=committed',
    'Race map state: status=validating-replays source=committed map=edge ruleset=quetoo-common-v1 generation=42 records=1 publication=replay-backed',
    'Race map state: replay validation complete map=edge records=1',
    'Replaying Runner - 0:00.050',
    'Replay paused.',
    'Replay stepped forward one sample and paused.',
    'Replay speed changed.',
    'Replay resumed.',
    'Raceline: Runner - 0:00.050',
    'Raceline hidden.'
  )
} else {
  $clientMarkers += @(
    'Race course: map=potato',
    'RACE_WINDOWS_SMOKE_READY'
  )
}
Assert-Log $clientLogPath $clientMarkers

if ($portabilityEnabled) {
  $clientText = [System.IO.File]::ReadAllText($clientLogPath)
  if ($clientText.Contains('Replay controls are cooling down.') -or
      $clientText.Contains('CMD_MAX_STRINGS exceeded')) {
    throw 'Persistence runtime smoke violated a host command boundary.'
  }

  foreach ($entry in @(
    [pscustomobject]@{ Source = 'profile.profile'; Destination = 'profiles\01234567-89ab-4cde-8f01-23456789abcd.profile' }
    [pscustomobject]@{ Source = 'map.state'; Destination = 'state\quetoo-common-v1\65646765.state' }
    [pscustomobject]@{ Source = 'gset.cfg'; Destination = 'race\gset.cfg' }
    [pscustomobject]@{ Source = 'replay.qrpl'; Destination = 'replays\quetoo-common-v1\65646765\replay-dba8d53d1dc24cab.ghost' }
    [pscustomobject]@{ Source = 'wire.bin'; Destination = '..\portability-wire.bin' }
  )) {
    $expectedPath = Join-Path $PortabilityFixture $entry.Source
    $actualPath = Get-FullPath (Join-Path $clientRoot $entry.Destination)
    $expectedHash = (Get-FileHash -LiteralPath $expectedPath -Algorithm SHA256).Hash
    Assert-FileHash $actualPath $expectedHash
  }
}

$screenshots = @(Get-ChildItem -LiteralPath (Join-Path $clientRoot 'screenshots') `
  -File -ErrorAction SilentlyContinue | Sort-Object FullName)
if ($screenshots.Count -ne 1 -or $screenshots[0].Length -le 0) {
  throw "Expected exactly one non-empty rendered client screenshot, found $($screenshots.Count)."
}

$clientHash = (Get-FileHash -LiteralPath $client -Algorithm SHA256).Hash
$dedicatedHash = (Get-FileHash -LiteralPath $dedicated -Algorithm SHA256).Hash
$gameHash = (Get-FileHash -LiteralPath $game -Algorithm SHA256).Hash
$cgameHash = (Get-FileHash -LiteralPath $cgame -Algorithm SHA256).Hash

$evidence = @(
  "format=1",
  "platform=windows",
  "architecture=x86_64",
  "portability_runtime=$([int] $portabilityEnabled)",
  "client_size=$((Get-Item -LiteralPath $client).Length)",
  "client_sha256=$clientHash",
  "dedicated_size=$((Get-Item -LiteralPath $dedicated).Length)",
  "dedicated_sha256=$dedicatedHash",
  "game_size=$((Get-Item -LiteralPath $game).Length)",
  "game_sha256=$gameHash",
  "cgame_size=$((Get-Item -LiteralPath $cgame).Length)",
  "cgame_sha256=$cgameHash",
  "dedicated_log_size=$((Get-Item -LiteralPath $dedicatedEvidenceLog).Length)",
  "dedicated_log_sha256=$((Get-FileHash -LiteralPath $dedicatedEvidenceLog -Algorithm SHA256).Hash)",
  "client_log_size=$((Get-Item -LiteralPath (Join-Path $OutputRoot 'client.stdout.log')).Length)",
  "client_log_sha256=$((Get-FileHash -LiteralPath (Join-Path $OutputRoot 'client.stdout.log') -Algorithm SHA256).Hash)",
  "screenshot_size=$($screenshots[0].Length)",
  "screenshot_sha256=$((Get-FileHash -LiteralPath $screenshots[0].FullName -Algorithm SHA256).Hash)"
)
if ($portabilityEnabled) {
  $portabilityFiles = @(
    [pscustomobject]@{ Name = 'profile'; Path = (Join-Path $clientRoot 'profiles\01234567-89ab-4cde-8f01-23456789abcd.profile') }
    [pscustomobject]@{ Name = 'map_state'; Path = (Join-Path $clientRoot 'state\quetoo-common-v1\65646765.state') }
    [pscustomobject]@{ Name = 'settings'; Path = (Join-Path $clientRoot 'race\gset.cfg') }
    [pscustomobject]@{ Name = 'replay'; Path = (Join-Path $clientRoot 'replays\quetoo-common-v1\65646765\replay-dba8d53d1dc24cab.ghost') }
    [pscustomobject]@{ Name = 'wire'; Path = (Join-Path $OutputRoot 'portability-wire.bin') }
  )
  $evidence += 'portability_replay_id=dba8d53d1dc24cab'
  foreach ($entry in $portabilityFiles) {
    $file = Get-Item -LiteralPath $entry.Path
    $evidence += "portability_$($entry.Name)_size=$($file.Length)"
    $evidence += "portability_$($entry.Name)_sha256=$((Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash)"
  }
}
[System.IO.File]::WriteAllLines((Join-Path $OutputRoot 'race-windows-smoke.meta'),
  $evidence, [System.Text.UTF8Encoding]::new($false))

Write-Output "RACE_WINDOWS_RUNTIME_PASS architecture=x86_64 dedicated=1 client=1 portability_runtime=$([int] $portabilityEnabled) screenshot=1"
