const ROUTE_STYLE = {
    color: '#2563eb',
    weight: 5,
    opacity: 0.8,
    smoothFactor: 1
};

// Nominatim für Geocoding
async function geocodeAddress(query) {
    const url = `https://nominatim.openstreetmap.org/search?q=${encodeURIComponent(query)}&format=json&limit=1&accept-language=de`;
    const res = await fetch(url);
    const data = await res.json();
    if (!data.length) return null;
    return L.latLng(parseFloat(data[0].lat), parseFloat(data[0].lon));
}

// Koordinaten parsen
function parseCoords(str) {
    const parts = str.split(',').map(s => parseFloat(s.trim()));
    if (parts.length === 2 && !isNaN(parts[0]) && !isNaN(parts[1])) {
        return L.latLng(parts[0], parts[1]);
    }
    return null;
}

async function resolveInput(inputId, setMarkerFn) {
    const val = document.getElementById(inputId).value.trim();
    if (!val) return;

    // Koordinaten direkt lesen
    const coords = parseCoords(val);
    if (coords) {
        setMarkerFn(coords);
        map.setView(coords, map.getZoom());
        return;
    }

    // Sonst Geocoding
    try {
        const latlng = await geocodeAddress(val);
        if (latlng) {
            setMarkerFn(latlng);
            map.setView(latlng, 15);
        } else {
            alert(`Adresse nicht gefunden: "${val}"`);
        }
    } catch {
        alert('Geocoding fehlgeschlagen. Ist eine Internetverbindung vorhanden?');
    }
}


// Route berechnen
async function calculateRoute() {
    const start = getStartCoords();
    const end   = getEndCoords();
    if (!start || !end) return;

    const btn = document.getElementById('btn-route');
    btn.disabled = true;
    btn.textContent = 'Berechne…';

    try {
        const url = `/route?from=${start.lat},${start.lng}&to=${end.lat},${end.lng}`;
        const res  = await fetch(url);
        const data = await res.json();

        if (!data.success) {
            alert('Keine Route gefunden.');
            return;
        }

        clearRoute();
        routeLayer = L.geoJSON(data.route, { style: ROUTE_STYLE }).addTo(map);
        map.fitBounds(routeLayer.getBounds(), { padding: [60, 60] });
        showRouteInfo(data.distance_km);

    } catch (err) {
        console.error(err);
        alert('Fehler bei der Routenberechnung. Ist der Server erreichbar?');
    } finally {
        btn.disabled = !(startMarker && endMarker);
        btn.textContent = 'Route berechnen';
    }
}


// Geocoding
document.getElementById('input-start').addEventListener('keydown', e => {
    if (e.key === 'Enter') resolveInput('input-start', setStartMarker);
});
document.getElementById('input-end').addEventListener('keydown', e => {
    if (e.key === 'Enter') resolveInput('input-end', setEndMarker);
});

document.getElementById('btn-route').addEventListener('click', calculateRoute);
