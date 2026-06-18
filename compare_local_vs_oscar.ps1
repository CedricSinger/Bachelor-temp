# Vergleich: lokaler Grid- + R-Tree-Index vs. OSCAR (remote), 50 km Radius,
# je Zone unterschiedlicher POI-Dichte.
#
# 1) baut/startet bench_pois auf Deutschland          -> bench_local.json
# 2) misst OSCAR apxstats (amenity=restaurant) je Zone, gemittelt
# 3) erzeugt einen eigenstaendigen HTML-Report        -> comparison_report.html
#
# Aufruf:
#   .\compare_local_vs_oscar.ps1                 # voller Lauf, Zonen Essen + Wuerzburg
#   .\compare_local_vs_oscar.ps1 -SkipLocal      # nur OSCAR + Report neu
#   .\compare_local_vs_oscar.ps1 -Zones @(@{Name='Koeln';Lat=50.94;Lng=6.96}, ...)

param(
    [switch]$SkipLocal,
    [array]$Zones = @(
        @{ Name = 'Essen';     Lat = 51.4556; Lng = 7.0116 },   # dicht (Ruhrgebiet)
        @{ Name = 'Wuerzburg'; Lat = 49.7913; Lng = 9.9534 }    # duenn (Franken)
    ),
    [int]$OscarRuns = 5,
    # Nur falls cmake nicht im PATH ist (Windows/MSYS2). Auf Linux/macOS ignoriert.
    [string]$MingwBin = "C:\msys64\mingw64\bin"
)

$ErrorActionPreference = "Stop"
$IC        = [System.Globalization.CultureInfo]::InvariantCulture   # Punkt-Dezimaltrenner erzwingen
$Root      = $PSScriptRoot
$IsWin     = ($env:OS -eq "Windows_NT")
$ExeName   = if ($IsWin) { "bench_pois.exe" } else { "bench_pois" }
$LocalJson = Join-Path $Root "bench_local.json"
$Pbf       = Join-Path $Root "data/germany-latest.osm.pbf"
$Exe       = Join-Path $Root "build/backend/$ExeName"
$Report    = Join-Path $Root "comparison_report.html"
$ua        = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/124 Safari/537.36"
$hdr       = @{ "User-Agent" = $ua; "Referer" = "https://www.oscar-web.de/" }
$OscarBase = "https://routing.oscar-web.de/oscar/cqr/clustered/apxstats"
$Radii     = @(10000, 50000)   # gemessene Radien (muss zu bench_pois passen)

# --- 1) Lokaler Lauf ---------------------------------------------------------
if (-not $SkipLocal) {
    # cmake/ninja nur dann aus MSYS2 nachladen, wenn nicht ohnehin im PATH
    if (-not (Get-Command cmake -ErrorAction SilentlyContinue) -and (Test-Path $MingwBin)) {
        $env:PATH = $MingwBin + [IO.Path]::PathSeparator + $env:PATH
    }
    Write-Host "[1/3] Baue bench_pois ..." -ForegroundColor Cyan
    cmake --build (Join-Path $Root "build") --target bench_pois | Out-Null
    $zoneArgs = $Zones | ForEach-Object { $_.Name + "," + ([double]$_.Lat).ToString($IC) + "," + ([double]$_.Lng).ToString($IC) }
    Write-Host "[1/3] Lokaler Messlauf auf Deutschland (~1 min, ~5 GB RAM) ..." -ForegroundColor Cyan
    & $Exe $Pbf $LocalJson @zoneArgs
}
$local = Get-Content $LocalJson -Raw | ConvertFrom-Json

# --- 2) OSCAR-Messung je Zone ------------------------------------------------
Write-Host "[2/3] Messe OSCAR (remote) ..." -ForegroundColor Cyan
function Measure-Oscar($lat, $lng, $radius, $runs) {
    $q = "@amenity:restaurant `$point:$radius," + ([double]$lat).ToString($IC) + "," + ([double]$lng).ToString($IC)
    $u = "$OscarBase`?q=" + [uri]::EscapeDataString($q)
    $times = @(); $items = 0
    for ($i = 0; $i -lt $runs; $i++) {
        try {
            $t = Measure-Command { $r = Invoke-WebRequest $u -UseBasicParsing -Headers $hdr -TimeoutSec 60 }
            $times += $t.TotalMilliseconds
            $items = ($r.Content | ConvertFrom-Json).items
        } catch { Write-Host "  OSCAR-Fehler: $($_.Exception.Message)" -ForegroundColor Yellow }
        Start-Sleep -Milliseconds 800
    }
    if ($times.Count -eq 0) { return $null }
    $m = $times | Measure-Object -Average -Minimum -Maximum
    [PSCustomObject]@{ avg_ms = [math]::Round($m.Average,1); min_ms = [math]::Round($m.Minimum,1); max_ms = [math]::Round($m.Maximum,1); items = $items }
}
$oscar = @{}   # Schluessel: "ZonenName|radius_m"
foreach ($z in $local.zones) {
    foreach ($rm in $Radii) { $oscar["$($z.name)|$rm"] = (Measure-Oscar $z.lat $z.lng $rm $OscarRuns) }
}

# --- 3) HTML-Report ----------------------------------------------------------
Write-Host "[3/3] Erzeuge $Report ..." -ForegroundColor Cyan

function Row($place, $name, $avg, $min, $max, $found, $bold) {
    $a = if ($bold) { "<strong>{0:N1}</strong>" -f $avg } else { "{0:N1}" -f $avg }
    "<tr><td>$place</td><td>$name</td><td class='num'>$a</td><td class='num'>{0:N1}</td><td class='num'>{1:N1}</td><td class='num'>{2:N0}</td></tr>" -f $min, $max, $found
}

$sections = ""
foreach ($z in $local.zones) {
    $rows = ""
    foreach ($case in $z.cases) {
        $g = $case.results.grid; $r = $case.results.rtree
        $oz = if ($case.type -eq "restaurant") { $oscar["$($z.name)|$([int]$case.radius_m)"] } else { $null }
        $avgs = @($g.avg_ms, $r.avg_ms); if ($oz) { $avgs += $oz.avg_ms }
        $fast = ($avgs | Measure-Object -Minimum).Minimum
        $rows += "<tr class='grp'><td colspan='6'>$($case.name)</td></tr>"
        $rows += Row "lokal" "Grid"   $g.avg_ms $g.min_ms $g.max_ms $g.found ($g.avg_ms -eq $fast)
        $rows += Row "lokal" "R-Tree" $r.avg_ms $r.min_ms $r.max_ms $r.found ($r.avg_ms -eq $fast)
        if ($oz) { $rows += Row "remote" "OSCAR (Netzwerk)" $oz.avg_ms $oz.min_ms $oz.max_ms $oz.items ($oz.avg_ms -eq $fast) }
    }
    # Dichte-Kontext: 50-km-Box dieser Zone
    $bbox50 = $z.cases | Where-Object { $_.type -eq "" -and [int]$_.radius_m -eq 50000 }
    $rest50 = $z.cases | Where-Object { $_.type -eq "restaurant" -and [int]$_.radius_m -eq 50000 }
    $dichte = "{0:N0} POIs in 50 km, davon {1:N0} Restaurants" -f $bbox50.results.grid.found, $rest50.results.grid.found

    $sections += @"
<h2>$($z.name) <span class="coord">($($z.lat), $($z.lng))</span></h2>
<p class="dichte">$dichte</p>
<table>
 <thead><tr><th>Ort</th><th>Index / Dienst</th><th class="num">avg (ms)</th><th class="num">min (ms)</th><th class="num">max (ms)</th><th class="num">Treffer</th></tr></thead>
 <tbody>$rows</tbody>
</table>
"@
}

$gen = Get-Date -Format "yyyy-MM-dd HH:mm"
$html = @"
<!DOCTYPE html><html lang="de"><head><meta charset="UTF-8">
<title>POI-Abfrage: Grid vs. R-Tree vs. OSCAR</title>
<style>
 body{font-family:Georgia,'Times New Roman',serif;max-width:920px;margin:48px auto;padding:0 24px;color:#1a1a1a;line-height:1.5}
 h1{font-size:30px;font-weight:600;margin:0 0 6px}
 h2{font-size:23px;font-weight:600;margin:34px 0 4px}
 h2 .coord{font-size:17px;font-weight:400;color:#666}
 p{font-size:19px}
 .meta{font-size:16px;color:#555;margin:0 0 12px}
 .dichte{font-size:17px;color:#444;margin:0 0 10px}
 table{width:100%;border-collapse:collapse;margin:8px 0 8px}
 th,td{padding:11px 14px;font-size:18px;border-bottom:1px solid #ccc;text-align:left}
 th{border-bottom:2px solid #333}
 td.num,th.num{text-align:right;font-variant-numeric:tabular-nums}
 tr.grp td{background:#f0f0f0;font-weight:600;font-size:18px;padding-top:14px}
 .note{font-size:16px;color:#444;margin-top:30px}
 code{font-family:Consolas,monospace;font-size:16px}
</style></head><body>
<h1>POI-Abfrage: Grid vs. R-Tree vs. OSCAR</h1>
<p class="meta">$("{0:N0}" -f $local.pois) getaggte Knoten aus $(Split-Path $local.dataset -Leaf), komplett im RAM
($("{0:N0}" -f $local.peak_ram_mb) MB, $("{0:N0}" -f $local.bytes_per_poi) Byte/POI).
Radien 10 und 50 km, $($local.n_queries) Abfragen je Fall. Erzeugt $gen.</p>
$sections
<p class="note">
Lokal: Mittel aus $($local.n_queries) Abfragen je Fall, Bounding-Box, In-Memory im selben Prozess.
OSCAR: Mittel aus $OscarRuns Abfragen, Endpoint <code>apxstats</code> auf <code>routing.oscar-web.de</code>,
echter Kreisradius (<code>`$point</code>), inklusive Netzwerk-Roundtrip.
Trefferzahlen nicht direkt vergleichbar (lokal Bounding-Box, OSCAR Kreis und naeherungsweise); massgeblich ist die Latenz.
</p>
</body></html>
"@
Set-Content -Path $Report -Value $html -Encoding UTF8
Write-Host "`nFertig. Report: $Report" -ForegroundColor Green
foreach ($z in $local.zones) { $o=$oscar[$z.name]; if($o){ "OSCAR $($z.name): avg $($o.avg_ms) ms ($($o.items) Treffer)" } }
