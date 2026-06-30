// Sequenzierte Route (iterative Verdopplung).
// Nutzt die vorhandenen Start-/Ziel-Marker als s und t.

const SEQ_COLORS = ['#2563eb', '#16a34a', '#db2777', '#ea580c', '#7c3aed', '#0891b2', '#ca8a04'];
const SEQ_FACILITY_COLOR = '#f59e0b';

// Beispiel-Kategorien (Anzeigename -> key=value-Tag)
const SEQ_PRESETS = [
    { label: 'Tankstelle',  tag: 'amenity=fuel' },
    { label: 'Bäckerei',    tag: 'shop=bakery' },
    { label: 'Supermarkt',  tag: 'shop=supermarket' },
    { label: 'Geldautomat', tag: 'amenity=atm' },
    { label: 'Bank',        tag: 'amenity=bank' },
    { label: 'Apotheke',    tag: 'amenity=pharmacy' },
    { label: 'Post',        tag: 'amenity=post_office' },
    { label: 'Restaurant',  tag: 'amenity=restaurant' },
];

let seqLayer = null;
let seqExploreLayer = null;        // aktuell angezeigter Baum (eines POI)
let seqExplorationData = null;     // alle Baeume der letzten explore-Anfrage (Features)
let seqExploreEnabled = false;     // Toggle-Zustand "Dijkstra-Segmente anzeigen"
let seqFacilityMarkers = [];
let lastSeqResolved = [];   // {tag,label} der zuletzt gesendeten Sequenz

// Farbe der Route, die von POI i (chosen[i]) ausgeht = Abschnitt i+1.
function seqOutColor(i) {
    return SEQ_COLORS[(i + 1) % SEQ_COLORS.length];
}

function escapeHtml(s) {
    return String(s).replace(/[&<>"']/g, c => (
        { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]
    ));
}

// Tag -> Anzeigename
function labelForTag(tag) {
    const preset = SEQ_PRESETS.find(p => p.tag.toLowerCase() === tag.toLowerCase());
    if (preset) return preset.label;
    const eq = tag.indexOf('=');
    return eq >= 0 ? tag.slice(eq + 1) : tag;
}

// Eingabe -> {tag,label} aufloesen (Anzeigename ODER rohes key=value)
function resolveCategory(val) {
    const v = (val || '').trim();
    if (!v) return null;
    const preset = SEQ_PRESETS.find(p => p.label.toLowerCase() === v.toLowerCase());
    if (preset) return { tag: preset.tag, label: preset.label };
    if (v.includes('=')) return { tag: v, label: labelForTag(v) };
    return null;  // unbekannt
}

// --- Listenverwaltung ---
function renumberSeqRows() {
    document.querySelectorAll('#seq-list .seq-row').forEach((row, i) => {
        row.querySelector('.seq-index').textContent = i + 1;
    });
}

function addSeqRow(value = '') {
    const list = document.getElementById('seq-list');
    const row = document.createElement('div');
    row.className = 'seq-row';
    row.innerHTML = `
        <span class="seq-index"></span>
        <input type="text" class="seq-cat" list="seq-presets"
               placeholder="Kategorie oder key=value" />
        <button class="btn-icon btn-remove" title="Entfernen">×</button>
    `;
    row.querySelector('.seq-cat').value = value;
    row.querySelector('.btn-remove').addEventListener('click', () => {
        row.remove();
        if (document.querySelectorAll('#seq-list .seq-row').length === 0) addSeqRow();
        renumberSeqRows();
    });
    list.appendChild(row);
    renumberSeqRows();
}

function getSeqValues() {
    return Array.from(document.querySelectorAll('#seq-list .seq-cat')).map(i => i.value);
}

function clearSequenced() {
    if (seqLayer) { map.removeLayer(seqLayer); seqLayer = null; }
    if (seqExploreLayer) { map.removeLayer(seqExploreLayer); seqExploreLayer = null; }
    seqFacilityMarkers.forEach(m => map.removeLayer(m));
    seqFacilityMarkers = [];
    seqExplorationData = null;
    document.getElementById('seq-status').textContent = '';
}

// Setzt den optischen Zustand des Toggle-Buttons.
function setExploreButton(active) {
    seqExploreEnabled = active;
    const btn = document.getElementById('btn-sequenced-explore');
    btn.classList.toggle('active', active);
}

// Zeigt nur die Dijkstra-Segmente eines einzelnen POI plus die uebrigen
// Kandidaten-Facilities derselben Kategorie als kleine Marker.
function showPoiSegments(i) {
    if (!seqExploreEnabled || !seqExplorationData) return;
    if (seqExploreLayer) { map.removeLayer(seqExploreLayer); seqExploreLayer = null; }
    const feat = seqExplorationData.find(f => f.properties.poi_index === i);
    if (!feat) return;

    const color = seqOutColor(i);
    const group = L.layerGroup();

    // Suchbaum
    L.geoJSON(feat, { style: { color, weight: 3, opacity: 0.55 } }).addTo(group);

    // Kandidaten derselben Kategorie als kleine Punkte
    (feat.properties.candidates || []).forEach(c => {
        const cm = L.circleMarker([c.coordinates[1], c.coordinates[0]], {
            radius: 4, color: color, weight: 1.5,
            fillColor: '#ffffff', fillOpacity: 1
        });
        if (c.name) cm.bindTooltip(c.name);
        cm.addTo(group);
    });

    group.addTo(map);
    seqExploreLayer = group;
    if (seqLayer) seqLayer.bringToFront();
}

async function calculateSequencedRoute(explore = false) {
    const start = getStartCoords();
    const end   = getEndCoords();
    if (!start || !end) { alert('Bitte Start und Ziel setzen.'); return; }

    // Sequenz aufloesen
    const resolved = [];
    for (const val of getSeqValues()) {
        if (!val.trim()) continue;
        const r = resolveCategory(val);
        if (!r) { alert(`Unbekannte Kategorie: "${val}"`); return; }
        resolved.push(r);
    }
    if (resolved.length === 0) { alert('Bitte mindestens eine Kategorie angeben.'); return; }
    lastSeqResolved = resolved;

    const seq     = resolved.map(r => r.tag).join(';');
    const dStart  = parseFloat(document.getElementById('seq-dstart').value)  || 2000;
    const dFactor = parseFloat(document.getElementById('seq-dfactor').value) || 2;

    const btn = document.getElementById(explore ? 'btn-sequenced-explore' : 'btn-sequenced');
    const btnLabel = btn.textContent;
    btn.disabled = true;
    btn.textContent = 'Berechne…';
    const status = document.getElementById('seq-status');
    status.textContent = '';

    try {
        const url = `/sequenced?from=${start.lat},${start.lng}&to=${end.lat},${end.lng}`
                  + `&seq=${seq}&d_start=${dStart}&d_factor=${dFactor}`
                  + (explore ? '&explore=1' : '');
        const res  = await fetch(url);
        const data = await res.json();

        clearSequenced();

        // Runden-Details fuer Debugging in die Konsole
        if (data.rounds) {
            console.table(data.rounds.map(r => ({
                cap_m: Math.round(r.cap),
                reached: r.reached,
                settled: r.settled,
                time_ms: Math.round(r.time_ms * 100) / 100
            })));
        }

        if (!data.success) {
            status.textContent = data.error || 'Keine Route gefunden.';
            return;
        }

        // Dijkstra-Suchbaeume merken (Anzeige erst beim Klick auf einen POI)
        seqExplorationData = data.exploration || null;

        // Segmente farbig zeichnen (eine Farbe je Abschnitt)
        seqLayer = L.geoJSON(data.route, {
            style: (feature) => ({
                color:   SEQ_COLORS[feature.properties.leg % SEQ_COLORS.length],
                weight:  5,
                opacity: 0.85
            })
        }).addTo(map);

        // Marker fuer die gewaehlten Facilities (Farbe = ausgehende Routenlinie).
        // Klick: Detailansicht (Typ + Name) und – falls aktiv – nur die
        // Dijkstra-Segmente dieses POI.
        (data.chosen || []).forEach((f, i) => {
            const ll    = L.latLng(f.coordinates[1], f.coordinates[0]);
            const m     = L.marker(ll, { icon: createIcon(seqOutColor(i)) }).addTo(map);
            const label = (lastSeqResolved[i] && lastSeqResolved[i].label) || 'POI';
            const name  = (f.tags && f.tags.name) ? f.tags.name : '(ohne Name)';
            m.bindPopup(
                `<div class="poi-popup">
                    <div class="poi-popup-type">${escapeHtml(label)}</div>
                    <div class="poi-popup-name">${escapeHtml(name)}</div>
                 </div>`
            );
            m.on('click', () => showPoiSegments(i));
            seqFacilityMarkers.push(m);
        });

        if (seqLayer.getBounds().isValid()) {
            map.fitBounds(seqLayer.getBounds(), { padding: [60, 60] });
        }

        const ms = (data.query_time_ms != null) ? ` · ${Math.round(data.query_time_ms)} ms` : '';
        status.textContent =
            `Distanz: ${data.total_distance_km} km · Runden: ${data.rounds_used}${ms}`;

        // Toggle-Zustand setzen; bei aktiver Anzeige Hinweis ergaenzen.
        setExploreButton(explore && !!seqExplorationData);
        if (seqExploreEnabled) status.textContent += ' · POI anklicken';

    } catch (err) {
        console.error(err);
        status.textContent = 'Fehler bei der Berechnung. Ist der Server erreichbar?';
    } finally {
        btn.disabled = false;
        btn.textContent = btnLabel;
    }
}

// --- Initialisierung ---
(function initSequenced() {
    // Datalist mit Beispiel-Kategorien fuellen
    const dl = document.getElementById('seq-presets');
    SEQ_PRESETS.forEach(p => {
        const opt = document.createElement('option');
        opt.value = p.label;
        dl.appendChild(opt);
    });

    // Zwei Startzeilen als Beispiel
    addSeqRow('Tankstelle');
    addSeqRow('Bäckerei');

    document.getElementById('btn-seq-add').addEventListener('click', () => addSeqRow());
    document.getElementById('btn-sequenced').addEventListener('click', () => calculateSequencedRoute(false));
    document.getElementById('btn-clear-sequenced').addEventListener('click', clearSequenced);

    // Toggle: an -> Route mit Suchbaeumen berechnen (Anzeige per POI-Klick);
    // aus -> aktuell gezeigten Baum ausblenden.
    document.getElementById('btn-sequenced-explore').addEventListener('click', () => {
        if (seqExploreEnabled) {
            if (seqExploreLayer) { map.removeLayer(seqExploreLayer); seqExploreLayer = null; }
            setExploreButton(false);
        } else {
            calculateSequencedRoute(true);
        }
    });
})();
