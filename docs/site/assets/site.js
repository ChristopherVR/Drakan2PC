document.documentElement.classList.add('js');

// Captures stay external. Animations load only when requested, including with reduced motion enabled.
document.querySelectorAll('.animation-toggle').forEach(button => {
  button.hidden = false;
  const picture = button.closest('figure').querySelector('img');
  const poster = picture.src;
  button.addEventListener('click', () => {
    const playing = button.getAttribute('aria-pressed') !== 'true';
    picture.src = playing ? button.dataset.animation : poster;
    button.setAttribute('aria-pressed', String(playing));
    button.textContent = playing ? 'Stop animation' : 'Play animation';
  });
});

document.querySelectorAll('.compare').forEach(figure => {
  const stage = figure.querySelector('.compare-stage');
  const range = figure.querySelector('.compare-range');
  const before = figure.querySelector('.compare-before');
  const after = figure.querySelector('.compare-after');
  range.addEventListener('input', () => stage.style.setProperty('--pos', range.value + '%'));
  figure.querySelectorAll('.compare-tab').forEach(tab => tab.addEventListener('click', () => {
    figure.querySelectorAll('.compare-tab').forEach(other => other.setAttribute('aria-pressed', String(other === tab)));
    before.src = tab.dataset.before;
    after.src = tab.dataset.after;
    before.alt = tab.textContent + ' with the original textures';
    after.alt = tab.textContent + ' with HD textures';
  }));
});

// Clips play while on screen, unless reduced motion is requested; the button always works.
const reduceMotion = matchMedia('(prefers-reduced-motion: reduce)').matches;
document.querySelectorAll('.media-video').forEach(figure => {
  const video = figure.querySelector('video');
  const button = figure.querySelector('.video-toggle');
  video.removeAttribute('controls');
  button.hidden = false;
  const sync = () => {
    button.textContent = video.paused ? 'Play' : 'Pause';
    button.setAttribute('aria-pressed', String(!video.paused));
  };
  video.addEventListener('play', sync);
  video.addEventListener('pause', sync);
  button.addEventListener('click', () => {
    figure.dataset.chosen = 'true';
    if (video.paused) video.play().catch(() => {});
    else video.pause();
  });
  if (reduceMotion || !('IntersectionObserver' in window)) return;
  new IntersectionObserver(([entry]) => {
    if (figure.dataset.chosen) return;
    if (entry.isIntersecting) video.play().catch(() => {});
    else video.pause();
  }, {threshold: 0.4}).observe(video);
});

const mediaDialog = document.querySelector('.media-dialog');
document.querySelectorAll('.media-enlarge').forEach(link => {
  link.addEventListener('click', event => {
    event.preventDefault();
    const picture = link.querySelector('img');
    const enlarged = mediaDialog.querySelector('img');
    enlarged.src = link.href;
    enlarged.alt = picture.alt;
    mediaDialog.querySelector('.media-description').textContent = link.closest('figure').querySelector('figcaption').textContent;
    mediaDialog.showModal();
  });
});
mediaDialog.querySelector('.media-close').addEventListener('click', () => mediaDialog.close());
mediaDialog.addEventListener('close', () => mediaDialog.querySelector('img').removeAttribute('src'));

const menu = document.querySelector('.doc-menu');
menu?.addEventListener('click', () => {
  const open = menu.getAttribute('aria-expanded') !== 'true';
  menu.setAttribute('aria-expanded', String(open));
  document.querySelector('.sidebar').classList.toggle('is-open', open);
});

// Tables retain native table semantics; only the surrounding region scrolls.
document.querySelectorAll('.prose table').forEach(table => {
  const wrapper = document.createElement('div');
  wrapper.className = 'table-scroll';
  wrapper.tabIndex = 0;
  wrapper.setAttribute('role', 'region');
  wrapper.setAttribute('aria-label', 'Scrollable table');
  table.before(wrapper);
  wrapper.append(table);
});

if (navigator.clipboard) document.querySelectorAll('.prose pre').forEach(pre => {
  const button = document.createElement('button');
  button.type = 'button';
  button.className = 'copy-code';
  button.textContent = 'Copy code';
  button.addEventListener('click', async () => {
    try {
      await navigator.clipboard.writeText(pre.textContent);
      button.textContent = 'Copied';
    } catch (_) {
      button.textContent = 'Select the code to copy';
    }
    setTimeout(() => { button.textContent = 'Copy code'; }, 2500);
  });
  pre.after(button);
});

const dialog = document.querySelector('.search-dialog');
const input = document.getElementById('search-input');
const results = document.getElementById('search-results');
const status = document.getElementById('search-status');
const base = new URL(document.body.dataset.base + '/', location.href);
let searchIndex;
let searchRequest;

async function loadIndex() {
  if (searchIndex) return searchIndex;
  if (!searchRequest) {
    searchRequest = fetch(new URL('search-index.json', base)).then(response => {
      if (!response.ok) throw new Error('Search index unavailable');
      return response.json();
    }).then(data => { searchIndex = data; return data; }).catch(error => {
      searchRequest = null;
      throw error;
    });
  }
  return searchRequest;
}

async function search() {
  const query = input.value.trim().toLocaleLowerCase();
  results.replaceChildren();
  if (!query) {
    status.textContent = 'Search player guides and developer documentation.';
    return;
  }
  status.textContent = 'Searching…';
  try {
    const index = await loadIndex();
    if (input.value.trim().toLocaleLowerCase() !== query) return;
    const terms = query.split(/\s+/);
    const matches = index.map(page => {
      const title = page.title.toLocaleLowerCase();
      const body = page.text.toLocaleLowerCase();
      if (!terms.every(term => (title + ' ' + body).includes(term))) return null;
      return {page, score: terms.reduce((sum, term) => sum + (title.includes(term) ? 10 : 1), 0)};
    }).filter(Boolean).sort((a, b) => b.score - a.score).slice(0, 12);
    status.textContent = matches.length ? `${matches.length} result${matches.length === 1 ? '' : 's'}` : 'No results. Try a shorter phrase, or browse the guides.';
    for (const {page} of matches) {
      const li = document.createElement('li');
      const link = document.createElement('a');
      link.href = new URL(page.url, base).href;
      const title = document.createElement('strong');
      title.textContent = page.title;
      const summary = document.createElement('span');
      summary.textContent = page.description;
      link.append(title, summary);
      li.append(link);
      results.append(li);
    }
  } catch (_) {
    status.textContent = 'Search could not load. Try again or use the guide navigation.';
  }
}

function openSearch() {
  if (dialog.open) return;
  dialog.showModal();
  input.focus();
  search();
}
document.querySelector('.search-open').hidden = false;
document.querySelector('.search-open').addEventListener('click', openSearch);
document.querySelector('.search-close').addEventListener('click', () => dialog.close());
dialog.addEventListener('click', event => { if (event.target === dialog && event.offsetX < 0) dialog.close(); });
input.addEventListener('input', search);
document.addEventListener('keydown', event => {
  if (event.key === '/' && !event.ctrlKey && !event.metaKey && !event.altKey && !event.target.closest('input, textarea, [contenteditable="true"]')) {
    event.preventDefault();
    openSearch();
  }
});
