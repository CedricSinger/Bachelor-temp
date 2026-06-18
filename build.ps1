$BuildDir = "$PSScriptRoot\build"

cmake --build $BuildDir
if ($LASTEXITCODE -ne 0) {
    Write-Host "Build fehlgeschlagen." -ForegroundColor Red
    exit 1
}

Write-Host "Build erfolgreich." -ForegroundColor Green
