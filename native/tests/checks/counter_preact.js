const app = document.getElementById('app');
const html = app.innerHTML;
if (html.indexOf('Zadania (1)') < 0) throw new Error('render: ' + html);
const lis = app.querySelectorAll('li');
if (lis.length !== 2) throw new Error('li count ' + lis.length);
if (!lis[1].classList.contains('done')) throw new Error('class done');
lis[0].dispatchEvent(new CustomEvent('click', { bubbles: true }));
const btn = app.querySelector('button');
btn.dispatchEvent(new CustomEvent('click'));
btn.dispatchEvent(new CustomEvent('click'));
Promise.resolve().then(() => Promise.resolve()).then(() => new Promise(r => setTimeout(r, 30))).then(() => {
  const h2 = app.innerHTML;
  if (h2.indexOf('Kliknięto 2') < 0) throw new Error('rerender button: ' + h2);
  if (h2.indexOf('Zadania (0)') < 0) throw new Error('rerender todos: ' + h2);
  globalThis.__preact_ok = 1;
});
