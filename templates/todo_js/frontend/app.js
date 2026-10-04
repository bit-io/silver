const $ = (sel) => document.querySelector(sel);
const state = { todos: [], filter: 'all', nextId: 1 };

async function load() {
  const r = await silver.invoke('load_todos');
  state.todos = r.todos || [];
  state.nextId = state.todos.reduce((m, t) => Math.max(m, t.id), 0) + 1;
  render();
}

let saveTimer = null;
function save() {                       // zapis z opóźnieniem (debounce)
  clearTimeout(saveTimer);
  saveTimer = setTimeout(() => silver.invoke('save_todos', { todos: state.todos }), 200);
}

function toast(msg) {
  const el = $('#toast');
  el.textContent = msg;
  setTimeout(() => { if (el.textContent === msg) el.textContent = ''; }, 2000);
}

function visible() {
  return state.todos.filter(t =>
    state.filter === 'all' || (state.filter === 'done') === t.done);
}

function render() {
  const list = $('#list');
  list.innerHTML = '';
  const items = visible();
  if (!items.length) {
    const li = document.createElement('li');
    li.className = 'empty';
    li.textContent = 'Brak zadań';
    list.appendChild(li);
  }
  for (const t of items) {
    const li = document.createElement('li');
    li.className = 'todo' + (t.done ? ' done' : '');
    li.dataset.id = t.id;

    const box = document.createElement('input');
    box.type = 'checkbox';
    box.checked = t.done;
    box.dataset.action = 'toggle';

    const title = document.createElement('span');
    title.className = 'title';
    title.textContent = t.title;

    const del = document.createElement('button');
    del.className = 'del';
    del.textContent = '✕';
    del.dataset.action = 'delete';

    li.append(box, title, del);
    list.appendChild(li);
  }
  $('#counter').textContent = String(state.todos.filter(t => !t.done).length);
  document.querySelectorAll('#filters button').forEach(b =>
    b.classList.toggle('active', b.dataset.filter === state.filter));
}

// Delegacja zdarzeń: jeden nasłuch na liście
$('#list').addEventListener('click', (e) => {
  const li = e.target.closest('li.todo');
  if (!li) return;
  const id = Number(li.dataset.id);
  const action = e.target.dataset.action;
  if (action === 'toggle') {
    const t = state.todos.find(x => x.id === id);
    t.done = !t.done;
  } else if (action === 'delete') {
    state.todos = state.todos.filter(x => x.id !== id);
  } else return;
  save(); render();
});

$('#filters').addEventListener('click', (e) => {
  if (!e.target.dataset.filter) return;
  state.filter = e.target.dataset.filter;
  render();
});

$('#add-form').addEventListener('submit', () => {
  const input = $('#new-title');
  const title = input.value.trim();
  if (!title) return;
  state.todos.push({ id: state.nextId++, title, done: false });
  input.value = '';
  save(); render();
  toast('Dodano');
});

$('#clear-done').addEventListener('click', async () => {
  const n = state.todos.filter(t => t.done).length;
  if (!n) return toast('Nic do usunięcia');
  if (await silver.dialog.confirm(`Usunąć ${n} zrobionych zadań?`)) {
    state.todos = state.todos.filter(t => !t.done);
    save(); render(); toast('Usunięto');
  }
});

$('#export').addEventListener('click', async () => {
  const path = await silver.dialog.save({ title: 'Eksport zadań', name: 'zadania.json' });
  if (!path) return;
  await silver.clipboard.writeText(JSON.stringify(state.todos, null, 2));
  toast('Skopiowano JSON do schowka');
});

window.addEventListener('keydown', (e) => {
  if (e.key === 'Escape') $('#new-title').value = '';
});

load();
