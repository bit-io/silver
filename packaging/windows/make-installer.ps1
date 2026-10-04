# Buduje przenośne .zip i (jeśli jest Inno Setup) instalator .exe dla Windows.
#   pwsh packaging/windows/make-installer.ps1 -Name app -Bin cache\build\release\app.exe -Version 0.2.0
# Opcjonalnie: $env:SIGN_PFX + $env:SIGN_PASSWORD → podpisanie Authenticode (signtool).
# Wymaga SDL2.dll, SDL2_ttf.dll, SDL2_image.dll obok binarki (lub w $env:SDL2_BIN).
param(
  [Parameter(Mandatory=$true)][string]$Name,
  [Parameter(Mandatory=$true)][string]$Bin,
  [string]$Version = "0.2.0",
  [string]$Frontend = "frontend"
)
$ErrorActionPreference = "Stop"
$out = "cache\bundle\$Name"
Remove-Item -Recurse -Force $out -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $out | Out-Null
Copy-Item $Bin "$out\$Name.exe"
if (Test-Path $Frontend) {
  Copy-Item -Recurse $Frontend "$out\frontend"
  Remove-Item -Recurse -Force "$out\frontend\node_modules","$out\frontend\src" -ErrorAction SilentlyContinue
}
$sdl = if ($env:SDL2_BIN) { $env:SDL2_BIN } else { Split-Path $Bin }
foreach ($dll in "SDL2.dll","SDL2_ttf.dll","SDL2_image.dll") {
  if (Test-Path "$sdl\$dll") { Copy-Item "$sdl\$dll" $out } else { Write-Warning "brak $dll w $sdl" }
}
if ($env:SIGN_PFX) {
  & signtool sign /f $env:SIGN_PFX /p $env:SIGN_PASSWORD /tr http://timestamp.digicert.com /td sha256 /fd sha256 "$out\$Name.exe"
}
Compress-Archive -Force -Path "$out\*" -DestinationPath "cache\bundle\$Name-$Version-win64.zip"
Write-Host "[bundle] gotowe: cache\bundle\$Name-$Version-win64.zip"

$iscc = Get-Command iscc -ErrorAction SilentlyContinue
if ($iscc) {
  $iss = @"
[Setup]
AppName=$Name
AppVersion=$Version
DefaultDirName={autopf}\$Name
DefaultGroupName=$Name
OutputDir=cache\bundle
OutputBaseFilename=$Name-$Version-setup
Compression=lzma2
ArchitecturesInstallIn64BitMode=x64compatible
[Files]
Source: "$out\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion
[Icons]
Name: "{group}\$Name"; Filename: "{app}\$Name.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\$Name"; Filename: "{app}\$Name.exe"; WorkingDir: "{app}"
"@
  Set-Content -Path "cache\bundle\$Name.iss" -Value $iss
  & iscc "cache\bundle\$Name.iss"
  if ($env:SIGN_PFX) { & signtool sign /f $env:SIGN_PFX /p $env:SIGN_PASSWORD /tr http://timestamp.digicert.com /td sha256 /fd sha256 "cache\bundle\$Name-$Version-setup.exe" }
  Write-Host "[bundle] gotowe: cache\bundle\$Name-$Version-setup.exe"
} else {
  Write-Host "[bundle] (pomijam instalator: brak Inno Setup — iscc)"
}
