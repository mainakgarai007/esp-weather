/**
 * firmware-viewer.js — loads and highlights the firmware code.
 */
(function (global) {
  'use strict';

  const FIRMWARE_PATH = 'firmware/SkyCore.ino';
  const VERSION = 'v1.1.0';

  function escapeHtml(str) {
    return String(str)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  }

  function highlight(code) {
    let escaped = escapeHtml(code);

    const placeholders = [];
    function stash(regex, cls) {
      escaped = escaped.replace(regex, (match) => {
        const token = `__TOKEN_${placeholders.length}__`;
        placeholders.push({ token, html: `<span class="${cls}">${match}</span>` });
        return token;
      });
    }

    stash(/\/\*[\s\S]*?\*\//g, 'tok-comment');
    stash(/\/\/.*$/gm, 'tok-comment');
    stash(/"([^"\\]|\\.)*"/g, 'tok-string');
    stash(/'([^'\\]|\\.)*'/g, 'tok-string');

    escaped = escaped.replace(/\b(\d+(?:\.\d+)?)\b/g, '<span class="tok-number">$1</span>');
    escaped = escaped.replace(/\b(void|int|float|double|bool|char|uint8_t|uint16_t|uint32_t|String|struct|enum|class)\b/g,
      '<span class="tok-type">$1</span>');
    escaped = escaped.replace(/\b(const|static|return|if|else|for|while|switch|case|break|continue|default|include|define|typedef)\b/g,
      '<span class="tok-keyword">$1</span>');
    escaped = escaped.replace(/\b([A-Za-z_][A-Za-z0-9_]*)\s*(?=\()/g,
      '<span class="tok-function">$1</span>');
    escaped = escaped.replace(/(#include|#define)/g, '<span class="tok-keyword">$1</span>');

    placeholders.forEach(({ token, html }) => {
      escaped = escaped.replace(token, html);
    });

    return escaped;
  }

  async function loadFirmware() {
    const codeEl = document.getElementById('firmware-code');
    if (!codeEl) return;

    try {
      const res = await fetch(FIRMWARE_PATH);
      const text = await res.text();
      codeEl.innerHTML = highlight(text);
      codeEl.setAttribute('data-raw', text);
    } catch {
      codeEl.textContent = 'Failed to load firmware.';
    }

    document.querySelectorAll('[data-firmware="version"]').forEach(el => {
      el.textContent = VERSION;
    });
  }

  global.FirmwareViewer = { loadFirmware };
}(window));
