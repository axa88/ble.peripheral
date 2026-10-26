param(
	[string[]]$Environments = @('c3', 'wrover'),
	[int[]]$Modes = @(0, 1, 2, 3)
)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Split-Path $PSScriptRoot -Parent)).Path
$pioExe = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\pio.exe'
$pythonCommand = Get-Command -Name python -CommandType Application -ErrorAction Stop | Select-Object -First 1
$pythonExe = $pythonCommand.Source
$evidenceRoot = Join-Path $projectRoot '.pio\descriptor-fixture'
if ($null -eq $Environments -or @($Environments).Count -eq 0 -or $null -eq $Modes -or @($Modes).Count -eq 0) {
	throw 'At least one board environment and firmware mode must be selected.'
}
if (@($Environments | Where-Object { $_ -notin @('c3', 'wrover') }).Count -or
	@($Modes | Where-Object { $_ -notin @(0, 1, 2, 3) }).Count -or
	@($Environments | Group-Object | Where-Object Count -gt 1).Count -or
	@($Modes | Group-Object | Where-Object Count -gt 1).Count) { throw 'Unsupported or repeated board/mode selection.' }
if (-not (Test-Path -LiteralPath $pioExe -PathType Leaf)) { throw 'Pinned PlatformIO executable was not found.' }
New-Item -ItemType Directory -Path $evidenceRoot -Force | Out-Null
$oldFlags = $env:PLATFORMIO_BUILD_FLAGS
$oldEncoding = $env:PYTHONIOENCODING
$runId = '{0}-{1}' -f [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ'), [Guid]::NewGuid().ToString('N').Substring(0, 8)
$runRoot = Join-Path $evidenceRoot $runId
New-Item -ItemType Directory -Path $runRoot | Out-Null
$manifest = Join-Path $runRoot 'firmware-builds.jsonl'
$sourceSnapshotPath = Join-Path $runRoot 'source-snapshot.json'
$runSummaryPath = Join-Path $runRoot 'run-summary.json'
$sourceSnapshotRelative = ".pio/descriptor-fixture/$runId/source-snapshot.json"
$runSummaryRelative = ".pio/descriptor-fixture/$runId/run-summary.json"
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)

function Invoke-GitText([string[]]$Arguments) {
	$lines = @(& git -C $projectRoot @Arguments 2>&1 | ForEach-Object { [string]$_ })
	if ($LASTEXITCODE -ne 0) { throw 'Git metadata capture failed.' }
	return $lines
}

function Get-GitState {
	$revision = (Invoke-GitText -Arguments @('rev-parse', 'HEAD') | Select-Object -First 1).Trim()
	$status = @(Invoke-GitText -Arguments @('status', '--porcelain=v1', '--untracked-files=all') |
		Where-Object { $_ -notmatch '(?i)(platformio\.local\.ini|credential|secret|password|token|\.(pem|key))(?:\s|$)' })
	[pscustomobject]@{ revision = $revision; status = $status }
}

function Get-SourceHashes {
	$visiblePaths = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
	foreach ($path in (Invoke-GitText -Arguments @('ls-files', '--cached', '--others', '--exclude-standard'))) {
		[void]$visiblePaths.Add($path.Replace('\', '/'))
	}
	$files = [System.Collections.Generic.List[System.IO.FileInfo]]::new()
	foreach ($directory in @('src', 'web', 'include')) {
		$directoryPath = Join-Path $projectRoot $directory
		if (Test-Path -LiteralPath $directoryPath -PathType Container) {
			foreach ($file in (Get-ChildItem -LiteralPath $directoryPath -File -Recurse -Force)) { $files.Add($file) }
		}
	}
	foreach ($relative in @('platformio.ini', 'tools/generate-web-assets.py')) {
		$path = Join-Path $projectRoot $relative.Replace('/', '\')
		if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required source input is missing: $relative" }
		$files.Add((Get-Item -LiteralPath $path))
	}
	$hashes = [System.Collections.Generic.List[object]]::new()
	foreach ($file in $files) {
		$relative = $file.FullName.Substring($projectRoot.Length).TrimStart([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar).Replace('\', '/')
		if (-not $visiblePaths.Contains($relative) -or $relative -match '(?i)(?:^|/)(?:platformio\.local\.ini|[^/]*(credential|secret|password|token)[^/]*)(?:/|$)|\.(pem|key)$') { continue }
		$hashes.Add([pscustomobject]@{
			path = $relative
			sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
		})
	}
	return @($hashes | Sort-Object path)
}

function Invoke-PioSafeLines([string[]]$Arguments) {
	$lines = @(& $pioExe @Arguments 2>&1 | ForEach-Object { ([string]$_).Trim() } | Where-Object { $_ })
	if ($LASTEXITCODE -ne 0) { throw "Safe PlatformIO version/package query failed: $($Arguments[0])" }
	return $lines
}

function Write-JsonFile([string]$Path, [object]$Value) {
	$json = ConvertTo-Json -InputObject $Value -Depth 10
	[IO.File]::WriteAllText($Path, $json + "`r`n", $utf8NoBom)
}

function Add-JsonLine([string]$Path, [object]$Value) {
	$json = ConvertTo-Json -InputObject $Value -Depth 10 -Compress
	[IO.File]::AppendAllText($Path, $json + "`r`n", $utf8NoBom)
}

function Invoke-NativeRegression([string]$Name, [string]$RelativeScript) {
	$scriptPath = Join-Path $projectRoot $RelativeScript.Replace('/', '\')
	if (-not (Test-Path -LiteralPath $scriptPath -PathType Leaf)) { throw "Native regression runner is missing: $RelativeScript" }
	$logName = "native-$Name.log"
	$logPath = Join-Path $runRoot $logName
	& $pythonExe $scriptPath *> $logPath
	$exitCode = $LASTEXITCODE
	$result = if ($exitCode -eq 0) { 'Passed' } else { 'Failed' }
	$nativeChecks.Add([ordered]@{
		name = $Name; result = $result; exitCode = $exitCode
		log = ".pio/descriptor-fixture/$runId/$logName"
	})
	if ($exitCode -ne 0) { throw "Native regression $Name failed; see $logPath." }
}

$sourceUnchanged = $false
$sourceChangePaths = @()
$nativeChecks = [System.Collections.Generic.List[object]]::new()
try {
	$env:PYTHONIOENCODING = 'utf-8'
	$baselineGit = Get-GitState
	$baselineHashes = @(Get-SourceHashes)
	$pioCoreVersion = @(Invoke-PioSafeLines -Arguments @('--version'))
	$packageVersions = [ordered]@{}
	foreach ($board in @($Environments | Sort-Object -Unique)) {
		$packageOutput = @(Invoke-PioSafeLines -Arguments @('pkg', 'list', '--project-dir', $projectRoot, '-e', $board))
		$packageLines = @($packageOutput | Where-Object { $_ -match '(?i)\s@\s*\S+' })
		if (-not $packageLines.Count) { throw "No safe package version lines found for $board." }
		$packageVersions[$board] = $packageLines
	}
	$sourceSnapshot = [ordered]@{
		runId = $runId
		capturedAtUtc = [DateTime]::UtcNow.ToString('o')
		gitRevision = $baselineGit.revision
		gitStatus = $baselineGit.status
		sourceInputs = $baselineHashes
		versions = [ordered]@{ platformIOCore = $pioCoreVersion; packagesByEnvironment = $packageVersions }
	}
	Write-JsonFile $sourceSnapshotPath $sourceSnapshot
	$sourceSnapshotHash = (Get-FileHash -LiteralPath $sourceSnapshotPath -Algorithm SHA256).Hash.ToLowerInvariant()
	Invoke-NativeRegression -Name 'fixture' -RelativeScript 'test/descriptor_fixture_native/run.py'
	Invoke-NativeRegression -Name 'startup' -RelativeScript 'test/descriptor_startup_native/run.py'
	Invoke-NativeRegression -Name 'web-status' -RelativeScript 'test/web_status_native/run.py'
	[IO.File]::WriteAllText($manifest, '', $utf8NoBom)
	foreach ($board in $Environments) {
		foreach ($mode in $Modes) {
			$label = "${board}_mode${mode}"
			$log = Join-Path $runRoot "$label.log"
			$binary = Join-Path $runRoot "$label.bin"
			$env:PLATFORMIO_BUILD_FLAGS = if ([string]::IsNullOrWhiteSpace($oldFlags)) { "-DBLE_INTERFACE_MODE=$mode" } else { "$oldFlags -DBLE_INTERFACE_MODE=$mode" }
			& $pioExe run --project-dir $projectRoot -e $board *> $log
			$buildExitCode = $LASTEXITCODE
			if ($buildExitCode -ne 0) {
				Add-JsonLine $manifest ([ordered]@{
					runId = $runId; label = $label; environment = $board; mode = $mode; result = 'Failed'
					effectiveModeDefine = "-DBLE_INTERFACE_MODE=$mode"; exitCode = $buildExitCode
					sourceSnapshot = $sourceSnapshotRelative; sourceSnapshotSha256 = $sourceSnapshotHash
					log = ".pio/descriptor-fixture/$runId/$label.log"
				})
				throw "Build $label failed; see $log (do not publish credential-bearing diagnostics)."
			}
			$buildRoot = Join-Path $projectRoot ".pio\build\$board"
			Copy-Item -LiteralPath (Join-Path $buildRoot 'firmware.bin') -Destination $binary
			$partitionBytes = [IO.File]::ReadAllBytes((Join-Path $buildRoot 'partitions.bin'))
			$slots = @(for ($offset = 0; $offset + 32 -le $partitionBytes.Length; $offset += 32) {
				if ([BitConverter]::ToUInt16($partitionBytes, $offset) -ne 0x50AA) { continue }
				if ($partitionBytes[$offset + 2] -eq 0 -and $partitionBytes[$offset + 3] -in @(0x10, 0x11)) {
					[BitConverter]::ToUInt32($partitionBytes, $offset + 8)
				}
			})
			$binarySize = (Get-Item -LiteralPath $binary).Length
			if ($slots.Count -ne 2 -or @($slots | Where-Object { $binarySize -gt $_ }).Count) { throw "$label does not fit both OTA slots." }
			$elfBytes = [IO.File]::ReadAllBytes((Join-Path $buildRoot 'firmware.elf'))
			$elfText = [Text.Encoding]::ASCII.GetString($elfBytes)
			$assetPresent = $elfText.Contains('indexGzip')
			if ($assetPresent -ne (($mode -band 2) -ne 0)) { throw "$label Web asset inclusion does not match mode." }
			$logText = [IO.File]::ReadAllText($log)
			$record = [ordered]@{
				runId = $runId; label = $label; environment = $board; mode = $mode; result = 'Passed'
				effectiveModeDefine = "-DBLE_INTERFACE_MODE=$mode"
				command = "pio run --project-dir . -e $board (PLATFORMIO_BUILD_FLAGS appends -DBLE_INTERFACE_MODE=$mode)"
				sourceSnapshot = $sourceSnapshotRelative; sourceSnapshotSha256 = $sourceSnapshotHash
				runSummary = $runSummaryRelative
				binary = ".pio/descriptor-fixture/$runId/$label.bin"; bytes = $binarySize
				sha256 = (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash.ToLowerInvariant()
				otaSlotSizes = $slots; webAsset = $assetPresent
				warnings = @([regex]::Matches($logText, '(?m)^.*warning:.*$') | ForEach-Object { $_.Value.Trim() })
				log = ".pio/descriptor-fixture/$runId/$label.log"
			}
			Add-JsonLine $manifest $record
			Write-Output "$label passed: $binarySize bytes; Web asset $assetPresent"
		}
	}
} finally {
	if ($null -eq $oldFlags) { Remove-Item Env:PLATFORMIO_BUILD_FLAGS -ErrorAction SilentlyContinue } else { $env:PLATFORMIO_BUILD_FLAGS = $oldFlags }
	if ($null -eq $oldEncoding) { Remove-Item Env:PYTHONIOENCODING -ErrorAction SilentlyContinue } else { $env:PYTHONIOENCODING = $oldEncoding }
	if ($baselineGit -and $baselineHashes) {
		$finalGit = Get-GitState
		$finalHashes = @(Get-SourceHashes)
		$baselineMap = @{}
		$finalMap = @{}
		foreach ($item in $baselineHashes) { $baselineMap[$item.path] = $item.sha256 }
		foreach ($item in $finalHashes) { $finalMap[$item.path] = $item.sha256 }
		$allInputPaths = @($baselineMap.Keys + $finalMap.Keys | Sort-Object -Unique)
		$sourceChangePaths = @($allInputPaths | Where-Object { $baselineMap[$_] -ne $finalMap[$_] })
		$sourceUnchanged = $sourceChangePaths.Count -eq 0
		Write-JsonFile $runSummaryPath ([ordered]@{
			runId = $runId
			completedAtUtc = [DateTime]::UtcNow.ToString('o')
			sourceSnapshot = $sourceSnapshotRelative
			sourceSnapshotSha256 = if ($sourceSnapshotHash) { $sourceSnapshotHash } else { $null }
			finalGitRevision = $finalGit.revision
			finalGitStatus = $finalGit.status
			finalSourceInputs = $finalHashes
			sourceInputsUnchanged = $sourceUnchanged
			changedSourceInputs = $sourceChangePaths
			nativeChecks = @($nativeChecks)
		})
	}
}
if (-not $sourceUnchanged) { throw "Source inputs changed during this run; review $runSummaryRelative." }
