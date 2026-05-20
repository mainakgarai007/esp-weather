/**
 * app.js — SkyCore IoT Dashboard
 * Main application controller. Initialises all modules, polls the ESP device
 * API every 30 seconds and keeps all UI elements in sync.
 *
 * Depends on (must be loaded before this script):
 *   storage.js · pairing.js · notifications.js · pixel-legend.js
 */

(function (global) {
  'use strict';

  // ─── Constants ────────────────────────────────────────────────────────────

  const POLL_INTERVAL_MS = 30_000;
  const API_TIMEOUT_MS   = 8_000;

  /** ESP API endpoint paths. */
  const ESP_ENDPOINTS = {
    STATUS: '/api/status',
    PAIR:   '/api/pair',
    CONFIG: '/api/config',
  };

  // ─── Mock / demo data ─────────────────────────────────────────────────────

  /**
   * Simulated device response used when offline or in demo mode.
   * @returns {Object}
   */
  function _mockStatus() {
    const now = new Date();
    return {
      device: {
        name: Storage.get(Storage.KEYS.DEVICE_NAME, 'SkyCore'),
        ip: '192.168.1.42',
        firmware: 'v1.1.0',
        uptime: Math.floor(Date.now() / 1000) % 86400,
        rssi: -58,
        freeHeap: 142080,
        errorCount: 0,
        devMode: false,
        nightMode: false,
        recovery: false,
        demo: true,
      },
      network: {
        wifiQuality: 'connected',
        internetQuality: 'good',
        apiStatus: 'working',
      },
      weather: {
        temp: 22.4,
        feelsLike: 21.1,
        humidity: 54,
        pressure: 1013,
        windSpeed: 6.2,
        windDir: 'NW',
        aqi: 2,
        uvIndex: 3,
        visibility: 10,
        condition: 'Partly Cloudy',
        description: 'Partly cloudy with mild breeze',
        season: 'Summer',
        moonPhase: '🌗 Last Quarter',
        dayNight: 'Day',
        weekend: false,
        holiday: false,
        weatherMode: 'Cloudy',
      },
      pixels: {
        pixel1: { color: '#7b2fff', effect: 'pulse', meaning: 'High Humidity', reason: 'Humidity 65%' },
        pixel2: { color: '#00ff88', effect: 'solid', meaning: 'WiFi Strong', reason: 'Signal strong' },
      },
      leds: {
        displayMode: 'weather',
        weather: 'Cloudy',
        season: 'Summer',
        warmMode: 'Day',
        yellowMode: 'Weekday',
        extraMode: 'Success',
        brightness: 80,
        animations: true,
        buzzer: true,
        nightMute: true,
      },
      status: {
        updateState: 'idle',
      },
      timestamp: now.toISOString(),
    };
  }

  // ─── Application state ────────────────────────────────────────────────────

  const state = {
    online:       false,
    demoMode:     false,
    lastData:     null,
    pollTimer:    null,
    espBaseURL:   '',
    lastAlerts:   {},
  };

  // ─── ESP API calls ────────────────────────────────────────────────────────

  /**
   * Generic ESP API fetch with timeout.
   * @param {string} path - Endpoint path.
   * @param {RequestInit} [options]
   * @returns {Promise<Object>}
   */
  async function _apiCall(path, options = {}) {
    const url = state.espBaseURL + path;
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), API_TIMEOUT_MS);
    try {
      const res = await fetch(url, { ...options, signal: controller.signal });
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      return await res.json();
    } finally {
      clearTimeout(timer);
    }
  }

  /**
   * Normalize API response shape for older firmware.
   * @param {Object} data
   * @returns {Object}
   */
  function _normalizeStatus(data) {
    if (!data) return _mockStatus();
    if (data.device && typeof data.device === 'object') return data;

    // Legacy flat response
    return {
      device: {
        name: data.device || 'SkyCore',
        ip: data.ip || '—',
        firmware: data.firmwareVer || 'unknown',
        uptime: data.uptime || 0,
        freeHeap: data.freeHeap || 0,
        rssi: data.rssi || 0,
        errorCount: 0,
        devMode: Boolean(data.devMode),
        nightMode: Boolean(data.nightMode),
        recovery: false,
      },
      network: {
        wifiQuality: 'unknown',
        internetQuality: 'unknown',
        apiStatus: String(data.apiStatus || 'unknown'),
      },
      weather: {
        temp: data.temp || 0,
        feelsLike: data.temp || 0,
        humidity: data.humidity || 0,
        pressure: data.pressure || 0,
        windSpeed: data.windSpeed || 0,
        windDir: '—',
        aqi: data.aqi || 0,
        uvIndex: data.uvIndex || 0,
        visibility: 0,
        condition: 'Unknown',
        description: '—',
        season: '—',
        moonPhase: '—',
        dayNight: data.nightMode ? 'Night' : 'Day',
        weekend: false,
        holiday: false,
        weatherMode: '—',
      },
      pixels: {
        pixel1: { color: '#00d4ff', effect: 'solid', meaning: '—', reason: '—' },
        pixel2: { color: '#00ff88', effect: 'solid', meaning: '—', reason: '—' },
      },
      leds: {
        displayMode: 'weather',
        weather: '—',
        season: '—',
        warmMode: '—',
        yellowMode: '—',
        extraMode: '—',
        brightness: 0,
        animations: true,
        buzzer: true,
        nightMute: true,
      },
      status: { updateState: 'idle' },
    };
  }

  /**
   * Fetch device status from `/api/status`.
   * Falls back to mock data when the device is unreachable.
   * @returns {Promise<Object>}
   */
  async function fetchStatus() {
    if (state.demoMode || !state.espBaseURL) {
      _setOnline(false);
      return _mockStatus();
    }

    try {
      const data = await _apiCall(ESP_ENDPOINTS.STATUS);
      _setOnline(true);
      return _normalizeStatus(data);
    } catch (err) {
      _log('warn', `Device unreachable: ${err.message}`);
      if (state.online) {
        Notifications.sendAlert(Notifications.AlertType.DEVICE_OFFLINE);
      }
      _setOnline(false);
      return _mockStatus();
    }
  }

  /**
   * Send pairing information to the ESP device.
   * @param {string} key
   * @returns {Promise<boolean>}
   */
  async function pairDevice(key) {
    try {
      await _apiCall(ESP_ENDPOINTS.PAIR, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ key }),
      });
      Storage.set(Storage.KEYS.PAIRING_VERIFIED, true);
      _log('ok', 'Device pairing confirmed.');
      Notifications.toast('Device paired successfully!', 'success', 'Paired ✅');
      return true;
    } catch (err) {
      _log('error', `Pairing failed: ${err.message}`);
      Notifications.toast('Could not reach the device for pairing.', 'error', 'Pairing Failed');
      return false;
    }
  }

  /**
   * Push a config payload to `/api/config`.
   * @param {Object} cfg
   * @returns {Promise<boolean>}
   */
  async function pushConfig(cfg) {
    try {
      const { key } = Pairing.getPairing();
      if (!key) throw new Error('Pairing key missing');
      await _apiCall(ESP_ENDPOINTS.CONFIG, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ ...cfg, key }),
      });
      _log('ok', 'Configuration saved.');
      Notifications.toast('Settings saved to device.', 'success', 'Saved ✅');
      return true;
    } catch (err) {
      _log('error', `Config push failed: ${err.message}`);
      Notifications.toast('Failed to save settings to device.', 'error', 'Error');
      return false;
    }
  }

  /** Trigger a soft restart on the ESP. */
  async function restartDevice() {
    try {
      const { key } = Pairing.getPairing();
      await _apiCall('/api/restart', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ key }),
      });
      _log('info', 'Restart command sent.');
      Notifications.toast('Device restarting…', 'info', 'Restart');
    } catch {
      Notifications.toast('Could not send restart command.', 'error', 'Error');
    }
  }

  /** Trigger a factory reset on the ESP. */
  async function resetDevice() {
    try {
      const { key } = Pairing.getPairing();
      await _apiCall('/api/reset', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ key }),
      });
      _log('warn', 'Factory reset triggered.');
      Notifications.toast('Factory reset sent. Device will reboot.', 'warning', 'Reset');
    } catch {
      Notifications.toast('Could not send reset command.', 'error', 'Error');
    }
  }

  /** Trigger a WiFi credentials reset on the ESP. */
  async function resetWiFi() {
    try {
      const { key } = Pairing.getPairing();
      await _apiCall('/api/wifi-reset', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ key }),
      });
      _log('warn', 'WiFi reset triggered.');
      Notifications.toast('WiFi reset sent. Device will reboot.', 'warning', 'WiFi Reset');
    } catch {
      Notifications.toast('Could not reset WiFi.', 'error', 'Error');
    }
  }

  // ─── Online state management ──────────────────────────────────────────────

  function _setOnline(online) {
    if (state.online === online) return;
    state.online = online;
    _updateStatusDots(online);
  }

  function _updateStatusDots(online) {
    document.querySelectorAll('.device-status-dot').forEach(el => {
      el.className = `status-dot device-status-dot ${online ? 'online' : 'offline'}`;
    });
    document.querySelectorAll('.device-status-text').forEach(el => {
      el.textContent = online ? 'Online' : (state.demoMode ? 'Demo' : 'Offline');
    });
  }

  // ─── Poll loop ────────────────────────────────────────────────────────────

  async function _poll() {
    const data = await fetchStatus();
    state.lastData = data;

    _updateWeather(data.weather);
    _updateDeviceInfo(data.device);
    _updateNetwork(data.network);
    _updatePixels(data.pixels);
    _updateLED(data.leds);
    _updateStatus(data.status);
    _checkAlerts(data);
    _updateLastSeen();

    Storage.set(Storage.KEYS.LAST_SEEN, Date.now());
    Storage.appendWeatherHistory(data.weather);
  }

  function _startPolling() {
    _poll();
    state.pollTimer = setInterval(_poll, POLL_INTERVAL_MS);
  }

  function _stopPolling() {
    clearInterval(state.pollTimer);
    state.pollTimer = null;
  }

  // ─── UI updaters ──────────────────────────────────────────────────────────

  /** Safely set textContent for all matching selectors. */
  function _setText(selector, value) {
    document.querySelectorAll(selector).forEach(el => {
      el.textContent = value ?? '—';
    });
  }

  function _updateWeather(w) {
    if (!w) return;
    _setText('[data-weather="temp"]',        `${w.temp}°C`);
    _setText('[data-weather="feelsLike"]',   `${w.feelsLike}°C`);
    _setText('[data-weather="humidity"]',    `${w.humidity}%`);
    _setText('[data-weather="pressure"]',    `${w.pressure} hPa`);
    _setText('[data-weather="windSpeed"]',   `${w.windSpeed} m/s`);
    _setText('[data-weather="windDir"]',     w.windDir);
    _setText('[data-weather="aqi"]',         w.aqi);
    _setText('[data-weather="uvIndex"]',     w.uvIndex);
    _setText('[data-weather="visibility"]',  `${w.visibility} km`);
    _setText('[data-weather="condition"]',   w.condition);
    _setText('[data-weather="description"]', w.description);
    _setText('[data-weather="holiday"]',     w.holiday ? 'Holiday' : (w.weekend ? 'Weekend' : 'Weekday'));

    // Season, day of week, moon phase
    const now = new Date();
    _setText('[data-time="dayOfWeek"]', _getDayOfWeek(now));
    _setText('[data-time="season"]',    w.season || _getSeason(now));
    _setText('[data-time="moon"]',      w.moonPhase || _getMoonPhase(now));
  }

  function _updateDeviceInfo(d) {
    if (!d) return;
    _setText('[data-device="name"]',     d.name);
    _setText('[data-device="ip"]',       d.ip);
    _setText('[data-device="firmware"]', d.firmware);
    _setText('[data-device="uptime"]',   _formatUptime(d.uptime));
    _setText('[data-device="rssi"]',     `${d.rssi} dBm`);
    _setText('[data-device="freeHeap"]', _formatBytes(d.freeHeap));
    _setText('[data-device="errorCount"]', d.errorCount ?? '0');
    _setText('[data-device="devMode"]', d.devMode ? 'On' : 'Off');
    _setText('[data-device="nightMode"]', d.nightMode ? 'Night' : 'Day');
    _setText('[data-device="recovery"]', d.recovery ? 'Active' : 'Off');

    // WiFi signal bars
    document.querySelectorAll('.device-signal-bars').forEach(el => {
      _renderSignalBars(el, d.rssi);
    });

    // Demo mode badge
    document.querySelectorAll('.demo-badge').forEach(el => {
      el.classList.toggle('hidden', !d.demo);
    });
  }

  function _formatStatusLabel(value) {
    if (!value) return '—';
    return String(value)
      .replace(/-/g, ' ')
      .replace(/\b\w/g, c => c.toUpperCase());
  }

  function _updateNetwork(n) {
    if (!n) return;
    _setText('[data-network="wifiQuality"]', _formatStatusLabel(n.wifiQuality));
    _setText('[data-network="internetQuality"]', _formatStatusLabel(n.internetQuality));
    _setText('[data-network="apiStatus"]', _formatStatusLabel(n.apiStatus));
  }

  function _updatePixels(pixels) {
    if (!pixels) return;

    ['pixel1', 'pixel2'].forEach((key, idx) => {
      const p = pixels[key];
      if (!p) return;

      document.querySelectorAll(`[data-pixel="${key}"]`).forEach(el => {
        el.style.background  = p.color;
        el.style.boxShadow   = `0 0 12px 4px ${p.color}88`;
        el.title             = `${p.meaning} — ${p.reason || ''}`;
        el.setAttribute('aria-label', `Pixel ${idx + 1}: ${p.meaning}`);
      });

      _setText(`[data-pixel="${key}-meaning"]`,    p.meaning);
      _setText(`[data-pixel="${key}-colorName"]`,  p.color || '—');
      _setText(`[data-pixel="${key}-reason"]`,     p.reason || '—');
      _setText(`[data-pixel="${key}-effect"]`,     _formatStatusLabel(p.effect));
    });
  }

  function _updateLED(leds) {
    if (!leds) return;
    const state = leds.extraMode === 'Error' ? 'error'
                 : leds.animations ? 'blink' : 'on';

    document.querySelectorAll('.main-led-indicator').forEach(el => {
      el.className = `led-indicator main-led-indicator led-${state}`;
    });

    _setText('[data-led="state"]',      (leds.displayMode || 'ON').toUpperCase());
    _setText('[data-led="brightness"]', `${leds.brightness}`);

    document.querySelectorAll('[data-led="color"]').forEach(el => {
      el.style.background = leds.displayMode === 'season' ? '#ff9500' : '#00d4ff';
      el.style.boxShadow  = '0 0 8px rgba(0, 212, 255, 0.8)';
    });

    _setText('[data-leds="displayMode"]', leds.displayMode);
    _setText('[data-leds="season"]', leds.season);
    _setText('[data-leds="weather"]', leds.weather);
    _setText('[data-leds="warmMode"]', leds.warmMode);
    _setText('[data-leds="yellowMode"]', leds.yellowMode);
    _setText('[data-leds="extraMode"]', leds.extraMode);
    _setText('[data-leds="brightness"]', leds.brightness);
    _setText('[data-leds="animations"]', leds.animations ? 'On' : 'Off');
    _setText('[data-leds="buzzer"]', leds.buzzer ? 'On' : 'Off');
    _setText('[data-leds="nightMute"]', leds.nightMute ? 'On' : 'Off');
  }

  function _updateStatus(status) {
    if (!status) return;
    _setText('[data-status="updateState"]', _formatStatusLabel(status.updateState));
  }

  function _checkAlerts(data) {
    if (!data || !data.weather || !data.network) return;
    const weatherMode = String(data.weather.weatherMode || '').toLowerCase();
    const thunderstorm = weatherMode.includes('thunder');
    const heavyRain = weatherMode.includes('heavy') || weatherMode.includes('storm');
    const apiFailed = String(data.network.apiStatus).toLowerCase() === 'failed';
    const devMode = Boolean(data.device?.devMode);
    const updateState = String(data.status?.updateState || '');

    if (thunderstorm && !state.lastAlerts.thunderstorm) {
      Notifications.sendAlert(Notifications.AlertType.THUNDERSTORM);
    }
    if (heavyRain && !state.lastAlerts.heavyRain) {
      Notifications.sendAlert(Notifications.AlertType.HEAVY_RAIN);
    }
    if (apiFailed && !state.lastAlerts.apiFailure) {
      Notifications.sendAlert(Notifications.AlertType.API_FAILURE);
    }
    if (devMode && !state.lastAlerts.devMode) {
      Notifications.sendAlert(Notifications.AlertType.DEV_MODE);
    }
    if (updateState === 'success' && state.lastAlerts.updateState !== 'success') {
      Notifications.sendAlert(Notifications.AlertType.OTA_COMPLETE);
    }

    state.lastAlerts = {
      thunderstorm,
      heavyRain,
      apiFailure: apiFailed,
      devMode,
      updateState,
    };
  }

  function _updateLastSeen() {
    _setText('[data-device="lastSeen"]', new Date().toLocaleTimeString());
  }

  // ─── Astronomy / calendar helpers ─────────────────────────────────────────

  /** @param {Date} date @returns {string} */
  function _getDayOfWeek(date) {
    return ['Sunday','Monday','Tuesday','Wednesday','Thursday','Friday','Saturday'][date.getDay()];
  }

  /**
   * Calculate season based on hemisphere setting and date.
   * @param {Date} date
   * @returns {string}
   */
  function _getSeason(date) {
    const hemisphere = Storage.get(Storage.KEYS.HEMISPHERE, 'north');
    const m = date.getMonth(); // 0-based

    let season;
    if      (m >= 2 && m <= 4) season = 'Spring';
    else if (m >= 5 && m <= 7) season = 'Summer';
    else if (m >= 8 && m <= 10) season = 'Autumn';
    else                        season = 'Winter';

    if (hemisphere === 'south') {
      const flip = { Spring: 'Autumn', Summer: 'Winter', Autumn: 'Spring', Winter: 'Summer' };
      season = flip[season];
    }
    return season;
  }

  /**
   * Approximate moon phase from date (synodic period ≈ 29.53 days).
   * @param {Date} date
   * @returns {string} Phase name with emoji.
   */
  function _getMoonPhase(date) {
    const KNOWN_NEW_MOON = new Date('2000-01-06T18:14:00Z');
    // Synodic period matches firmware constant: 2551443 s / 86400 = 29.530590278 days
    const SYNODIC = 2551443 / 86400;
    const daysSince = (date - KNOWN_NEW_MOON) / 86_400_000;
    const phase = ((daysSince % SYNODIC) + SYNODIC) % SYNODIC;
    const idx = Math.floor((phase / SYNODIC) * 8) % 8;
    return [
      '🌑 New Moon', '🌒 Waxing Crescent', '🌓 First Quarter', '🌔 Waxing Gibbous',
      '🌕 Full Moon', '🌖 Waning Gibbous',  '🌗 Last Quarter',  '🌘 Waning Crescent',
    ][idx];
  }

  // ─── Format helpers ───────────────────────────────────────────────────────

  /** @param {number} seconds @returns {string} */
  function _formatUptime(seconds) {
    if (!seconds) return '—';
    const h = Math.floor(seconds / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    const s = seconds % 60;
    return `${h}h ${m}m ${s}s`;
  }

  /** @param {number} bytes @returns {string} */
  function _formatBytes(bytes) {
    if (!bytes) return '—';
    if (bytes < 1024) return `${bytes} B`;
    return `${(bytes / 1024).toFixed(1)} KB`;
  }

  /** Render WiFi signal bars into a container element. */
  function _renderSignalBars(container, rssi) {
    if (!container) return;
    const level = rssi >= -50 ? 4 : rssi >= -65 ? 3 : rssi >= -80 ? 2 : 1;
    container.innerHTML = '';
    for (let i = 1; i <= 4; i++) {
      const bar = document.createElement('div');
      bar.className = `signal-bar${i <= level ? ' active' : ''}`;
      container.appendChild(bar);
    }
  }

  // ─── Navigation ───────────────────────────────────────────────────────────

  function _initNavigation() {
    const currentPath = location.pathname.replace(/\/+$/, '') || '/';

    document.querySelectorAll('.nav-link, .sidebar-link').forEach(link => {
      const href = link.getAttribute('href');
      if (!href) return;
      const linkPath = href.replace(/\/+$/, '') || '/';
      link.classList.toggle('active', currentPath.endsWith(linkPath) && href !== '#');
    });

    // Hamburger / mobile menu
    const hamburger  = document.getElementById('hamburger');
    const mobileMenu = document.getElementById('mobile-menu');

    if (hamburger && mobileMenu) {
      hamburger.addEventListener('click', () => {
        const isOpen = mobileMenu.classList.toggle('open');
        hamburger.classList.toggle('open', isOpen);
        hamburger.setAttribute('aria-expanded', String(isOpen));
      });

      // Close on link click
      mobileMenu.querySelectorAll('.nav-link').forEach(link => {
        link.addEventListener('click', () => {
          mobileMenu.classList.remove('open');
          hamburger.classList.remove('open');
          hamburger.setAttribute('aria-expanded', 'false');
        });
      });
    }
  }

  // ─── Theme toggle ─────────────────────────────────────────────────────────

  function _initTheme() {
    const theme = Storage.getTheme();
    document.documentElement.setAttribute('data-theme', theme);

    document.querySelectorAll('[data-action="toggle-theme"]').forEach(btn => {
      btn.addEventListener('click', () => {
        const next = Storage.getTheme() === 'dark' ? 'light' : 'dark';
        Storage.setTheme(next);
        document.documentElement.setAttribute('data-theme', next);
        _setText('[data-theme-label]', next === 'dark' ? '☀️' : '🌙');
      });
    });

    _setText('[data-theme-label]', theme === 'dark' ? '☀️' : '🌙');

    const themeSelect = document.getElementById('setting-theme');
    if (themeSelect) themeSelect.value = theme;
  }

  // ─── Settings form ────────────────────────────────────────────────────────

  function _initSettings() {
    const savedSettings = Storage.getDeviceSettings();

    // Device name
    const nameInput = document.getElementById('setting-device-name');
    if (nameInput) {
      nameInput.value = Storage.get(Storage.KEYS.DEVICE_NAME, '');
      nameInput.addEventListener('change', () => {
        Storage.set(Storage.KEYS.DEVICE_NAME, nameInput.value.trim());
        _setText('[data-device="name"]', nameInput.value.trim() || 'SkyCore');
      });
    }

    // Brightness slider
    const brightnessInput = document.getElementById('setting-brightness');
    if (brightnessInput) {
      brightnessInput.value = savedSettings.brightness ?? 80;
      _setText('[data-setting="brightness"]', brightnessInput.value);
      brightnessInput.addEventListener('input', () => {
        _setText('[data-setting="brightness"]', brightnessInput.value);
        savedSettings.brightness = Number(brightnessInput.value);
        Storage.setDeviceSettings(savedSettings);
      });
    }

    const animationsSelect = document.getElementById('setting-animations');
    if (animationsSelect) {
      animationsSelect.value = String(savedSettings.animations ?? true);
      animationsSelect.addEventListener('change', () => {
        savedSettings.animations = animationsSelect.value === 'true';
        Storage.setDeviceSettings(savedSettings);
      });
    }

    const buzzerSelect = document.getElementById('setting-buzzer');
    if (buzzerSelect) {
      buzzerSelect.value = String(savedSettings.buzzer ?? true);
      buzzerSelect.addEventListener('change', () => {
        savedSettings.buzzer = buzzerSelect.value === 'true';
        Storage.setDeviceSettings(savedSettings);
      });
    }

    const nightMuteSelect = document.getElementById('setting-night-mute');
    if (nightMuteSelect) {
      nightMuteSelect.value = String(savedSettings.nightMute ?? true);
      nightMuteSelect.addEventListener('change', () => {
        savedSettings.nightMute = nightMuteSelect.value === 'true';
        Storage.setDeviceSettings(savedSettings);
      });
    }

    const holidaySelect = document.getElementById('setting-holiday');
    if (holidaySelect) {
      holidaySelect.value = String(savedSettings.holiday ?? false);
      holidaySelect.addEventListener('change', () => {
        savedSettings.holiday = holidaySelect.value === 'true';
        Storage.setDeviceSettings(savedSettings);
      });
    }

    // Hemisphere
    const hemiSelect = document.getElementById('setting-hemisphere');
    if (hemiSelect) {
      hemiSelect.value = Storage.get(Storage.KEYS.HEMISPHERE, 'north');
      hemiSelect.addEventListener('change', () => {
        Storage.set(Storage.KEYS.HEMISPHERE, hemiSelect.value);
      });
    }

    // Notification toggles
    const notifSettings = Storage.getNotifSettings();
    document.querySelectorAll('[data-notif-toggle]').forEach(checkbox => {
      const key = checkbox.getAttribute('data-notif-toggle');
      checkbox.checked = notifSettings[key] !== false;
      checkbox.addEventListener('change', () => {
        const current = Storage.getNotifSettings();
        current[key] = checkbox.checked;
        Storage.setNotifSettings(current);
      });
    });

    // Save config button
    const saveBtn = document.getElementById('btn-save-config');
    if (saveBtn) {
      saveBtn.addEventListener('click', async () => {
        const nameValue = nameInput ? nameInput.value.trim() : '';
        const cfg = {
          deviceName: nameValue || undefined,
          brightness: brightnessInput ? Number(brightnessInput.value) : undefined,
          animations: animationsSelect ? animationsSelect.value === 'true' : undefined,
          buzzer: buzzerSelect ? buzzerSelect.value === 'true' : undefined,
          nightMute: nightMuteSelect ? nightMuteSelect.value === 'true' : undefined,
          holiday: holidaySelect ? holidaySelect.value === 'true' : undefined,
        };
        await pushConfig(cfg);
      });
    }

    // Restart / Reset
    document.getElementById('btn-restart')?.addEventListener('click', async () => {
      if (confirm('Restart the ESP device?')) await restartDevice();
    });

    document.getElementById('btn-reset-wifi')?.addEventListener('click', async () => {
      if (confirm('Reset WiFi credentials? Device will reboot.')) await resetWiFi();
    });

    document.getElementById('btn-factory-reset')?.addEventListener('click', async () => {
      if (confirm('⚠️ This will erase all device settings. Continue?')) await resetDevice();
    });

    // Re-pair
    document.getElementById('btn-repair')?.addEventListener('click', async () => {
      Pairing.clearPairing();
      await Pairing.showUI();
      _initBaseURL();
      _stopPolling();
      _startPolling();
    });

    // Clear storage
    document.getElementById('btn-clear-storage')?.addEventListener('click', () => {
      if (confirm('Clear all local dashboard data?')) {
        Storage.clear();
        location.reload();
      }
    });

    const themeSelect = document.getElementById('setting-theme');
    if (themeSelect) {
      themeSelect.value = Storage.getTheme();
      themeSelect.addEventListener('change', () => {
        const next = themeSelect.value;
        Storage.setTheme(next);
        document.documentElement.setAttribute('data-theme', next);
      });
    }
  }

  // ─── Log viewer ───────────────────────────────────────────────────────────

  /** @param {'info'|'warn'|'error'|'ok'|'debug'} level */
  function _log(level, message) {
    const entry = { level, message, ts: Date.now() };
    Storage.appendLog(entry);
    _renderLogEntry(entry);
    console[level === 'error' ? 'error' : level === 'warn' ? 'warn' : 'log'](
      `[SkyCore ${level.toUpperCase()}]`, message
    );
  }

  function _renderLogEntry(entry) {
    const tbody = document.getElementById('log-tbody');
    if (!tbody) return;

    const tr = document.createElement('tr');
    tr.innerHTML = `
      <td class="log-timestamp">${new Date(entry.ts).toLocaleTimeString()}</td>
      <td class="log-level-${entry.level}" style="text-transform:uppercase;font-weight:700">${entry.level}</td>
      <td>${_esc(entry.message)}</td>
    `;
    tbody.prepend(tr);

    // Keep table at 100 rows max
    while (tbody.rows.length > 100) tbody.deleteRow(-1);
  }

  function _loadLogHistory() {
    Storage.getLogHistory().slice(-50).reverse().forEach(_renderLogEntry);
  }

  function _esc(str) {
    return String(str)
      .replace(/&/g, '&amp;').replace(/</g, '&lt;')
      .replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  }

  // ─── Copy to clipboard ────────────────────────────────────────────────────

  /**
   * Copy text to clipboard; shows a toast on result.
   * @param {string} text
   * @returns {Promise<void>}
   */
  async function copyToClipboard(text) {
    try {
      await navigator.clipboard.writeText(text);
      Notifications.toast('Copied to clipboard.', 'success', '', 2500);
    } catch {
      Notifications.toast('Clipboard access denied.', 'warning');
    }
  }

  function _initCopyButtons() {
    document.querySelectorAll('[data-copy]').forEach(btn => {
      btn.addEventListener('click', () => {
        const selector = btn.getAttribute('data-copy');
        const source   = document.querySelector(selector);
        const text     = source ? (source.getAttribute('data-raw') || source.value || source.textContent).trim() : '';
        if (text) copyToClipboard(text);
      });
    });

    // Pairing key copy
    document.getElementById('btn-copy-key')?.addEventListener('click', () => {
      const { key } = Pairing.getPairing();
      if (key) copyToClipboard(key);
    });
  }

  // ─── ESP base URL ─────────────────────────────────────────────────────────

  function _initBaseURL() {
    const { espIP } = Pairing.getPairing();
    state.espBaseURL = espIP ? `http://${espIP}` : '';
    state.demoMode   = Storage.get(Storage.KEYS.DEMO_MODE, false) || !espIP;
  }

  // ─── Firmware version display ─────────────────────────────────────────────

  function _updateFirmwareDisplay(version) {
    _setText('[data-device="firmware"]', version || '—');
  }

  // ─── Pixel legend ─────────────────────────────────────────────────────────

  function _initPixelLegend() {
    if (global.PixelLegend) {
      PixelLegend.renderAll();
    }
  }

  function _initPairingPage() {
    if (global.Pairing && typeof global.Pairing.initPage === 'function') {
      global.Pairing.initPage();
    }
  }

  function _initNotificationsPage() {
    document.getElementById('btn-request-notifications')?.addEventListener('click', () => {
      Notifications.request();
    });
  }

  function _initFirmwareViewer() {
    if (global.FirmwareViewer) {
      global.FirmwareViewer.loadFirmware();
    }
  }

  function _initCharts() {
    const canvas = document.getElementById('history-chart');
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    const history = Storage.getWeatherHistory().slice(-40);
    if (!history.length) return;

    const temps = history.map(h => h.temp ?? 0);
    const hums = history.map(h => h.humidity ?? 0);
    const maxTemp = Math.max(...temps, 30);
    const minTemp = Math.min(...temps, 0);
    const maxHum = Math.max(...hums, 100);

    const width = canvas.width;
    const height = canvas.height;
    ctx.clearRect(0, 0, width, height);
    ctx.fillStyle = 'rgba(0,0,0,0.4)';
    ctx.fillRect(0, 0, width, height);

    function plot(values, color, maxVal, minVal = 0) {
      ctx.strokeStyle = color;
      ctx.lineWidth = 2;
      ctx.beginPath();
      values.forEach((val, i) => {
        const x = (i / (values.length - 1)) * (width - 40) + 20;
        const y = height - 20 - ((val - minVal) / (maxVal - minVal)) * (height - 40);
        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
      });
      ctx.stroke();
    }

    plot(temps, '#00d4ff', maxTemp, minTemp);
    plot(hums, '#00ff88', maxHum, 0);
  }

  // ─── Demo mode banner ────────────────────────────────────────────────────

  function _initDemoBanner() {
    const banner = document.getElementById('demo-banner');
    if (!banner) return;
    banner.classList.toggle('hidden', !state.demoMode);

    document.getElementById('btn-exit-demo')?.addEventListener('click', async () => {
      Storage.remove(Storage.KEYS.DEMO_MODE);
      Pairing.clearPairing();
      await Pairing.showUI();
      _initBaseURL();
      banner.classList.toggle('hidden', !state.demoMode);
      _stopPolling();
      _startPolling();
    });
  }

  // ─── Boot sequence ────────────────────────────────────────────────────────

  async function _boot() {
    _log('info', 'SkyCore dashboard initialising…');

    // 1. Onboarding & signup
    if (global.Onboarding && typeof global.Onboarding.init === 'function') {
      await global.Onboarding.init();
    }

    // 2. Pairing (shows modal on first visit)
    await Pairing.init();

    // 3. Resolve ESP address and demo mode
    _initBaseURL();

    // 4. Notification permission (non-blocking)
    Notifications.request().catch(() => {});

    // 5. Theme
    _initTheme();

    // 6. Navigation
    _initNavigation();

    // 7. Settings form
    _initSettings();

    // 8. Copy buttons
    _initCopyButtons();

    // 9. Pixel legend
    _initPixelLegend();

    // 10. Log history
    _loadLogHistory();

    // 11. Demo banner
    _initDemoBanner();

    // 12. Pairing page handler
    _initPairingPage();

    // 13. Notifications page handler
    _initNotificationsPage();

    // 14. Firmware viewer
    _initFirmwareViewer();

    // 15. History charts
    _initCharts();

    // 16. Start polling
    _startPolling();

    _log('ok', `Dashboard ready. Mode: ${state.demoMode ? 'DEMO' : 'LIVE'}`);
  }

  // ─── Expose public helpers ────────────────────────────────────────────────

  global.App = {
    fetchStatus,
    pairDevice,
    pushConfig,
    restartDevice,
    resetDevice,
    resetWiFi,
    copyToClipboard,
    getMoonPhase: _getMoonPhase,
    getSeason:    _getSeason,
    getDayOfWeek: _getDayOfWeek,
    getState: () => ({ ...state }),
  };

  // ─── Entry point ──────────────────────────────────────────────────────────

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', _boot);
  } else {
    _boot();
  }

}(window));
