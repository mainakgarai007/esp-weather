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
    { color: '#00d4ff', colorName: 'Cyan',         meaning: 'Optimal Humidity',   condition: 'Relative humidity 40–60 %',             priority: 10, category: 'Humidity' },
    { color: '#0080ff', colorName: 'Blue',          meaning: 'High Humidity',      condition: 'Relative humidity 60–80 %',             priority: 11, category: 'Humidity' },
    { color: '#004499', colorName: 'Deep Blue',     meaning: 'Very High Humidity', condition: 'Relative humidity > 80 %',              priority: 12, category: 'Humidity' },
    { color: '#ffcc44', colorName: 'Amber',         meaning: 'Low Humidity',       condition: 'Relative humidity 20–40 %',             priority: 13, category: 'Humidity' },
    { color: '#ff6600', colorName: 'Orange',        meaning: 'Very Low Humidity',  condition: 'Relative humidity < 20 %',              priority: 14, category: 'Humidity' },

    // ── AQI / Air Quality ──────────────────────────────────────────────────
    { color: '#00ff88', colorName: 'Mint Green',    meaning: 'Good Air Quality',   condition: 'AQI 0–50 (Good)',                       priority: 20, category: 'AQI' },
    { color: '#aaff00', colorName: 'Yellow-Green',  meaning: 'Moderate AQI',       condition: 'AQI 51–100 (Moderate)',                 priority: 21, category: 'AQI' },
    { color: '#ff9500', colorName: 'Orange',        meaning: 'Unhealthy (Sens)',   condition: 'AQI 101–150 (Unhealthy for Sensitive)', priority: 22, category: 'AQI' },
    { color: '#ff3366', colorName: 'Red',           meaning: 'Unhealthy AQI',      condition: 'AQI 151–200 (Unhealthy)',               priority: 23, category: 'AQI' },
    { color: '#7b2fff', colorName: 'Purple',        meaning: 'Very Unhealthy',     condition: 'AQI 201–300 (Very Unhealthy)',           priority: 24, category: 'AQI' },
    { color: '#8b0000', colorName: 'Maroon',        meaning: 'Hazardous AQI',      condition: 'AQI > 300 (Hazardous)',                 priority: 25, category: 'AQI' },

    // ── Wind ───────────────────────────────────────────────────────────────
    { color: '#e0e0f0', colorName: 'White',         meaning: 'Calm Wind',          condition: 'Wind speed < 5 km/h',                   priority: 30, category: 'Wind' },
    { color: '#aaddff', colorName: 'Light Blue',    meaning: 'Light Breeze',       condition: 'Wind speed 5–20 km/h',                  priority: 31, category: 'Wind' },
    { color: '#55aaff', colorName: 'Sky Blue',      meaning: 'Moderate Wind',      condition: 'Wind speed 20–40 km/h',                 priority: 32, category: 'Wind' },
    { color: '#ff9500', colorName: 'Orange',        meaning: 'Strong Wind',        condition: 'Wind speed 40–60 km/h',                 priority: 33, category: 'Wind' },
    { color: '#ff3366', colorName: 'Red',           meaning: 'Storm Wind',         condition: 'Wind speed > 60 km/h',                  priority: 34, category: 'Wind' },

    // ── Fog / Visibility ───────────────────────────────────────────────────
    { color: '#888899', colorName: 'Grey',          meaning: 'Light Fog',          condition: 'Visibility 5–10 km',                    priority: 40, category: 'Fog' },
    { color: '#555566', colorName: 'Dark Grey',     meaning: 'Dense Fog',          condition: 'Visibility 1–5 km',                     priority: 41, category: 'Fog' },
    { color: '#333344', colorName: 'Charcoal',      meaning: 'Thick Fog',          condition: 'Visibility < 1 km',                     priority: 42, category: 'Fog' },

    // ── UV Index ───────────────────────────────────────────────────────────
    { color: '#00ff00', colorName: 'Green',         meaning: 'Low UV',             condition: 'UV index 0–2 (Low)',                    priority: 50, category: 'UV' },
    { color: '#ffff00', colorName: 'Yellow',        meaning: 'Moderate UV',        condition: 'UV index 3–5 (Moderate)',               priority: 51, category: 'UV' },
    { color: '#ff8800', colorName: 'Orange-Red',    meaning: 'High UV',            condition: 'UV index 6–7 (High)',                   priority: 52, category: 'UV' },
    { color: '#ff0000', colorName: 'Red',           meaning: 'Very High UV',       condition: 'UV index 8–10 (Very High)',             priority: 53, category: 'UV' },
    { color: '#cc00cc', colorName: 'Violet',        meaning: 'Extreme UV',         condition: 'UV index ≥ 11 (Extreme)',               priority: 54, category: 'UV' },
  ];

  // ─── Pixel 2: Device Status ────────────────────────────────────────────

  /** @type {PixelState[]} */
  const PIXEL2 = [
    // ── WiFi ───────────────────────────────────────────────────────────────
    { color: '#00ff88', colorName: 'Mint Green',    meaning: 'WiFi Strong',        condition: 'RSSI ≥ −50 dBm (Excellent)',            priority: 10, category: 'WiFi' },
    { color: '#00d4ff', colorName: 'Cyan',          meaning: 'WiFi Good',          condition: 'RSSI −50 to −65 dBm',                   priority: 11, category: 'WiFi' },
    { color: '#ffcc44', colorName: 'Amber',         meaning: 'WiFi Weak',          condition: 'RSSI −65 to −80 dBm',                   priority: 12, category: 'WiFi' },
    { color: '#ff9500', colorName: 'Orange',        meaning: 'WiFi Poor',          condition: 'RSSI −80 to −90 dBm',                   priority: 13, category: 'WiFi' },
    { color: '#ff3366', colorName: 'Red',           meaning: 'WiFi Disconnected',  condition: 'No WiFi connection / RSSI < −90 dBm',   priority: 14, category: 'WiFi' },

    // ── Internet Quality ───────────────────────────────────────────────────
    { color: '#00ff88', colorName: 'Mint Green',    meaning: 'Internet OK',        condition: 'Ping < 50 ms, no packet loss',           priority: 20, category: 'Internet' },
    { color: '#ffcc44', colorName: 'Amber',         meaning: 'Internet Slow',      condition: 'Ping 50–200 ms or minor packet loss',    priority: 21, category: 'Internet' },
    { color: '#ff3366', colorName: 'Red',           meaning: 'No Internet',        condition: 'DNS/ping failure — WiFi connected only', priority: 22, category: 'Internet' },

    // ── Weather API ────────────────────────────────────────────────────────
    { color: '#00d4ff', colorName: 'Cyan',          meaning: 'API OK',             condition: 'Weather API responding normally',        priority: 30, category: 'API' },
    { color: '#ff9500', colorName: 'Orange',        meaning: 'API Degraded',       condition: 'API responding slowly (> 3 s)',          priority: 31, category: 'API' },
    { color: '#ff3366', colorName: 'Red',           meaning: 'API Failed',         condition: 'Weather API unreachable or error 5xx',   priority: 32, category: 'API' },
    { color: '#7b2fff', colorName: 'Purple',        meaning: 'API Rate Limited',   condition: 'HTTP 429 — too many requests',           priority: 33, category: 'API' },

    // ── OTA / Update ───────────────────────────────────────────────────────
    { color: '#ffffff', colorName: 'White',         meaning: 'OTA Idle',           condition: 'No pending firmware update',             priority: 40, category: 'OTA' },
    { color: '#aaddff', colorName: 'Light Blue',    meaning: 'OTA Checking',       condition: 'Checking GitHub for new release',        priority: 41, category: 'OTA' },
    { color: '#00d4ff', colorName: 'Cyan',          meaning: 'OTA Downloading',    condition: 'Firmware download in progress',          priority: 42, category: 'OTA' },
    { color: '#00ff88', colorName: 'Mint Green',    meaning: 'OTA Complete',       condition: 'Firmware flashed — restarting device',  priority: 43, category: 'OTA' },
    { color: '#ff3366', colorName: 'Red',           meaning: 'OTA Failed',         condition: 'Flash verification failed',             priority: 44, category: 'OTA' },

    // ── Special / Developer Modes ──────────────────────────────────────────
    { color: '#7b2fff', colorName: 'Purple',        meaning: 'Developer Mode',     condition: 'Dev/debug mode active via serial cmd',  priority: 50, category: 'Special' },
    { color: '#ff3366', colorName: 'Red (rapid)',   meaning: 'Pairing Mode',       condition: 'Waiting for dashboard pairing',          priority: 51, category: 'Special' },
    { color: '#ffcc44', colorName: 'Amber (blink)', meaning: 'Config Reset',       condition: 'Factory reset in progress',             priority: 52, category: 'Special' },
    { color: '#00ff00', colorName: 'Bright Green',  meaning: 'Self-Test Pass',     condition: 'POST self-test completed OK',            priority: 53, category: 'Special' },
    { color: '#000000', colorName: 'Off',           meaning: 'Sleep / Off',        condition: 'Deep sleep or LED disabled by user',     priority: 99, category: 'Special' },
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
      <div class="legend-swatch" style="background:${state.color};box-shadow:0 0 6px ${state.color}88" aria-hidden="true"></div>
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
