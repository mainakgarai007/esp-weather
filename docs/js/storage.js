/**
 * storage.js — SkyCore IoT Dashboard
 * Centralised localStorage manager with versioning and migration support.
 *
 * Usage:
 *   Storage.get(Storage.KEYS.THEME)
 *   Storage.set(Storage.KEYS.DEVICE_NAME, 'My Sensor')
 *   Storage.remove(Storage.KEYS.PAIRING)
 *   Storage.clear()
 */

(function (global) {
  'use strict';

  /** Current schema version — bump to trigger migration. */
  const SCHEMA_VERSION = 1;

  /** Namespace prefix to avoid collisions with other sites on GitHub Pages. */
  const NS = 'skycore_';

  /** All storage keys, namespaced and typed. */
  const KEYS = Object.freeze({
    SCHEMA_VERSION:   NS + 'schema_version',
    THEME:            NS + 'theme',
    PAIRING_KEY:      NS + 'pairing_key',
    PAIRING_ESP_IP:   NS + 'pairing_esp_ip',
    PAIRING_VERIFIED: NS + 'pairing_verified',
    DEVICE_NAME:      NS + 'device_name',
    DEVICE_SETTINGS:  NS + 'device_settings',
    WEATHER_HISTORY:  NS + 'weather_history',
    NOTIF_SETTINGS:   NS + 'notif_settings',
    NOTIF_HISTORY:    NS + 'notif_history',
    LOG_HISTORY:      NS + 'log_history',
    DEMO_MODE:        NS + 'demo_mode',
    HEMISPHERE:       NS + 'hemisphere',
    LAST_SEEN:        NS + 'last_seen',
  });

  // ─── Core helpers ────────────────────────────────────────────────────────

  /**
   * Read and JSON-parse a value from localStorage.
   * @param {string} key - A value from KEYS.
   * @param {*} [fallback=null] - Returned when key is absent or parse fails.
   * @returns {*}
   */
  function get(key, fallback = null) {
    try {
      const raw = localStorage.getItem(key);
      if (raw === null) return fallback;
      return JSON.parse(raw);
    } catch {
      return fallback;
    }
  }

  /**
   * JSON-stringify and write a value to localStorage.
   * @param {string} key
   * @param {*} value
   * @returns {boolean} true on success.
   */
  function set(key, value) {
    try {
      localStorage.setItem(key, JSON.stringify(value));
      return true;
    } catch (err) {
      console.warn('[Storage] set failed:', key, err);
      return false;
    }
  }

  /**
   * Remove a single key from localStorage.
   * @param {string} key
   */
  function remove(key) {
    try {
      localStorage.removeItem(key);
    } catch (err) {
      console.warn('[Storage] remove failed:', key, err);
    }
  }

  /**
   * Remove ALL SkyCore keys from localStorage (leaves foreign keys untouched).
   */
  function clear() {
    try {
      Object.values(KEYS).forEach(k => localStorage.removeItem(k));
    } catch (err) {
      console.warn('[Storage] clear failed:', err);
    }
  }

  // ─── Migration ───────────────────────────────────────────────────────────

  /**
   * Run once on module load.
   * Compares the stored schema version and applies any needed migrations.
   */
  function _runMigrations() {
    const stored = get(KEYS.SCHEMA_VERSION, 0);
    if (stored === SCHEMA_VERSION) return;

    if (stored < 1) {
      // v0 → v1: no-op (first install)
    }

    set(KEYS.SCHEMA_VERSION, SCHEMA_VERSION);
  }

  // ─── Typed convenience helpers ───────────────────────────────────────────

  /** @returns {'dark'|'light'} */
  function getTheme() {
    return get(KEYS.THEME, 'dark');
  }

  /** @param {'dark'|'light'} theme */
  function setTheme(theme) {
    set(KEYS.THEME, theme);
  }

  /**
   * Append an entry to a capped history array.
   * @param {string} key - A KEYS value for the history array.
   * @param {Object} entry
   * @param {number} [maxLength=200]
   */
  function appendHistory(key, entry, maxLength = 200) {
    const arr = get(key, []);
    arr.push({ ...entry, ts: Date.now() });
    if (arr.length > maxLength) arr.splice(0, arr.length - maxLength);
    set(key, arr);
  }

  /** @returns {Object[]} */
  function getWeatherHistory() {
    return get(KEYS.WEATHER_HISTORY, []);
  }

  /** @param {Object} reading */
  function appendWeatherHistory(reading) {
    appendHistory(KEYS.WEATHER_HISTORY, reading, 500);
  }

  /** @returns {Object[]} */
  function getNotifHistory() {
    return get(KEYS.NOTIF_HISTORY, []);
  }

  /** @param {Object} notif */
  function appendNotifHistory(notif) {
    appendHistory(KEYS.NOTIF_HISTORY, notif, 100);
  }

  /** @returns {Object[]} */
  function getLogHistory() {
    return get(KEYS.LOG_HISTORY, []);
  }

  /** @param {Object} entry */
  function appendLog(entry) {
    appendHistory(KEYS.LOG_HISTORY, entry, 300);
  }

  // ─── Default notification settings ──────────────────────────────────────

  const DEFAULT_NOTIF_SETTINGS = Object.freeze({
    enabled:         true,
    thunderstorm:    true,
    heavyRain:       true,
    deviceOffline:   true,
    otaComplete:     true,
    devModeEnabled:  true,
    apiFailure:      true,
  });

  /** @returns {Object} */
  function getNotifSettings() {
    return { ...DEFAULT_NOTIF_SETTINGS, ...get(KEYS.NOTIF_SETTINGS, {}) };
  }

  /** @param {Object} settings */
  function setNotifSettings(settings) {
    set(KEYS.NOTIF_SETTINGS, settings);
  }

  // ─── Run on load ─────────────────────────────────────────────────────────
  _runMigrations();

  // ─── Public API ──────────────────────────────────────────────────────────
  const Storage = {
    KEYS,
    get,
    set,
    remove,
    clear,
    getTheme,
    setTheme,
    appendWeatherHistory,
    getWeatherHistory,
    appendNotifHistory,
    getNotifHistory,
    appendLog,
    getLogHistory,
    getNotifSettings,
    setNotifSettings,
  };

  // Attach to window so other scripts can access it without ES modules.
  global.Storage = Storage;

}(window));
