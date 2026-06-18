// Karte
const map = L.map('map', { zoomControl: false }).setView([48.74595, 9.10535], 14);

L.control.zoom({ position: 'topright' }).addTo(map);

L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
    attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a>',
    maxZoom: 19
}).addTo(map);


// Marker-Icons
function createIcon(color) {
    return L.divIcon({
        className: '',
        html: `<div style="
            width:22px;height:22px;
            background:${color};
            border:3px solid white;
            border-radius:50%;
            box-shadow:0 2px 6px rgba(0,0,0,0.35);
        "></div>`,
        iconSize: [22, 22],
        iconAnchor: [11, 11]
    });
}

const startIcon = createIcon('#22c55e');
const endIcon   = createIcon('#ef4444');

let startMarker = null;
let endMarker   = null;
let routeLayer  = null;

// Placing Modus (null/start/end)
let placingMode = null;

function setPlacingMode(mode) {
    placingMode = mode;
    const mapEl = document.getElementById('map');
    mapEl.classList.toggle('placing-mode', mode !== null);
    document.getElementById('btn-place-start').classList.toggle('active', mode === 'start');
    document.getElementById('btn-place-end').classList.toggle('active', mode === 'end');
    const benchBtn = document.getElementById('btn-place-bench');
    if (benchBtn) benchBtn.classList.toggle('active', mode === 'bench');
}

function setStartMarker(latlng) {
    if (startMarker) map.removeLayer(startMarker);
    startMarker = L.marker(latlng, { icon: startIcon, draggable: true }).addTo(map);
    startMarker.on('drag', () => { clearRoute(); updateInputs(); });
    updateInputs();
}

function setEndMarker(latlng) {
    if (endMarker) map.removeLayer(endMarker);
    endMarker = L.marker(latlng, { icon: endIcon, draggable: true }).addTo(map);
    endMarker.on('drag', () => { clearRoute(); updateInputs(); });
    updateInputs();
}

function getStartCoords() { return startMarker ? startMarker.getLatLng() : null; }
function getEndCoords()   { return endMarker   ? endMarker.getLatLng()   : null; }


// Eingabefelder füllen wenn Marker gesetzt wird
function updateInputs() {
    const btnRoute = document.getElementById('btn-route');

    if (startMarker) {
        const ll = startMarker.getLatLng();
        document.getElementById('input-start').value = `${ll.lat.toFixed(5)}, ${ll.lng.toFixed(5)}`;
    }
    if (endMarker) {
        const ll = endMarker.getLatLng();
        document.getElementById('input-end').value = `${ll.lat.toFixed(5)}, ${ll.lng.toFixed(5)}`;
    }

    btnRoute.disabled = !(startMarker && endMarker);
}

function showRouteInfo(distKm) {
    const info = document.getElementById('route-info');
    info.classList.remove('hidden');
    document.getElementById('distance-value').textContent = distKm < 1
        ? `${Math.round(distKm * 1000)} m`
        : `${distKm.toFixed(2)} km`;
}

function hideRouteInfo() {
    document.getElementById('route-info').classList.add('hidden');
}

function clearRoute() {
    if (routeLayer) { map.removeLayer(routeLayer); routeLayer = null; }
    hideRouteInfo();
}


// Benchmark-Punkt + Radius-Kreis
let benchMarker = null;
let benchCircle = null;
const benchIcon = createIcon('#eab308');

function setBenchMarker(latlng) {
    if (benchMarker) map.removeLayer(benchMarker);
    benchMarker = L.marker(latlng, { icon: benchIcon, draggable: true }).addTo(map);
    benchMarker.on('drag', updateBenchCircle);
    updateBenchCircle();
}

function getBenchCoords() { return benchMarker ? benchMarker.getLatLng() : null; }

function updateBenchCircle() {
    if (!benchMarker) return;
    const radius = parseFloat(document.getElementById('bench-radius').value) || 1000;
    if (benchCircle) map.removeLayer(benchCircle);
    benchCircle = L.circle(benchMarker.getLatLng(), {
        radius:      radius,
        color:       '#eab308',
        weight:      2,
        fillColor:   '#eab308',
        fillOpacity: 0.1,
    }).addTo(map);
}

function clearBench() {
    if (benchMarker) { map.removeLayer(benchMarker); benchMarker = null; }
    if (benchCircle) { map.removeLayer(benchCircle); benchCircle = null; }
}


// Klicken auf Karte
map.on('click', function(e) {
    if (placingMode === 'start') {
        setStartMarker(e.latlng);
        setPlacingMode(null);
    } else if (placingMode === 'end') {
        setEndMarker(e.latlng);
        setPlacingMode(null);
    } else if (placingMode === 'bench') {
        setBenchMarker(e.latlng);
        setPlacingMode(null);
    }
});


// Marker Buttons
document.getElementById('btn-place-start').addEventListener('click', () => {
    setPlacingMode(placingMode === 'start' ? null : 'start');
});
document.getElementById('btn-place-end').addEventListener('click', () => {
    setPlacingMode(placingMode === 'end' ? null : 'end');
});

// Benchmark-Punkt setzen + Radius live aktualisieren
const benchPlaceBtn = document.getElementById('btn-place-bench');
if (benchPlaceBtn) benchPlaceBtn.addEventListener('click', () => {
    setPlacingMode(placingMode === 'bench' ? null : 'bench');
});
const benchRadiusInput = document.getElementById('bench-radius');
if (benchRadiusInput) benchRadiusInput.addEventListener('input', updateBenchCircle);

// Escape bricht ab
document.addEventListener('keydown', e => {
    if (e.key === 'Escape') setPlacingMode(null);
});


// Zurücksetzen
document.getElementById('btn-clear').addEventListener('click', function() {
    if (startMarker) { map.removeLayer(startMarker); startMarker = null; }
    if (endMarker)   { map.removeLayer(endMarker);   endMarker   = null; }
    document.getElementById('input-start').value = '';
    document.getElementById('input-end').value   = '';
    setPlacingMode(null);
    clearRoute();
    document.getElementById('btn-route').disabled = true;
});
