(function () {
  let statusRafId = null;

  function stopStatusAutoScroll() {
    try {
      if (statusRafId) cancelAnimationFrame(statusRafId);
    } catch (_) {}
    statusRafId = null;
  }

  function startStatusAutoScroll() {
    const el = document.getElementById('status');
    if (!el) return;
    stopStatusAutoScroll();

    const max = el.scrollWidth - el.clientWidth;
    if (max <= 0) return;

    let pos = el.scrollLeft || 0;
    let dir = 1;
    const step = 1;

    function tick() {
      const node = document.getElementById('status');
      if (!node) return;

      pos += dir * step;
      if (pos >= max) {
        pos = max;
        dir = -1;
      } else if (pos <= 0) {
        pos = 0;
        dir = 1;
      }
      node.scrollLeft = pos;
      statusRafId = requestAnimationFrame(tick);
    }

    statusRafId = requestAnimationFrame(tick);
  }

  function setStatusMessage(msg) {
    try {
      const el = document.getElementById('status');
      if (!el) return;
      el.textContent = String(msg);
      setTimeout(startStatusAutoScroll, 0);
    } catch (_) {}
  }

  window.printErrorToStatus = function (err, context) {
    const msg = (err && err.message) ? err.message : String(err);
    const prefix = context ? '[' + String(context) + '] ' : '';
    setStatusMessage(prefix + msg);
  };

  const originalFetch = window.fetch;
  window.fetch = async function (input, init) {
    const url = (typeof input === 'string') ? input : (input && input.url ? input.url : '');
    const method = (init && init.method) || (typeof input === 'object' && input && input.method) || 'GET';
    try {
      const res = await originalFetch(input, init);
      if (!res.ok) {
        let detail = '';
        try {
          const txt = await res.clone().text();
          try {
            const obj = JSON.parse(txt);
            detail = obj && (obj.detail || obj.message) ? (obj.detail || obj.message) : txt;
          } catch (_) {
            detail = txt;
          }
        } catch (_) {
          detail = res.statusText || '未知错误';
        }
        setStatusMessage(`错误 ${res.status} ${method} ${url}: ${String(detail).trim()}`);
      }
      return res;
    } catch (err) {
      setStatusMessage(`网络错误 ${method} ${url}: ${err && err.message ? err.message : String(err)}`);
      throw err;
    }
  };

  window.addEventListener('unhandledrejection', function (ev) {
    try {
      const reason = ev && ev.reason ? ev.reason : 'Promise rejection';
      window.printErrorToStatus(reason, 'Promise');
    } catch (_) {}
  });

  window.addEventListener('error', function (ev) {
    try {
      if (!ev) return;
      const msg = ev.error ? (ev.error.message || String(ev.error)) : (ev.message || '错误');
      window.printErrorToStatus(msg, 'JS');
    } catch (_) {}
  });
})();
