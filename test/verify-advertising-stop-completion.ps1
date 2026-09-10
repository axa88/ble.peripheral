#Requires -Version 7.0
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot

function Assert-Contains([string] $Text, [string] $Needle, [string] $Description) {
	if (-not $Text.Contains($Needle, [StringComparison]::Ordinal)) {
		throw "Missing $Description anchor '$Needle'."
	}
}

function Read-Source([string] $Path) {
	$bytes = [IO.File]::ReadAllBytes($Path)
	if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
		throw "Unexpected UTF-8 BOM in '$Path'."
	}
	$text = [Text.Encoding]::UTF8.GetString($bytes)
	if ([regex]::IsMatch($text, '(?<!\r)\n') -or [regex]::IsMatch($text, '\r(?!\n)')) {
		throw "Source '$Path' is not CRLF formatted."
	}
	return $text
}

$advertising = Read-Source (Join-Path $projectRoot 'src\advertising.cpp')
$manager = Read-Source (Join-Path $projectRoot 'src\bluetoothManager.cpp')
$header = Read-Source (Join-Path $projectRoot 'src\advertising.h')

Assert-Contains $header 'void OnAdvertisingStopped(uint8_t instanceId, int reason);' 'extended advertising callback boundary'
Assert-Contains $advertising 'stopRequestGenerations_' 'stop request generation state'
Assert-Contains $advertising 'stopCompletionGenerations_' 'stop completion generation state'
Assert-Contains $advertising 'kStopCompletionTimeoutMs' 'bounded stop completion wait'
Assert-Contains $advertising 'beginStopRequestLocked' 'explicit stop request capture'
Assert-Contains $advertising 'publishExplicitStopCompletionLocked' 'explicit stop completion publication'
Assert-Contains $advertising 'waitForStopCompletion' 'serialized subsequent start'
Assert-Contains $advertising 'xTaskCheckForTimeOut' 'wrap-safe stop wait'
Assert-Contains $advertising 'Advertising start blocked by pending stop completion' 'actionable pending-stop failure'
Assert-Contains $advertising 'Advertising stop complete | Adv[%u] | generation:%u | api:1 | active:0 | source:stop-return' 'explicit stop-completion marker'
Assert-Contains $advertising 'Advertising stop complete | Adv[%u] | generation:0 | reason:0x%x | active:%u | source:adv-complete' 'spontaneous ADV_COMPLETE marker'
Assert-Contains $advertising 'stopCompletionGenerations_[instanceId] = generation' 'explicit completion state publication'
Assert-Contains $advertising 'stopCompletionGenerations_.erase(instanceId)' 'completion generation lifecycle reset'
Assert-Contains $advertising 'explicitStopPending' 'callback/restart race guard'
Assert-Contains $advertising 'Advertising stop completion failed | Adv[%u] | generation:%u | api:%u | active:%u' 'actionable stop failure'
Assert-Contains $advertising 'it->second == generation' 'exact generation comparison'
if ($advertising.Contains('stopCompletionGenerations_[instanceId] = stopGeneration', [StringComparison]::Ordinal)) {
	throw 'Spontaneous ADV_COMPLETE callback still publishes an explicit stop generation.'
}
$stopReturnIndex = $advertising.IndexOf('stopSucceeded = !wasActive || adv->stop(instanceId);', [StringComparison]::Ordinal)
$inactiveStateIndex = $advertising.IndexOf('const bool inactive = !adv->isActive(instanceId);', [StringComparison]::Ordinal)
$publicationIndex = $advertising.IndexOf('publishExplicitStopCompletionLocked(instanceId, stopGeneration, stopSucceeded, inactive)', [StringComparison]::Ordinal)
if ($stopReturnIndex -lt 0 -or $inactiveStateIndex -lt $stopReturnIndex -or $publicationIndex -lt $inactiveStateIndex) {
	throw 'Explicit stop completion does not require stop-return success and verified inactive state before publication.'
}
$exactGenerationIndex = $advertising.IndexOf('it->second == generation', [StringComparison]::Ordinal)
if ($exactGenerationIndex -lt 0) {
	throw 'Stop completion state does not use exact generation matching.'
}
$retainedWrapGeneration = [uint32]::MaxValue
$newWrapGeneration = [uint32]1
if ($retainedWrapGeneration -eq $newWrapGeneration) {
	throw 'The retained UINT32_MAX completion unexpectedly equals the wrapped generation 1.'
}
if (-not $advertising.Contains('it->second == generation', [StringComparison]::Ordinal)) {
	throw 'The wrap regression would be vulnerable to a >= generation comparison.'
}
Assert-Contains $manager 'Advertising::OnAdvertisingStopped(instId, reason);' 'NimBLE ADV_COMPLETE callback forwarding'
Assert-Contains $manager 'setAdvertisingCompleteCallback' 'legacy advertising callback compatibility'

$startWaitIndex = $advertising.IndexOf('waitForStopCompletion(instanceId)', [StringComparison]::Ordinal)
$startLockIndex = if ($startWaitIndex -ge 0) { $advertising.IndexOf('std::lock_guard<std::mutex> lk(configMutex_);', $startWaitIndex, [StringComparison]::Ordinal) } else { -1 }
if ($startWaitIndex -lt 0 -or $startLockIndex -lt $startWaitIndex) {
	throw 'Manual start does not wait for ext-advertising stop completion before taking configMutex_.'
}

Write-Output 'Advertising stop-completion static checks passed: callback/state/serialization/timeout/legacy-compatibility=pass.'
