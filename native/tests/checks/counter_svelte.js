const app = document.getElementById('app');
if (app.innerHTML.indexOf('Licznik: 0') < 0) throw new Error('render: ' + app.innerHTML);
if (app.querySelectorAll('li').length !== 2) throw new Error('each');
const btn = app.querySelector('button');
btn.dispatchEvent(new CustomEvent('click'));
btn.dispatchEvent(new CustomEvent('click'));
app.querySelector('button.add').dispatchEvent(new CustomEvent('click'));
new Promise(r => setTimeout(r, 30)).then(() => {
  const h = app.innerHTML;
  if (h.indexOf('Licznik: 2') < 0 || h.indexOf('x2 = 4') < 0) throw new Error('reactive: ' + h);
  if (!app.querySelector('p.big')) throw new Error('if block');
  if (app.querySelectorAll('li').length !== 3) throw new Error('each update ' + app.querySelectorAll('li').length);
});
