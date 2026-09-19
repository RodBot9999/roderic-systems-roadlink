param(
  [Parameter(Mandatory=$true)][string]$Compiler,
  [switch]$Zig,
  [string]$FirmwareDirectory = (Join-Path $PSScriptRoot '../../firmware/RoadLink')
)
$ErrorActionPreference = 'Stop'
if ($Zig) {
  $env:ZIG_GLOBAL_CACHE_DIR = Join-Path $PSScriptRoot '.zig-global-cache'
  $env:ZIG_LOCAL_CACHE_DIR = Join-Path $PSScriptRoot '.zig-local-cache'
}
$taskTestBuild = Join-Path ([System.IO.Path]::GetTempPath()) ('roadlink-modem-tests-' + [guid]::NewGuid())
New-Item -ItemType Directory -Path $taskTestBuild | Out-Null
function Read-TaskSource([string]$Name) {
  return [System.IO.File]::ReadAllText((Join-Path $FirmwareDirectory $Name))
}
function Remove-TaskIncludes([string]$Source) {
  return [regex]::Replace($Source, '(?m)^#(?:include|pragma)[^\r\n]*', '')
}
$taskGpsStruct = [regex]::Match((Read-TaskSource 'GpsService.h'), '(?s)struct GpsSnapshot \{.*?\n\};').Value
$taskObdStruct = [regex]::Match((Read-TaskSource 'ObdService.h'), '(?s)struct ObdLiveData \{.*?\n\};').Value
$taskStreamFields = [regex]::Match((Read-TaskSource 'AppState.h'), '(?s)namespace StreamField \{.*?\n\}').Value
if (!$taskGpsStruct -or !$taskObdStruct -or !$taskStreamFields) { throw 'Snapshot/config definitions not found' }
$taskFakes = [System.IO.File]::ReadAllText((Join-Path $PSScriptRoot 'ArduinoFakes.h'))
$taskHarness = [regex]::Replace($taskFakes, '(?m)^#pragma[^\r\n]*', '') +
  (Remove-TaskIncludes (Read-TaskSource 'AppConfig.h')) +
  $taskStreamFields + $taskGpsStruct + $taskObdStruct + @'
class GpsService {
public:
  GpsSnapshot data;
  const GpsSnapshot& snapshot() const { return data; }
  bool hasFix() const { return data.positionValid; }
};
class ObdService {
public:
  ObdLiveData data;
  uint16_t mask = StreamField::ALL;
  const ObdLiveData& liveData() const { return data; }
  uint16_t livePidMask() const { return mask; }
};
'@ +
  (Remove-TaskIncludes (Read-TaskSource 'A7670Service.h')) +
  [regex]::Replace((Read-TaskSource 'ModemReply.h'), '(?m)^#pragma[^\r\n]*', '') +
  (Remove-TaskIncludes (Read-TaskSource 'A7670Service.cpp')) +
  [System.IO.File]::ReadAllText((Join-Path $PSScriptRoot 'test_modem.cpp'))
$taskGeneratedSource = Join-Path $taskTestBuild 'modem_tests.cpp'
[System.IO.File]::WriteAllText($taskGeneratedSource, $taskHarness)
$taskExecutable = Join-Path $taskTestBuild 'modem_tests.exe'
$taskCompilerArgs = @('-std=c++17', '-O0', '-g', $taskGeneratedSource, '-o', $taskExecutable)
if ($Zig) { $taskCompilerArgs = @('c++', '-target', 'x86_64-windows-gnu') + $taskCompilerArgs }
& $Compiler @taskCompilerArgs
if ($LASTEXITCODE -ne 0) { throw 'Host modem test compilation failed' }
& $taskExecutable
if ($LASTEXITCODE -ne 0) { throw 'Modem regression tests failed' }
Write-Output "Generated test artifacts: $taskTestBuild"

$taskCanFrame = [regex]::Match((Read-TaskSource 'CanService.h'), '(?s)struct CanFrameSnapshot \{.*?\n\};').Value
$taskRemoteConfig = [regex]::Match((Read-TaskSource 'A7670Service.h'), '(?s)struct RemoteStreamingConfig \{.*?\n\};').Value
if (!$taskCanFrame -or !$taskRemoteConfig) { throw 'Service boundary definitions not found' }
$taskServiceHarness = [regex]::Replace($taskFakes, '(?m)^#pragma[^\r\n]*', '') +
  (Remove-TaskIncludes (Read-TaskSource 'AppConfig.h')) +
  (Remove-TaskIncludes (Read-TaskSource 'AppState.h')) +
  $taskCanFrame + $taskRemoteConfig +
  [System.IO.File]::ReadAllText((Join-Path $PSScriptRoot 'StreamingFakes.h')) +
  (Remove-TaskIncludes (Read-TaskSource 'GpsService.h')) +
  (Remove-TaskIncludes (Read-TaskSource 'GpsService.cpp')) +
  (Remove-TaskIncludes (Read-TaskSource 'ObdService.h')) +
  (Remove-TaskIncludes (Read-TaskSource 'ObdService.cpp')) +
  (Remove-TaskIncludes (Read-TaskSource 'StreamingController.h')) +
  (Remove-TaskIncludes (Read-TaskSource 'StreamingController.cpp')) +
  [System.IO.File]::ReadAllText((Join-Path $PSScriptRoot 'test_streaming.cpp'))
$taskServiceSource = Join-Path $taskTestBuild 'streaming_tests.cpp'
[System.IO.File]::WriteAllText($taskServiceSource, $taskServiceHarness)
$taskServiceExecutable = Join-Path $taskTestBuild 'streaming_tests.exe'
$taskCompilerArgs = @('-std=c++17', '-O0', '-g', $taskServiceSource, '-o', $taskServiceExecutable)
if ($Zig) { $taskCompilerArgs = @('c++', '-target', 'x86_64-windows-gnu') + $taskCompilerArgs }
& $Compiler @taskCompilerArgs
if ($LASTEXITCODE -ne 0) { throw 'Streaming service test compilation failed' }
& $taskServiceExecutable
if ($LASTEXITCODE -ne 0) { throw 'Streaming service tests failed' }
