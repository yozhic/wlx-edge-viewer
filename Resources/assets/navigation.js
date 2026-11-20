// In-viewer navigation helpers shared by the Markdown and RST loaders.

// Pending CMD_READ requests: id -> { resolve, reject }.
const _evHostReads = new Map();
let _evHostReadId = 0;

window.chrome?.webview?.addEventListener?.('message', e => {
  const p = _evHostReads.get(e.data?.evRead);
  if (!p) return;
  _evHostReads.delete(e.data.evRead);
  if (e.data.ok) p.resolve(Uint8Array.from(atob(e.data.data), c => c.charCodeAt(0)));
  else p.reject(new Error('host read failed'));
});

// Read a linked file. XHR, not fetch(): Qt Web Engine's fetch() cannot reach
// custom schemes. Files on a network share (evh://) are read by the plugin:
// the page has origin "null", so its XHR to evh:// fails.
function evFetch(url) {
  return new Promise((resolve, reject) => {
    if (/^evh:/i.test(url)) {
      const id = String(++_evHostReadId);
      _evHostReads.set(id, { resolve, reject });
      window.chrome.webview.postMessage('CMD_READ|' + id + '|' + url.replace(/[?#].*$/, ''));
      return;
    }
    const x = new XMLHttpRequest();
    x.open('GET', url);
    x.responseType = 'arraybuffer';
    x.onload = () => {
      if (x.status >= 200 && x.status < 300) resolve(new Uint8Array(x.response || []));
      else reject(new Error('HTTP ' + x.status));
    };
    x.onerror = () => reject(new Error('net error'));
    x.send();
  });
}

// Message for a link that could not be opened. The default look uses :where()
// (zero specificity), so theme CSS overrides it: .ev-nav-error,
// .ev-nav-error-text, .ev-nav-error-close.
const _evErrorCss = `
  :where(.ev-nav-error) {
    position: fixed; top: 16px; left: 0; right: 0; z-index: 10000;
    width: fit-content; max-width: calc(100% - 32px); margin: 0 auto;
    display: flex; align-items: center; gap: 8px; padding: 8px 8px 8px 12px;
    border: 1px solid #d4a72c; border-radius: 6px; background: #fff8c5; color: #1f2328;
    font: 14px/1.4 sans-serif; box-shadow: 0 2px 8px rgba(0, 0, 0, 0.15);
  }
  :where(.ev-nav-error-text) { overflow-wrap: anywhere; }
  :where(.ev-nav-error-close) {
    flex: none; width: 28px; height: 28px; border: none; border-radius: 4px;
    background: transparent; color: inherit; font-size: 18px; line-height: 1; cursor: pointer;
  }
  :where(.ev-nav-error-close:hover) { background: rgba(128, 128, 128, 0.2); }
  @media (prefers-color-scheme: dark) {
    :where(.ev-nav-error) { border-color: #9e6a03; background: #3b2e0a; color: #f0f6fc; }
  }
`;

function evShowError(url) {
  evHideError();
  if (!document.getElementById('ev-nav-error-style')) {
    const style = document.createElement('style');
    style.id = 'ev-nav-error-style';
    style.textContent = _evErrorCss;
    document.head.prepend(style); // before the theme CSS
  }
  let name = url.pathname.split('/').pop();
  try { name = decodeURIComponent(name); } catch (e) {}
  const box = document.createElement('div');
  box.className = 'ev-nav-error';
  box.setAttribute('role', 'alert');
  const text = document.createElement('span');
  text.className = 'ev-nav-error-text';
  text.textContent = 'Cannot open file: ' + name;
  const close = document.createElement('button');
  close.type = 'button';
  close.className = 'ev-nav-error-close';
  close.title = 'Close';
  close.setAttribute('aria-label', 'Close');
  close.textContent = '×';
  box.append(text, close);
  document.body.appendChild(box);
}

// Called on every click, so the message stays until the next click.
function evHideError() {
  document.querySelector('.ev-nav-error')?.remove();
}

// A link that renders in place: the same scheme and host as the page.
// local.example maps the root of the disk or share, so any folder works.
function evIsInViewerLink(url) {
  const base = new URL(document.baseURI);
  return url.protocol === base.protocol && url.host === base.host;
}
