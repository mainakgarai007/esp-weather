/**
 * pixel-legend.js — SkyCore IoT Dashboard
 * Defines all NeoPixel color states and renders them as legend grids.
 *
 * Pixel 1 = Environment indicator  (humidity, AQI, wind, fog, UV)
 * Pixel 2 = Device status indicator (WiFi, internet, API, OTA, special modes)
 *
 * Public API (window.PixelLegend):
 *   PixelLegend.PIXEL1  — array of state definitions
 *   PixelLegend.PIXEL2  — array of state definitions
 *   PixelLegend.render(containerId, pixelIndex)
 *   PixelLegend.renderAll()
 *   PixelLegend.getStateByColor(hex, pixelIndex)
 */

(function (global) {
  'use strict';

  /**
   * @typedef {Object} PixelState
   * @property {string} color      - Hex color (e.g. "#00ff88")
   * @property {string} colorName  - Human-readable color name
   * @property {string} meaning    - Short display label
   * @property {string} condition  - Detailed description / trigger condition
   * @property {number} priority   - Lower = higher priority (displayed first)
   * @property {string} [category] - Grouping label
   */

  // ─── Pixel 1: Environment ─────────────────────────────────────────────

  /** @type {PixelState[]} */
  const PIXEL1 = [
    // ── Humidity ──────────────────────────────────────────────────────────
    { color: '#0033ff', colorName: 'Deep Blue',  meaning: 'Low Humidity',     condition: 'Humidity < 30 %',         effect: 'dim-pulse', priority: 10, category: 'Humidity' },
    { color: '#0066ff', colorName: 'Blue',       meaning: 'Medium Humidity',  condition: 'Humidity 30–60 %',        effect: 'solid',     priority: 11, category: 'Humidity' },
    { color: '#7b2fff', colorName: 'Purple',     meaning: 'High Humidity',    condition: 'Humidity 60–80 %',        effect: 'slow-pulse',priority: 12, category: 'Humidity' },
    { color: '#b44cff', colorName: 'Violet',     meaning: 'Very High Humidity',condition: 'Humidity > 80 %',        effect: 'fast-pulse',priority: 13, category: 'Humidity' },

    // ── AQI / Air Quality ──────────────────────────────────────────────────
    { color: '#00ff88', colorName: 'Green',      meaning: 'AQI Good',          condition: 'AQI 1 (Good)',            effect: 'solid',     priority: 20, category: 'AQI' },
    { color: '#ffff00', colorName: 'Yellow',     meaning: 'AQI Moderate',      condition: 'AQI 2 (Moderate)',        effect: 'solid',     priority: 21, category: 'AQI' },
    { color: '#ff9500', colorName: 'Orange',     meaning: 'AQI Bad',           condition: 'AQI 3 (Bad)',             effect: 'solid',     priority: 22, category: 'AQI' },
    { color: '#ff3366', colorName: 'Red',        meaning: 'AQI Dangerous',     condition: 'AQI 4 (Danger)',          effect: 'blink',     priority: 23, category: 'AQI' },
    { color: '#ff0000', colorName: 'Red',        meaning: 'AQI Very Dangerous',condition: 'AQI 5 (Very Dangerous)', effect: 'fast-blink',priority: 24, category: 'AQI' },

    // ── Wind ───────────────────────────────────────────────────────────────
    { color: '#66ffee', colorName: 'Soft Cyan',  meaning: 'Wind Low',          condition: 'Wind < 5 m/s',            effect: 'solid',     priority: 30, category: 'Wind' },
    { color: '#00d4ff', colorName: 'Cyan',       meaning: 'Wind Medium',       condition: 'Wind 5–10 m/s',           effect: 'pulse',     priority: 31, category: 'Wind' },
    { color: '#00ffff', colorName: 'Bright Cyan',meaning: 'Strong Wind',       condition: 'Wind 10–17 m/s',          effect: 'blink',     priority: 32, category: 'Wind' },
    { color: '#00ffff', colorName: 'Bright Cyan',meaning: 'Storm Wind',        condition: 'Wind ≥ 17 m/s',           effect: 'fast-blink',priority: 33, category: 'Wind' },

    // ── Fog / Visibility ───────────────────────────────────────────────────
    { color: '#dddddd', colorName: 'White Dim',  meaning: 'Light Fog',         condition: 'Visibility 5–10 km',      effect: 'dim',       priority: 40, category: 'Fog' },
    { color: '#ffffff', colorName: 'White',      meaning: 'Medium Fog',        condition: 'Visibility 1–5 km',       effect: 'pulse',     priority: 41, category: 'Fog' },
    { color: '#ffffff', colorName: 'White',      meaning: 'Heavy Fog',         condition: 'Visibility < 1 km',       effect: 'blink',     priority: 42, category: 'Fog' },

    // ── UV Index ───────────────────────────────────────────────────────────
    { color: '#000000', colorName: 'Off',        meaning: 'UV Low',            condition: 'UV 0–2',                  effect: 'off',       priority: 50, category: 'UV' },
    { color: '#ff8800', colorName: 'Orange',     meaning: 'UV Moderate',       condition: 'UV 3–7',                  effect: 'dim',       priority: 51, category: 'UV' },
    { color: '#ff8800', colorName: 'Orange',     meaning: 'UV High',           condition: 'UV 8–10',                 effect: 'pulse',     priority: 52, category: 'UV' },
    { color: '#ff6600', colorName: 'Orange',     meaning: 'UV Extreme',        condition: 'UV ≥ 11',                 effect: 'fast-blink',priority: 53, category: 'UV' },

    // ── Severity Overrides ─────────────────────────────────────────────────
    { color: '#7b2fff', colorName: 'Purple',     meaning: 'Warning',           condition: 'Environmental warning',   effect: 'blink',     priority: 90, category: 'Severity' },
    { color: '#ff3366', colorName: 'Red',        meaning: 'Danger',            condition: 'Danger alert active',     effect: 'fast-blink',priority: 91, category: 'Severity' },
    { color: '#ff3366', colorName: 'Red',        meaning: 'Critical',          condition: 'Critical alert active',   effect: 'rapid',     priority: 92, category: 'Severity' },
  ];

  // ─── Pixel 2: Device Status ────────────────────────────────────────────

  /** @type {PixelState[]} */
  const PIXEL2 = [
    // ── WiFi ───────────────────────────────────────────────────────────────
    { color: '#00ff88', colorName: 'Green',    meaning: 'WiFi Connected',     condition: 'RSSI strong',                effect: 'solid',     priority: 10, category: 'WiFi' },
    { color: '#ffff00', colorName: 'Yellow',   meaning: 'WiFi Weak',          condition: 'RSSI weak',                  effect: 'pulse',     priority: 11, category: 'WiFi' },
    { color: '#ff9500', colorName: 'Orange',   meaning: 'WiFi Very Weak',     condition: 'RSSI very weak',             effect: 'pulse',     priority: 12, category: 'WiFi' },
    { color: '#ff3366', colorName: 'Red',      meaning: 'WiFi Disconnected',  condition: 'No WiFi',                    effect: 'blink',     priority: 13, category: 'WiFi' },

    // ── Internet Quality ───────────────────────────────────────────────────
    { color: '#00d4ff', colorName: 'Blue',     meaning: 'Internet Good',       condition: 'Low latency',               effect: 'pulse',     priority: 20, category: 'Internet' },
    { color: '#7b2fff', colorName: 'Purple',   meaning: 'Internet Slow',       condition: 'Medium latency',            effect: 'pulse',     priority: 21, category: 'Internet' },
    { color: '#ff9500', colorName: 'Orange',   meaning: 'Internet Bad',        condition: 'High latency',              effect: 'blink',     priority: 22, category: 'Internet' },
    { color: '#ff3366', colorName: 'Red',      meaning: 'No Internet',         condition: 'Offline',                   effect: 'blink',     priority: 23, category: 'Internet' },

    // ── Weather API ────────────────────────────────────────────────────────
    { color: '#00ff88', colorName: 'Green',    meaning: 'API OK',              condition: 'Update received',           effect: 'flash',     priority: 30, category: 'API' },
    { color: '#ffff00', colorName: 'Yellow',   meaning: 'API Slow',            condition: 'Slow response',             effect: 'flash',     priority: 31, category: 'API' },
    { color: '#ff3366', colorName: 'Red',      meaning: 'API Failed',          condition: 'No response',               effect: 'triple',    priority: 32, category: 'API' },

    // ── OTA / Update ───────────────────────────────────────────────────────
    { color: '#ffffff', colorName: 'White',    meaning: 'Updating',            condition: 'Firmware update in progress',effect: 'fast-blink',priority: 40, category: 'Update' },
    { color: '#00ff88', colorName: 'Green',    meaning: 'Update Success',      condition: 'Update completed',          effect: 'solid',     priority: 41, category: 'Update' },
    { color: '#ff3366', colorName: 'Red',      meaning: 'Update Failed',       condition: 'Update error',              effect: 'blink',     priority: 42, category: 'Update' },

    // ── Special / Developer Modes ──────────────────────────────────────────
    { color: '#7b2fff', colorName: 'Rainbow',  meaning: 'Developer Mode',      condition: 'Dev mode active',           effect: 'rainbow',   priority: 50, category: 'Special' },
    { color: '#ff3366', colorName: 'Red/Blue', meaning: 'Recovery Mode',       condition: 'Recovery boot',             effect: 'alternate', priority: 51, category: 'Special' },
    { color: '#b44cff', colorName: 'Purple',   meaning: 'WiFi Setup',          condition: 'Captive portal active',     effect: 'breathing', priority: 52, category: 'Special' },
    { color: '#00d4ff', colorName: 'Blue',     meaning: 'Idle',                condition: 'Device idle',               effect: 'dim',       priority: 60, category: 'Special' },
  ];

  // ─── Rendering ────────────────────────────────────────────────────────

  /**
   * Build a legend-item element for one state.
   * @param {PixelState} state
   * @returns {HTMLElement}
   */
  function _buildItem(state) {
    const item = document.createElement('div');
    item.className = 'legend-item';
    item.setAttribute('data-tooltip', state.condition);
    item.setAttribute('data-category', state.category || '');
    item.innerHTML = `
      <div class="legend-swatch ${state.effect ? 'effect-' + state.effect : ''}" style="background:${state.color};box-shadow:0 0 6px ${state.color}88" aria-hidden="true"></div>
      <div>
        <div class="legend-label">${_esc(state.meaning)}</div>
        <div class="legend-desc">${_esc(state.colorName)}</div>
      </div>
    `;
    return item;
  }

  function _esc(str) {
    return String(str)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  }

  /**
   * Render legend for one pixel into a container element.
   * States are grouped by category with a heading.
   * @param {string|HTMLElement} container - Element or its id.
   * @param {1|2} pixelIndex
   */
  function render(container, pixelIndex) {
    const el = typeof container === 'string'
      ? document.getElementById(container)
      : container;

    if (!el) {
      console.warn('[PixelLegend] render: container not found:', container);
      return;
    }

    const states = pixelIndex === 1 ? PIXEL1 : PIXEL2;
    const sorted = [...states].sort((a, b) => a.priority - b.priority);

    // Group by category
    const groups = {};
    sorted.forEach(s => {
      const cat = s.category || 'Other';
      if (!groups[cat]) groups[cat] = [];
      groups[cat].push(s);
    });

    el.innerHTML = '';

    Object.entries(groups).forEach(([cat, items]) => {
      const section = document.createElement('div');
      section.style.marginBottom = 'var(--space-md)';

      const heading = document.createElement('p');
      heading.className = 'text-xs text-muted text-mono';
      heading.style.cssText = 'text-transform:uppercase;letter-spacing:0.1em;margin-bottom:var(--space-xs)';
      heading.textContent = cat;
      section.appendChild(heading);

      const grid = document.createElement('div');
      grid.className = 'legend-grid';
      items.forEach(state => grid.appendChild(_buildItem(state)));
      section.appendChild(grid);

      el.appendChild(section);
    });
  }

  /**
   * Convenience: render both pixels using ids `pixel1-legend` and `pixel2-legend`.
   */
  function renderAll() {
    render('pixel1-legend', 1);
    render('pixel2-legend', 2);
  }

  /**
   * Find a state definition by hex color.
   * @param {string} hex
   * @param {1|2} pixelIndex
   * @returns {PixelState|null}
   */
  function getStateByColor(hex, pixelIndex) {
    const states = pixelIndex === 1 ? PIXEL1 : PIXEL2;
    const normal  = hex.toLowerCase().trim();
    return states.find(s => s.color.toLowerCase() === normal) || null;
  }

  // ─── Public API ───────────────────────────────────────────────────────
  global.PixelLegend = { PIXEL1, PIXEL2, render, renderAll, getStateByColor };

}(window));
