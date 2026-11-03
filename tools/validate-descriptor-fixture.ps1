param(
	[string[]]$Environments = @('c3', 'wrover'),
	[int[]]$Modes = @(0, 1, 2, 3)
)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Split-Path $PSScriptRoot -Parent)).Path
$pioExe = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\pio.exe'
$pythonCommand = Get-Command -Name python -CommandType Application -ErrorAction Stop | Select-Object -First 1
$pythonExe = $pythonCommand.Source
$nodeCommand = Get-Command -Name node -CommandType Application -ErrorAction Stop | Select-Object -First 1
$nodeExe = $nodeCommand.Source
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
	foreach ($directory in @(
		'src', 'web', 'include', 'test/firmware_revision', 'test/gatt_publication_native',
		'test/descriptor_fixture_native', 'test/descriptor_startup_native', 'test/web_status_native', 'test/dynamic_gatt_native', 'test/dynamic_entry_native', 'test/dynamic_recovery_native', 'test/dynamic_topology_native', 'test/dynamic_advertising_native'
	)) {
		$directoryPath = Join-Path $projectRoot $directory
		if (Test-Path -LiteralPath $directoryPath -PathType Container) {
			foreach ($file in (Get-ChildItem -LiteralPath $directoryPath -File -Recurse -Force)) { $files.Add($file) }
		}
	}
	foreach ($relative in @(
		'.gitignore', 'platformio.ini', 'firmware-revision.json',
		'tools/generate-web-assets.py', 'tools/firmware_revision.py',
		'tools/pio_firmware_identity.py', 'tools/pio_firmware_identity_post.py',
		'tools/validate-descriptor-fixture.ps1', 'test/verify-advertising-stop-completion.ps1',
        'test/verify-descriptor-fixture.py', 'test/web_ui_regression.cjs', 'test/README'
	)) {
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

function Invoke-NodeRegression([string]$Name, [string]$RelativeScript) {
	$scriptPath = Join-Path $projectRoot $RelativeScript.Replace('/', '\')
	if (-not (Test-Path -LiteralPath $scriptPath -PathType Leaf)) { throw "Node regression script is missing: $RelativeScript" }
	$logName = "native-$Name.log"
	$logPath = Join-Path $runRoot $logName
	& $nodeExe $scriptPath *> $logPath
	$exitCode = $LASTEXITCODE
	$result = if ($exitCode -eq 0) { 'Passed' } else { 'Failed' }
	$nativeChecks.Add([ordered]@{
		name = $Name; result = $result; exitCode = $exitCode
		log = ".pio/descriptor-fixture/$runId/$logName"
	})
	if ($exitCode -ne 0) { throw "Node regression $Name failed; see $logPath." }
}

function Remove-InheritedInterfaceMode([string]$Flags) {
	if ([string]::IsNullOrWhiteSpace($Flags)) { return '' }
	$definitionPattern = '(?i)(?<!\S)(?:"-D\s*BLE_INTERFACE_MODE(?:\s*=\s*[^"]*)?"|''-D\s*BLE_INTERFACE_MODE(?:\s*=\s*[^'']*)?''|-D\s*(?:"BLE_INTERFACE_MODE(?:\s*=\s*[^"]*)?"|''BLE_INTERFACE_MODE(?:\s*=\s*[^'']*)?''|BLE_INTERFACE_MODE(?:\s*=\s*[^\s]+)?))'
	$withoutMode = [regex]::Replace($Flags, $definitionPattern, ' ')
	if ($withoutMode -match '(?i)\bBLE_INTERFACE_MODE\b') {
		throw 'Inherited PLATFORMIO_BUILD_FLAGS contains an unsupported BLE_INTERFACE_MODE token; refusing to risk a conflicting effective definition.'
	}
	return $withoutMode.Trim()
}

function Get-SconsVariableBlocks([string[]]$Lines, [string]$Variable) {
	$keyPattern = '^\s*\{?\s*[''"]?' + [regex]::Escape($Variable) + '[''"]?\s*(?::|=)\s*(?<value>.*)$'
	$nextKeyPattern = '^\s*\{?\s*[''\"]?[A-Za-z_][A-Za-z0-9_]*[''\"]?\s*(?::|=)\s*'
	$blocks = [System.Collections.Generic.List[string]]::new()
	for ($i = 0; $i -lt $Lines.Count; $i++) {
		$match = [regex]::Match($Lines[$i], $keyPattern)
		if (-not $match.Success) { continue }
		$indent = $Lines[$i].Length - $Lines[$i].TrimStart().Length
		$block = [System.Collections.Generic.List[string]]::new()
		$block.Add($match.Groups['value'].Value)
		for ($j = $i + 1; $j -lt $Lines.Count; $j++) {
			$nextIndent = $Lines[$j].Length - $Lines[$j].TrimStart().Length
			if ($nextIndent -le $indent -and $Lines[$j] -match $nextKeyPattern) { break }
			$block.Add($Lines[$j])
		}
		$blocks.Add(($block -join "`n"))
	}
	return @($blocks)
}

function Get-SconsModeTokens([string[]]$Blocks, [string]$Variable, [string]$Label) {
	$values = [System.Collections.Generic.List[string]]::new()
	foreach ($block in $Blocks) {
		$matches = [regex]::Matches($block, '(?i)(?<![A-Z0-9_])(?:-D\s*)?BLE_INTERFACE_MODE\s*=\s*["'']?([0-9]+)["'']?(?![A-Z0-9_])')
		foreach ($match in $matches) { $values.Add($match.Groups[1].Value) }
		$tupleMatches = [regex]::Matches($block, '(?i)[''"]BLE_INTERFACE_MODE[''"]\s*(?:,|:)\s*[''"]?([0-9]+)[''"]?')
		foreach ($match in $tupleMatches) { $values.Add($match.Groups[1].Value) }
		$mentionCount = [regex]::Matches($block, '(?i)(?<![A-Z0-9_])["'']?(?:-D\s*|/D\s*)?["'']?BLE_INTERFACE_MODE["'']?(?![A-Z0-9_])').Count
		if ($mentionCount -ne ($matches.Count + $tupleMatches.Count)) {
			throw "$Label SCons $Variable contains an unrecognized BLE_INTERFACE_MODE representation."
		}
	}
	return @($values)
}

function Get-SconsModeEvidence([string[]]$Lines, [int]$Mode, [string]$Label) {
	$cppDefines = @(Get-SconsVariableBlocks -Lines $Lines -Variable 'CPPDEFINES')
	if (-not $cppDefines.Count) { throw "$Label SCons envdump did not expose CPPDEFINES." }
	$cppModeValues = @(Get-SconsModeTokens -Blocks $cppDefines -Variable 'CPPDEFINES' -Label $Label)
	if (-not $cppModeValues.Count -or @($cppModeValues | Where-Object { $_ -ne [string]$Mode }).Count) {
		throw "$Label effective SCons CPPDEFINES contain a missing or conflicting BLE_INTERFACE_MODE value."
	}
	$ccFlags = @(Get-SconsVariableBlocks -Lines $Lines -Variable 'CCFLAGS')
	$ccModeValues = @(Get-SconsModeTokens -Blocks $ccFlags -Variable 'CCFLAGS' -Label $Label)
	if (@($ccModeValues | Where-Object { $_ -ne [string]$Mode }).Count) {
		throw "$Label SCons CCFLAGS contains a conflicting BLE_INTERFACE_MODE override."
	}
	return [ordered]@{
		evidenceSource = 'pinned PlatformIO/SCons CPPDEFINES and CCFLAGS envdump'
		sourceCompileCommandCount = 0
		cppDefinesListCount = $cppDefines.Count
		verifiedCppDefinesModeDefinitionCount = $cppModeValues.Count
		cppDefinesModeValues = @($cppModeValues | Sort-Object -Unique)
		ccFlagsModeDefinitionCount = $ccModeValues.Count
		ccFlagsModeValues = @($ccModeValues | Sort-Object -Unique)
		effectiveModeDefine = "-DBLE_INTERFACE_MODE=$Mode"
	}
}

function Get-CompilerModeEvidence([string[]]$Lines, [int]$Mode, [string]$Label, [string]$Board) {
	$compilerLines = @($Lines | Where-Object {
		$_ -match '(?i)(?:g\+\+|gcc)(?:\.exe)?' -and $_ -match '(^|\s)-c(\s|$)'
	})
	$sourceLines = @($compilerLines | Where-Object { $_ -match '(?i)(?:^|[\\/\s])src[\\/]' })
	if ($sourceLines.Count) {
		$modeDefinitionCounts = [System.Collections.Generic.List[int]]::new()
		$observedModeValues = [System.Collections.Generic.List[string]]::new()
		foreach ($line in $sourceLines) {
			$definitions = [regex]::Matches($line, '(?i)(?<![A-Z0-9_])["'']?(?:-D\s*|/D\s*)["'']?BLE_INTERFACE_MODE["'']?\s*=\s*["'']?([0-9]+)["'']?["'']?(?![A-Z0-9_])')
			$modeMentions = [regex]::Matches($line, '(?i)(?<![A-Z0-9_])["'']?(?:-D\s*|/D\s*)?["'']?BLE_INTERFACE_MODE["'']?(?![A-Z0-9_])').Count
			if (-not $definitions.Count -or $modeMentions -ne $definitions.Count -or
				@($definitions | Where-Object { $_.Groups[1].Value -ne [string]$Mode }).Count) {
				throw "$Label source compiler arguments omit the requested BLE_INTERFACE_MODE or contain a conflicting/unrecognized definition."
			}
			$modeDefinitionCounts.Add($definitions.Count)
			foreach ($definition in $definitions) { $observedModeValues.Add($definition.Groups[1].Value) }
		}
		return [ordered]@{
			evidenceSource = 'source compiler commands'
			sourceCompileCommandCount = $sourceLines.Count
			minimumModeDefinitionCountPerCommand = ($modeDefinitionCounts | Measure-Object -Minimum).Minimum
			maximumModeDefinitionCountPerCommand = ($modeDefinitionCounts | Measure-Object -Maximum).Maximum
			modeValues = @($observedModeValues | Sort-Object -Unique)
			effectiveModeDefine = "-DBLE_INTERFACE_MODE=$Mode"
		}
	}

	$sconsLines = @(& $pioExe run --project-dir $projectRoot -e $Board --target envdump 2>&1 | ForEach-Object { [string]$_ })
	if ($LASTEXITCODE -ne 0) { throw "$Label could not query the effective PlatformIO/SCons environment for incremental-build mode evidence." }
	return Get-SconsModeEvidence -Lines $sconsLines -Mode $Mode -Label $Label
}

function Write-SafeBuildLog([string]$Path, [string[]]$Lines) {
	$safeLines = foreach ($line in $Lines) {
		if ($line -match '(?i)(?:g\+\+|gcc)(?:\.exe)?' -and $line -match '(^|\s)-c(\s|$)') {
			'[compiler invocation redacted; effective mode verified in build manifest]'
			continue
		}
		$line -replace '(?i)(?<!\S)-D\s*(?!BLE_INTERFACE_MODE\b)([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(?:"(?:\\.|[^"])*"|\S+)', '-D$1=<redacted>'
	}
	[IO.File]::WriteAllText($Path, ($safeLines -join [Environment]::NewLine) + [Environment]::NewLine, $utf8NoBom)
}

$sourceUnchanged = $false
$sourceChangePaths = @()
$nativeChecks = [System.Collections.Generic.List[object]]::new()
$identityPreparation = $null
try {
	$env:PYTHONIOENCODING = 'utf-8'
	$identityPrepareLog = Join-Path $runRoot 'firmware-identity-prepare.log'
	$identityScript = Join-Path $projectRoot 'tools\firmware_revision.py'
	& $pythonExe $identityScript prepare --project-root $projectRoot *> $identityPrepareLog
	$identityPrepareExitCode = $LASTEXITCODE
	if ($identityPrepareExitCode -ne 0) { throw "Firmware identity preparation failed; see $identityPrepareLog." }
	$revisionStatePath = Join-Path $projectRoot 'firmware-revision.json'
	if (-not (Test-Path -LiteralPath $revisionStatePath -PathType Leaf)) { throw 'Firmware revision state was not created.' }
	$revisionState = Get-Content -LiteralPath $revisionStatePath -Raw | ConvertFrom-Json
	if ($revisionState.revision -lt 1 -or $revisionState.contentFingerprint -notmatch '^[0-9a-f]{64}$') {
		throw 'Firmware revision state did not produce a valid source identity.'
	}
	$identityPreparation = [ordered]@{
		result = 'Passed'; exitCode = $identityPrepareExitCode; log = ".pio/descriptor-fixture/$runId/firmware-identity-prepare.log"
		revision = $revisionState.revision; sourceId = $revisionState.contentFingerprint
	}
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
		firmwareIdentity = $identityPreparation
		versions = [ordered]@{ platformIOCore = $pioCoreVersion; packagesByEnvironment = $packageVersions }
	}
	Write-JsonFile $sourceSnapshotPath $sourceSnapshot
	$sourceSnapshotHash = (Get-FileHash -LiteralPath $sourceSnapshotPath -Algorithm SHA256).Hash.ToLowerInvariant()
	Invoke-NativeRegression -Name 'firmware-revision' -RelativeScript 'test/firmware_revision/run.py'
	Invoke-NativeRegression -Name 'publication' -RelativeScript 'test/gatt_publication_native/run.py'
	Invoke-NativeRegression -Name 'fixture' -RelativeScript 'test/descriptor_fixture_native/run.py'
	Invoke-NativeRegression -Name 'startup' -RelativeScript 'test/descriptor_startup_native/run.py'
	Invoke-NativeRegression -Name 'web-status' -RelativeScript 'test/web_status_native/run.py'
	Invoke-NativeRegression -Name 'dynamic-gatt' -RelativeScript 'test/dynamic_gatt_native/run.py'
	Invoke-NativeRegression -Name 'dynamic-topology' -RelativeScript 'test/dynamic_topology_native/run.py'
	Invoke-NativeRegression -Name 'fixture-source' -RelativeScript 'test/verify-descriptor-fixture.py'
	Invoke-NodeRegression -Name 'web-ui' -RelativeScript 'test/web_ui_regression.cjs'
	[IO.File]::WriteAllText($manifest, '', $utf8NoBom)
	foreach ($board in $Environments) {
		foreach ($mode in $Modes) {
			$label = "${board}_mode${mode}"
			$log = Join-Path $runRoot "$label.log"
			$binary = Join-Path $runRoot "$label.bin"
			$preservedFlags = Remove-InheritedInterfaceMode -Flags $oldFlags
			$env:PLATFORMIO_BUILD_FLAGS = if ([string]::IsNullOrWhiteSpace($preservedFlags)) { "-DBLE_INTERFACE_MODE=$mode" } else { "$preservedFlags -DBLE_INTERFACE_MODE=$mode" }
			$buildOutput = @(& $pioExe run --project-dir $projectRoot -e $board --verbose 2>&1 | ForEach-Object { [string]$_ })
			$buildExitCode = $LASTEXITCODE
			Write-SafeBuildLog -Path $log -Lines $buildOutput
			if ($buildExitCode -ne 0) {
				Add-JsonLine $manifest ([ordered]@{
					runId = $runId; label = $label; environment = $board; mode = $mode; result = 'Failed'
					effectiveModeDefine = "-DBLE_INTERFACE_MODE=$mode"; exitCode = $buildExitCode
					sourceSnapshot = $sourceSnapshotRelative; sourceSnapshotSha256 = $sourceSnapshotHash
					log = ".pio/descriptor-fixture/$runId/$label.log"
				})
				throw "Build $label failed; see $log (do not publish credential-bearing diagnostics)."
			}
			$compilerModeEvidence = Get-CompilerModeEvidence -Lines $buildOutput -Mode $mode -Label $label -Board $board
			$buildRoot = Join-Path $projectRoot ".pio\build\$board"
			$buildBinary = Join-Path $buildRoot 'firmware.bin'
			$buildIdentityStampPath = Join-Path $buildRoot 'firmware.bin.identity.json'
			if (-not (Test-Path -LiteralPath $buildIdentityStampPath -PathType Leaf)) { throw "$label firmware identity stamp is missing." }
			$buildIdentityStamp = Get-Content -LiteralPath $buildIdentityStampPath -Raw | ConvertFrom-Json
			$buildBinaryHash = (Get-FileHash -LiteralPath $buildBinary -Algorithm SHA256).Hash.ToLowerInvariant()
			if ($buildIdentityStamp.revision -ne $identityPreparation.revision -or
				$buildIdentityStamp.sourceId -ne $identityPreparation.sourceId -or
				$buildIdentityStamp.environment -ne $board -or
				$buildIdentityStamp.firmwareSha256 -ne $buildBinaryHash) {
				throw "$label firmware image identity does not match the prepared source snapshot."
			}
			Copy-Item -LiteralPath $buildBinary -Destination $binary
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
				effectiveCompilerArguments = $compilerModeEvidence
				command = "pio run --project-dir . -e $board --verbose (mode verified from source compiler commands or pinned SCons envdump)"
				sourceSnapshot = $sourceSnapshotRelative; sourceSnapshotSha256 = $sourceSnapshotHash
				runSummary = $runSummaryRelative
				binary = ".pio/descriptor-fixture/$runId/$label.bin"; bytes = $binarySize
				sha256 = (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash.ToLowerInvariant()
				firmwareIdentity = [ordered]@{
					revision = $buildIdentityStamp.revision; sourceId = $buildIdentityStamp.sourceId
					environment = $buildIdentityStamp.environment; firmwareSha256 = $buildIdentityStamp.firmwareSha256
				}
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
			firmwareIdentityPreparation = $identityPreparation
			nativeChecks = @($nativeChecks)
		})
	}
}
if (-not $sourceUnchanged) { throw "Source inputs changed during this run; review $runSummaryRelative." }
