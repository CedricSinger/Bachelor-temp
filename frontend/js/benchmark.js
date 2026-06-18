const BENCH_PRESETS = {
    urban: { lat: 48.775, lng: 9.182,  zoom: 13 },
    rural: { lat: 48.200, lng: 8.200,  zoom: 12 },
};

// Preset setzt nur den Punkt + Kreis und springt zur Stelle
function benchPreset(preset) {
    const { lat, lng, zoom } = BENCH_PRESETS[preset];
    map.setView([lat, lng], zoom);
    setBenchMarker(L.latLng(lat, lng));
}

async function runBenchmark() {
    const center = getBenchCoords();
    const statusEl  = document.getElementById('bench-status');
    const resultsEl = document.getElementById('bench-results');

    if (!center) {
        statusEl.textContent = 'Bitte erst einen Punkt auf der Karte wählen.';
        return;
    }

    const n        = Math.min(500, Math.max(1, parseInt(document.getElementById('bench-n').value)      || 50));
    const radius   = Math.min(50000, Math.max(100, parseInt(document.getElementById('bench-radius').value) || 1000));
    const key      = document.getElementById('bench-key').value.trim();
    const value    = document.getElementById('bench-value').value.trim();

    statusEl.textContent = 'Teste...';
    resultsEl.innerHTML  = '';

    try {
        const tagParam = (key && value)
            ? `&key=${encodeURIComponent(key)}&value=${encodeURIComponent(value)}` : '';
        const res  = await fetch(
            `/benchmark?lat=${center.lat}&lng=${center.lng}&radius=${radius}&n=${n}${tagParam}`);
        const data = await res.json();

        if (data.error) {
            statusEl.textContent = 'Fehler: ' + data.error;
            return;
        }

        const filterLabel = (key && value) ? `${key}=${value}` : 'alle Tags';
        const totalShown = data.filter_pois > 0 ? data.filter_pois : data.total_pois;
        statusEl.textContent =
            `${data.n_queries} Abfragen @ ` +
            `(${data.center_lat.toFixed(4)}, ${data.center_lng.toFixed(4)}), ` +
            `Radius ${data.radius_m} m, ${filterLabel}` +
            ` · Ø ${data.avg_found.toFixed(1)} von ${totalShown.toLocaleString('de-DE')} POIs gefunden`;

        const order  = ['linear', 'grid', 'rtree'];
        const labels = {
            linear:    'Linear (Brute-Force)',
            grid:      'Grid',
            rtree:     'R-Tree',
        };

        const times = order.map(k => data.timings[k]?.avg_ms ?? Infinity);
        const minAvg = Math.min(...times);

        let html = `<table class="bench-table">
            <thead><tr><th>Index</th><th>avg (ms)</th><th>min (ms)</th><th>max (ms)</th></tr></thead>
            <tbody>`;

        for (let i = 0; i < order.length; i++) {
            const key = order[i];
            const t   = data.timings[key];
            if (!t) continue;
            const fastest = t.avg_ms === minAvg ? ' class="bench-fastest"' : '';
            html += `<tr${fastest}>
                <td>${labels[key]}</td>
                <td>${t.avg_ms.toFixed(3)}</td>
                <td>${t.min_ms.toFixed(3)}</td>
                <td>${t.max_ms.toFixed(3)}</td>
            </tr>`;
        }

        html += '</tbody></table>';
        resultsEl.innerHTML = html;

    } catch (e) {
        statusEl.textContent = 'Fehler: ' + e.message;
    }
}
