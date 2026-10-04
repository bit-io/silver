<script>
  import { onMount } from 'svelte';

  let notes = $state([]);
  let draft = $state('');
  let query = $state('');
  let now = $state(new Date());
  let loaded = false;

  let shown = $derived(
    notes
      .filter(n => n.text.toLowerCase().includes(query.toLowerCase()))
      .sort((a, b) => Number(b.pinned) - Number(a.pinned) || b.created - a.created)
  );

  onMount(async () => {
    const r = await silver.invoke('list_notes');
    notes = r.notes;
    loaded = true;
    const id = setInterval(() => (now = new Date()), 1000);
    return () => clearInterval(id);
  });

  $effect(() => {
    const snapshot = $state.snapshot(notes);
    if (loaded) silver.invoke('save_notes', { notes: snapshot });
  });

  function add(e) {
    e.preventDefault();
    const text = draft.trim();
    if (!text) return;
    notes.push({ id: Date.now(), text, pinned: false, created: Date.now() });
    draft = '';
  }
</script>

<div class="app">
  <header><h1>Notatki</h1><span class="clock">{now.toLocaleTimeString('pl-PL')}</span></header>
  <input class="search" type="text" placeholder="Szukaj…" bind:value={query} />
  <form class="add" onsubmit={add}>
    <input type="text" placeholder="Nowa notatka" bind:value={draft} />
    <button type="submit">Dodaj</button>
  </form>
  <ul>
    {#each shown as n (n.id)}
      <li class="note" class:pinned={n.pinned}>
        <span class="text">{n.text}</span>
        <button class="pin" onclick={() => (n.pinned = !n.pinned)}>{n.pinned ? '★' : '☆'}</button>
        <button class="del" onclick={() => (notes = notes.filter(x => x.id !== n.id))}>✕</button>
      </li>
    {/each}
  </ul>
  <footer>{notes.length} notatek · {notes.filter(n => n.pinned).length} przypiętych</footer>
</div>
