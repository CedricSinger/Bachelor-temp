const POI_COLORS = {
    amenity:    '#3b82f6',
    shop:       '#f97316',
    tourism:    '#8b5cf6',
    leisure:    '#10b981',
    healthcare: '#ef4444',
    historic:   '#92400e',
    office:     '#6b7280',
    craft:      '#d97706',
};

let poiLayer = null;

// Bounding Box berechnen
function radiusToBbox(center, radiusM) {
    const R   = 6371000;
    const dlat = (radiusM / R) * (180 / Math.PI);
    const dlng = dlat / Math.cos(center.lat * Math.PI / 180);
    return {
        latMin: center.lat - dlat,
        lngMin: center.lng - dlng,
        latMax: center.lat + dlat,
        lngMax: center.lng + dlng,
    };
}

async function loadPOIs() {
    const category = document.getElementById('poi-category').value;
    const type     = document.getElementById('poi-type').value.trim();
    const radius   = parseFloat(document.getElementById('poi-radius').value) || 1000;
    const centerSel = document.getElementById('poi-center').value;
    const status   = document.getElementById('poi-status');

    // Mittelpunkt bestimmen
    let center;
    if (centerSel === 'start')     center = getStartCoords();
    else if (centerSel === 'end')  center = getEndCoords();
    else                           center = map.getCenter();

    if (!center) {
        status.textContent = 'Kein Zentrum verfügbar.';
        return;
    }

    status.textContent = 'Lade POIs…';

    // Marker laden
    const { latMin, lngMin, latMax, lngMax } = radiusToBbox(center, radius);
    let url = `/pois?bbox=${latMin},${lngMin},${latMax},${lngMax}`;
    if (category) url += `&category=${encodeURIComponent(category)}`;
    if (type)     url += `&type=${encodeURIComponent(type)}`;

    try {
        const res  = await fetch(url);
        const data = await res.json();

        if (poiLayer) { map.removeLayer(poiLayer); poiLayer = null; }

        poiLayer = L.geoJSON(data, {
            pointToLayer(feature, latlng) {
                const color = POI_COLORS[feature.properties.category] ?? '#6b7280';
                return L.circleMarker(latlng, {
                    radius:      10,
                    fillColor:   '#facc15',
                    color:       'white',
                    weight:      2,
                    fillOpacity: 0.9,
                });
            },
            onEachFeature(feature, layer) {
                const p = feature.properties;
                const label = p.name || p.type;
                layer.bindPopup(
                    `<b>${label}</b><br><span style="color:#6b7280">${p.category} · ${p.type}</span>`
                );
            }
        }).addTo(map);

        const count = data.features?.length ?? 0;
        status.textContent = `${count} POI${count !== 1 ? 's' : ''} geladen`;

    } catch (err) {
        console.error(err);
        status.textContent = 'Fehler beim Laden der POIs.';
    }
}

// Marker entfernen
function clearPOIs() {
    if (poiLayer) { map.removeLayer(poiLayer); poiLayer = null; }
    document.getElementById('poi-status').textContent = '';
}

document.getElementById('btn-load-pois').addEventListener('click', loadPOIs);
document.getElementById('btn-clear-pois').addEventListener('click', clearPOIs);
