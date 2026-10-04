const $  = (s) => document.querySelector(s);
const $$ = (s) => Array.from(document.querySelectorAll(s));

// --- nawigacja ---
$$('.nav').forEach(a => a.addEventListener('click', () => {
  $$('.nav').forEach(x => x.classList.toggle('active', x === a));
  $$('.view').forEach(v => v.classList.toggle('hidden', v.id !== a.dataset.view));
}));

// --- wykres słupkowy (wysokość = % wartości) ---
let data = Array.from({ length: 12 }, () => 20 + Math.floor(Math.random() * 80));
function drawChart() {
  const chart = $('#chart');
  chart.innerHTML = '';
  const max = Math.max(...data);
  for (const v of data) {
    const bar = document.createElement('div');
    bar.className = 'bar';
    bar.style.height = Math.round(v / max * 100) + '%';
    bar.title = String(v);
    chart.appendChild(bar);
  }
  $('#range').textContent = `min ${Math.min(...data)} · max ${max}`;
}
$('#shuffle').addEventListener('click', () => {
  data = data.map(() => 20 + Math.floor(Math.random() * 80));
  drawChart();
});

// --- animowane liczniki KPI (requestAnimationFrame) ---
function countUp(el, to, ms = 800) {
  const t0 = performance.now();
  (function step(now) {
    const k = Math.min(1, (now - t0) / ms);
    el.textContent = Math.round(to * k).toLocaleString('pl-PL');
    if (k < 1) requestAnimationFrame(step);
  })(t0);
}
countUp($('#kpi-a'), 12840);
countUp($('#kpi-b'), 342);
countUp($('#kpi-c'), 28190);

// --- zadanie w tle: H# spawn_task → zdarzenie z wynikiem ---
$('#run').addEventListener('click', () => {
  $('#task-out').textContent = 'Uruchomiono…';
  silver.invoke('start_task');
});
silver.listen('silver://task', (p) => { $('#task-out').textContent = p.output || '(pusto)'; });

// --- fetch ---
$('#fetch').addEventListener('click', async () => {
  const out = $('#fetch-out');
  out.textContent = 'Pobieranie…';
  try {
    const r = await fetch('https://example.com');
    const text = await r.text();
    out.textContent = `HTTP ${r.status}, ${text.length} znaków`;
  } catch (e) {
    out.textContent = 'Błąd: ' + e.message;
  }
});

drawChart();
