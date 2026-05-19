/**
 * notifications.js — SkyCore IoT Dashboard
 * Browser Notification + Toast fallback system.
 *
 * Public API (window.Notifications):
 *   Notifications.request()
 *   Notifications.send(title, body, icon, tag)
 *   Notifications.sendAlert(type, data)
 *   Notifications.toast(message, level, title, duration)
 *   Notifications.AlertType  (enum object)
 */

(function (global) {
  'use strict';

  // ─── Alert type enum ─────────────────────────────────────────────────────

  /** @enum {string} */
  const AlertType = Object.freeze({
    THUNDERSTORM:   'thunderstorm',
    HEAVY_RAIN:     'heavyRain',
    DEVICE_OFFLINE: 'deviceOffline',
    OTA_COMPLETE:   'otaComplete',
    DEV_MODE:       'devModeEnabled',
    API_FAILURE:    'apiFailure',
  });

  /** Default notification payloads keyed by AlertType. */
  const ALERT_DEFAULTS = {
    [AlertType.THUNDERSTORM]:   { title: '⚡ Thunderstorm Alert',        body: 'Severe thunderstorm detected in your area.',       icon: '⛈️',  tag: 'thunderstorm'  },
    [AlertType.HEAVY_RAIN]:     { title: '🌧️ Heavy Rain Warning',        body: 'Heavy rainfall is currently being recorded.',       icon: '🌧️', tag: 'heavy-rain'    },
    [AlertType.DEVICE_OFFLINE]: { title: '📡 Device Offline',            body: 'Your ESP device is not responding.',                icon: '❌',  tag: 'device-status' },
    [AlertType.OTA_COMPLETE]:   { title: '✅ OTA Update Complete',        body: 'Firmware updated successfully. Device restarting.', icon: '🔄',  tag: 'ota'           },
    [AlertType.DEV_MODE]:       { title: '🛠️ Developer Mode Enabled',    body: 'Advanced debug mode is now active.',                icon: '🛠️', tag: 'dev-mode'      },
    [AlertType.API_FAILURE]:    { title: '⚠️ Weather API Failure',       body: 'Could not reach the weather data API.',             icon: '⚠️',  tag: 'api-failure'   },
  };

  // ─── Toast container (created lazily) ───────────────────────────────────

  let _toastContainer = null;

  function _getToastContainer() {
    if (_toastContainer && document.body.contains(_toastContainer)) {
      return _toastContainer;
    }
    _toastContainer = document.createElement('div');
    _toastContainer.className = 'toast-container';
    _toastContainer.setAttribute('aria-live', 'polite');
    _toastContainer.setAttribute('aria-atomic', 'false');
    document.body.appendChild(_toastContainer);
    return _toastContainer;
  }

  // ─── Toast helper ────────────────────────────────────────────────────────

  /**
   * Show an in-page toast notification.
   * @param {string} message
   * @param {'info'|'success'|'warning'|'error'} [level='info']
   * @param {string} [title='']
   * @param {number} [duration=5000] ms before auto-dismiss (0 = no auto-dismiss)
   */
  function toast(message, level = 'info', title = '', duration = 5000) {
    const icons = { info: 'ℹ️', success: '✅', warning: '⚠️', error: '❌' };
    const icon  = icons[level] || icons.info;

    const el = document.createElement('div');
    el.className = `toast toast-${level}`;
    el.setAttribute('role', 'alert');
    el.innerHTML = `
      <span class="toast-icon" aria-hidden="true">${icon}</span>
      <div class="toast-body">
        ${title ? `<div class="toast-title">${_esc(title)}</div>` : ''}
        <div class="toast-message">${_esc(message)}</div>
      </div>
      <button class="toast-close" aria-label="Dismiss notification">✕</button>
    `;

    // Optional auto-shrink progress bar
    if (duration > 0) {
      const bar = document.createElement('div');
      bar.className = 'toast-progress';
      bar.style.animationDuration = `${duration}ms`;
      bar.style.color = level === 'error' ? 'var(--color-danger)'
                       : level === 'success' ? 'var(--color-accent)'
                       : level === 'warning' ? 'var(--color-warning)'
                       : 'var(--color-primary)';
      el.appendChild(bar);
    }

    const container = _getToastContainer();
    container.appendChild(el);

    function dismiss() {
      el.style.opacity = '0';
      el.style.transform = 'translateY(12px)';
      el.style.transition = 'opacity 0.2s, transform 0.2s';
      setTimeout(() => el.remove(), 220);
    }

    el.querySelector('.toast-close').addEventListener('click', dismiss);
    if (duration > 0) setTimeout(dismiss, duration);

    return { dismiss };
  }

  /** Simple HTML escaper to prevent XSS in toast content. */
  function _esc(str) {
    return String(str)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  }

  // ─── Browser Notification permission ────────────────────────────────────

  /**
   * Request browser notification permission.
   * @returns {Promise<NotificationPermission>}
   */
  async function request() {
    if (!('Notification' in window)) {
      toast('Browser notifications are not supported. Toasts will be used instead.', 'warning');
      return 'denied';
    }
    if (Notification.permission === 'granted') return 'granted';
    if (Notification.permission === 'denied')  return 'denied';

    const permission = await Notification.requestPermission();
    if (permission === 'granted') {
      toast('Browser notifications enabled.', 'success');
    } else {
      toast('Notification permission denied — using in-page toasts instead.', 'info');
    }
    return permission;
  }

  // ─── Core send ───────────────────────────────────────────────────────────

  /**
   * Send a browser notification, falling back to a toast.
   * @param {string} title
   * @param {string} body
   * @param {string} [icon='']
   * @param {string} [tag='skycore']
   */
  function send(title, body, icon = '', tag = 'skycore') {
    _recordHistory({ title, body, tag, ts: Date.now() });

    if ('Notification' in window && Notification.permission === 'granted') {
      try {
        const n = new Notification(title, { body, icon, tag });
        n.onclick = () => window.focus();
        return;
      } catch {
        // Fall through to toast
      }
    }

    // Toast fallback
    toast(body, 'info', title);
  }

  /** Persist notification to history (capped at 100). */
  function _recordHistory(entry) {
    if (global.Storage) {
      global.Storage.appendNotifHistory(entry);
    }
  }

  // ─── Typed alerts ────────────────────────────────────────────────────────

  /**
   * Send a well-known alert type, respecting per-type settings.
   * @param {string} type - One of AlertType values.
   * @param {Object} [data={}] - Override title/body/icon.
   */
  function sendAlert(type, data = {}) {
    const settings = global.Storage ? global.Storage.getNotifSettings() : { enabled: true };
    if (!settings.enabled) return;
    if (settings[type] === false) return;

    const defaults = ALERT_DEFAULTS[type];
    if (!defaults) {
      console.warn('[Notifications] Unknown alert type:', type);
      return;
    }

    const title = data.title || defaults.title;
    const body  = data.body  || defaults.body;
    const icon  = data.icon  || defaults.icon;
    const tag   = data.tag   || defaults.tag;

    send(title, body, icon, tag);
  }

  // ─── Public API ──────────────────────────────────────────────────────────
  global.Notifications = {
    AlertType,
    request,
    send,
    sendAlert,
    toast,
  };

}(window));
