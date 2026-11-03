$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$dependency = Join-Path $projectRoot 'third_party/snes9x'
$upstream = Join-Path $dependency 'upstream'
if (!(Test-Path -LiteralPath $upstream)) {
    $archive = Join-Path $dependency 'source.zip'
    if ((Get-FileHash -LiteralPath $archive).Hash -ne '7F6DEE7AB1A367DA86BC86A87A204BF8FE0BE389D62E7CFBD568DC2CCAC32055') { throw 'Snes9xソースのハッシュが一致しません。' }
    Expand-Archive -LiteralPath $archive -DestinationPath $dependency
    Move-Item -LiteralPath (Join-Path $dependency 'snes9x-fae2fea08f74180759ef540ee94259213f503480') -Destination $upstream
}
$makefile = Get-Content -LiteralPath (Join-Path $upstream 'libretro/Makefile.common') -Raw
$sources = [regex]::Matches($makefile, '\$\((CORE_DIR|LIBRETRO_COMM_DIR)\)/([\w/]+\.(?:cpp|c))') | ForEach-Object {
    $prefix = if ($_.Groups[1].Value -eq 'CORE_DIR') { 'upstream/' } else { 'upstream/libretro/libretro-common/' }
    $prefix + $_.Groups[2].Value
} | Select-Object -Unique
$items = ($sources | ForEach-Object {
    $compileAs = ''
    '<ClCompile Include="' + $_ + '"><ObjectFileName>$(IntDir)' + ($_ -replace '[/\\.]','_') + '.obj</ObjectFileName>' + $compileAs + '</ClCompile>'
}) -join "`n"
$apuPath = Join-Path $upstream 'apu/apu.cpp'
$apu = [IO.File]::ReadAllText($apuPath)
if (!$apu.Contains('wiizv_audio.inc')) {
    $apu = $apu.Replace('static inline int S9xAPUGetClock(int32);', "#include `"../../wiizv_audio.inc`"`n`nstatic inline int S9xAPUGetClock(int32);")
    $apu = $apu.Replace('spc::ratio_numerator * (cpucycles - spc::reference_time)', 'static_cast<uint64>(spc::ratio_numerator) * wiizvSpeedDenominator * (cpucycles - spc::reference_time)')
    $apu = $apu.Replace('           spc::ratio_denominator;', '           (static_cast<uint64>(spc::ratio_denominator) * wiizvSpeedNumerator);')
    if (!$apu.Contains('wiizvSpeedNumerator);')) { throw 'APU変更の適用に失敗しました。' }
    [IO.File]::WriteAllText($apuPath, $apu, [Text.UTF8Encoding]::new($false))
}
$cpuPath = Join-Path $upstream 'cpuexec.cpp'
$cpu = [IO.File]::ReadAllText($cpuPath)
if (!$cpu.Contains('wiizv_movement.inc')) {
    $cpu = $cpu.Replace('static inline void S9xReschedule (void);', "#include `"../wiizv_movement.inc`"`n`nstatic inline void S9xReschedule (void);")
    $cpu = $cpu.Replace("`t`tuint8`t`t`t`tOp;", "`t`twiizvMovementHook();`n`n`t`tuint8`t`t`t`tOp;")
    if (!$cpu.Contains('wiizvMovementHook();')) { throw '移動フックの適用に失敗しました。' }
    [IO.File]::WriteAllText($cpuPath, $cpu, [Text.UTF8Encoding]::new($false))
}
if (!$cpu.Contains('wiizv_effects.inc')) {
    $cpu = $cpu.Replace('#include "../wiizv_movement.inc"', "#include `"../wiizv_movement.inc`"`n#include `"../wiizv_effects.inc`"")
    $cpu = $cpu.Replace('wiizvMovementHook();', "wiizvMovementHook();`n`t`twiizvEffectsHook();")
    if (!$cpu.Contains('wiizvEffectsHook();')) { throw '常時効果フックの適用に失敗しました。' }
    [IO.File]::WriteAllText($cpuPath, $cpu, [Text.UTF8Encoding]::new($false))
}
if (!$cpu.Contains('wiizv_audio_cpu.inc')) {
    $cpu = $cpu.Replace('#include "../wiizv_effects.inc"', "#include `"../wiizv_effects.inc`"`n#include `"../wiizv_audio_cpu.inc`"")
    $cpu = $cpu.Replace('wiizvEffectsHook();', "wiizvEffectsHook();`n`t`twiizvAudioTransferHook();")
    if (!$cpu.Contains('wiizvAudioTransferHook();')) { throw '音声転送フックの適用に失敗しました。' }
    [IO.File]::WriteAllText($cpuPath, $cpu, [Text.UTF8Encoding]::new($false))
}
if (!$cpu.Contains('wiizv_character.inc')) {
    $cpu = $cpu.Replace('#include "../wiizv_effects.inc"', "#include `"../wiizv_effects.inc`"`n#include `"../wiizv_character.inc`"")
    $cpu = $cpu.Replace('wiizvEffectsHook();', "wiizvEffectsHook();`n`t`twiizvCharacterHook();")
    if (!$cpu.Contains('wiizvCharacterHook();')) { throw 'キャラクター編集フックの適用に失敗しました。' }
    [IO.File]::WriteAllText($cpuPath, $cpu, [Text.UTF8Encoding]::new($false))
}
$definition = [IO.File]::ReadAllText((Join-Path $upstream 'libretro/libretro.def')).Replace('LIBRARY      libretro', 'LIBRARY      wiizv_snes9x')
$definitionPath = Join-Path $dependency 'WiizVCore.def'
if (!(Test-Path -LiteralPath $definitionPath) -or [IO.File]::ReadAllText($definitionPath) -ne $definition) {
    [IO.File]::WriteAllText($definitionPath, $definition, [Text.UTF8Encoding]::new($false))
}
$project = @'
<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
 <ItemGroup Label="ProjectConfigurations">
  <ProjectConfiguration Include="Debug|x64"><Configuration>Debug</Configuration><Platform>x64</Platform></ProjectConfiguration>
  <ProjectConfiguration Include="Release|x64"><Configuration>Release</Configuration><Platform>x64</Platform></ProjectConfiguration>
 </ItemGroup>
 <PropertyGroup Label="Globals"><ProjectGuid>{DB353010-8837-45A8-AECF-9FEE2328FEDE}</ProjectGuid><WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion></PropertyGroup>
 <Import Project="$(VCTargetsPath)\Microsoft.Cpp.Default.props" />
 <PropertyGroup Label="Configuration"><ConfigurationType>DynamicLibrary</ConfigurationType><PlatformToolset>v145</PlatformToolset><UseDebugLibraries>false</UseDebugLibraries></PropertyGroup>
 <Import Project="$(VCTargetsPath)\Microsoft.Cpp.props" />
 <PropertyGroup Label="UserMacros" />
 <PropertyGroup><PreferredToolArchitecture>x64</PreferredToolArchitecture><TargetName>wiizv_snes9x</TargetName></PropertyGroup>
 <ItemDefinitionGroup>
  <ClCompile><WarningLevel>Level1</WarningLevel><Optimization>MaxSpeed</Optimization><MultiProcessorCompilation>true</MultiProcessorCompilation><LanguageStandard>stdcpp17</LanguageStandard><PreprocessorDefinitions>__LIBRETRO__;ALLOW_CPU_OVERCLOCK;__WIN32__;_CRT_SECURE_NO_WARNINGS;_CRT_NONSTDC_NO_DEPRECATE;NDEBUG;__inline__=__inline;%(PreprocessorDefinitions)</PreprocessorDefinitions><AdditionalIncludeDirectories>upstream;upstream/libretro;upstream/libretro/libretro-common/include;upstream/apu;upstream/apu/bapu;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories><ForcedIncludeFiles>$(ProjectDir)wiizv_linkage.h;%(ForcedIncludeFiles)</ForcedIncludeFiles><ExceptionHandling>Sync</ExceptionHandling><AdditionalOptions>/utf-8 %(AdditionalOptions)</AdditionalOptions></ClCompile>
  <Link><ModuleDefinitionFile>WiizVCore.def</ModuleDefinitionFile><AdditionalDependencies>crypt32.lib;winmm.lib;ws2_32.lib;%(AdditionalDependencies)</AdditionalDependencies></Link>
  <PostBuildEvent><Command>copy /Y "$(TargetPath)" "$(ProjectDir)wiizv_snes9x.dll" || exit /b 1</Command></PostBuildEvent>
 </ItemDefinitionGroup>
 <ItemGroup>
ITEMS
 </ItemGroup>
 <Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" />
</Project>
'@
$projectPath = Join-Path $dependency 'WiizVCore.vcxproj'
$projectText = $project.Replace('ITEMS', $items)
if (!(Test-Path -LiteralPath $projectPath) -or [IO.File]::ReadAllText($projectPath) -ne $projectText) {
    [IO.File]::WriteAllText($projectPath, $projectText, [Text.UTF8Encoding]::new($false))
}
