document.getElementById('greet-btn').addEventListener('click', async () => {
  const r = await silver.invoke('greet', {});
  document.getElementById('out').textContent = r.message;
});
