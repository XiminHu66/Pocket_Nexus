const tablist = document.querySelector('[role="tablist"]');
const tabs = [...tablist.querySelectorAll('[role="tab"]')];

function selectTab(active) {
  for (const tab of tabs) {
    const selected = tab === active;
    tab.setAttribute('aria-selected', String(selected));
    tab.tabIndex = selected ? 0 : -1;
    document.getElementById(tab.getAttribute('aria-controls')).hidden = !selected;
  }
}

function selectFromLocation() {
  const active = tabs.find(tab => '#' + tab.getAttribute('aria-controls') === location.hash);
  selectTab(active ?? tabs[0]);
}

function activate(tab) {
  selectTab(tab);
  const hash = tab === tabs[0] ? '' : '#' + tab.getAttribute('aria-controls');
  if (hash !== location.hash) {
    history.pushState(null, '', location.pathname + location.search + hash);
  }
}

tablist.addEventListener('click', event => {
  const tab = event.target.closest('[role="tab"]');
  if (tab) activate(tab);
});

tablist.addEventListener('keydown', event => {
  const index = tabs.indexOf(event.target);
  if (index < 0) return;
  const next = {
    ArrowLeft: (index + tabs.length - 1) % tabs.length,
    ArrowRight: (index + 1) % tabs.length,
    Home: 0,
    End: tabs.length - 1,
  }[event.key];
  if (next === undefined) return;
  event.preventDefault();
  tabs[next].focus();
  activate(tabs[next]);
});

window.addEventListener('popstate', selectFromLocation);
window.addEventListener('hashchange', selectFromLocation);
selectFromLocation();
