document.getElementById('reset').addEventListener('click', async () => {
  const el = document.getElementById('count');
  while (Number(el.textContent) !== 0) {
    await silver.invoke(Number(el.textContent) > 0 ? 'decrement' : 'increment');
    await new Promise(r => setTimeout(r, 15));   // setTimeout/Promise działają
  }
});

document.addEventListener('keydown', (e) => {
  if (e.key === 'ArrowUp' || e.key === ' ') silver.invoke('increment');
  if (e.key === 'ArrowDown') silver.invoke('decrement');
});

console.log('app.js załadowany, Silver', silver.version);
