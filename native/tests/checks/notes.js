const root = document.getElementById('root');
if (!root.querySelector('h1') || root.querySelector('h1').textContent !== 'Notatki') throw new Error('render: ' + root.innerHTML);
// symulacja odpowiedzi list_notes przez wsad invoke obsługuje bundle_runner ({}), więc lista jest pusta
if (root.querySelectorAll('li.note').length !== 0) throw new Error('lista powinna być pusta');
const inp = root.querySelector('form.add input');
inp.value = 'Zażółć gęślą';
inp.dispatchEvent(new CustomEvent('input', { bubbles: true }));
root.querySelector('form.add').dispatchEvent(new CustomEvent('submit', { bubbles: true, cancelable: true }));
new Promise(r => setTimeout(r, 40)).then(() => {
  if (root.querySelectorAll('li.note').length !== 1) throw new Error('dodanie notatki: ' + root.innerHTML);
  if (root.textContent.indexOf('Zażółć gęślą') < 0) throw new Error('tekst notatki');
});
