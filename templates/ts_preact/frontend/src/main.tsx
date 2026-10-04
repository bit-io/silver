import { h, render, Fragment } from 'preact';
import { useEffect, useMemo, useRef, useState } from 'preact/hooks';
import { call, Note } from './api';

function useClock(): string {
  const [now, setNow] = useState(new Date());
  useEffect(() => {
    const id = setInterval(() => setNow(new Date()), 1000);
    return () => clearInterval(id);
  }, []);
  return now.toLocaleTimeString('pl-PL');
}

function NoteRow(props: { note: Note; onToggle: () => void; onDelete: () => void }) {
  const { note } = props;
  return (
    <li class={'note' + (note.pinned ? ' pinned' : '')}>
      <span class="text">{note.text}</span>
      <button class="pin" onClick={props.onToggle}>{note.pinned ? '★' : '☆'}</button>
      <button class="del" onClick={props.onDelete}>✕</button>
    </li>
  );
}

function App() {
  const [notes, setNotes] = useState<Note[]>([]);
  const [draft, setDraft] = useState('');
  const [query, setQuery] = useState('');
  const loaded = useRef(false);
  const clock = useClock();

  useEffect(() => { call('list_notes').then(r => { setNotes(r.notes); loaded.current = true; }); }, []);
  useEffect(() => { if (loaded.current) void call('save_notes', { notes }); }, [notes]);

  const shown = useMemo(
    () => notes
      .filter(n => n.text.toLowerCase().includes(query.toLowerCase()))
      .sort((a, b) => Number(b.pinned) - Number(a.pinned) || b.created - a.created),
    [notes, query]);

  function add(e: Event) {
    e.preventDefault();
    const text = draft.trim();
    if (!text) return;
    setNotes([...notes, { id: Date.now(), text, pinned: false, created: Date.now() }]);
    setDraft('');
  }

  return (
    <div class="app">
      <header><h1>Notatki</h1><span class="clock">{clock}</span></header>
      <input class="search" type="text" placeholder="Szukaj…" value={query}
             onInput={(e) => setQuery((e.target as HTMLInputElement).value)} />
      <form class="add" onSubmit={add}>
        <input type="text" placeholder="Nowa notatka" value={draft}
               onInput={(e) => setDraft((e.target as HTMLInputElement).value)} />
        <button type="submit">Dodaj</button>
      </form>
      <ul>
        {shown.map(n => (
          <NoteRow key={n.id} note={n}
            onToggle={() => setNotes(notes.map(x => x.id === n.id ? { ...x, pinned: !x.pinned } : x))}
            onDelete={() => setNotes(notes.filter(x => x.id !== n.id))} />
        ))}
      </ul>
      <footer>{notes.length} notatek · {notes.filter(n => n.pinned).length} przypiętych</footer>
    </div>
  );
}

render(<App />, document.getElementById('root')!);
