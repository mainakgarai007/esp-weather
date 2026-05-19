/**
 * pairing.js — SkyCore IoT Dashboard
 * Manages pairing between the GitHub Pages dashboard and a physical ESP device.
 *
 * Key format: SKY-XXXX-XXXX  (uppercase alphanumeric segments)
 *
 * Public API (attached to window.Pairing):
 *   Pairing.generateKey()
 *   Pairing.savePairing(key, espIP)
 *   Pairing.getPairing()
 *   Pairing.isPaired()
 *   Pairing.clearPairing()
 *   Pairing.validateKey(key)
 *   Pairing.showUI()
 *   Pairing.init()
 */

(function (global) {
  'use strict';

  const CHARSET = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789'; // no 0/O/1/I ambiguity
  const KEY_REGEX = /^SKY-[A-Z0-9]{4}-[A-Z0-9]{4}$/;

  // ─── Key helpers ─────────────────────────────────────────────────────────

  /** Generate a random segment of `len` characters from CHARSET. */
  function _randomSegment(len) {
    let out = '';
    const array = new Uint8Array(len);
    crypto.getRandomValues(array);
    for (let i = 0; i < len; i++) {
      out += CHARSET[array[i] % CHARSET.length];
    }
    return out;
  }

  /**
   * Generate a fresh unique pairing key.
   * @returns {string} e.g. "SKY-A3BX-K9QR"
   */
  function generateKey() {
    return `SKY-${_randomSegment(4)}-${_randomSegment(4)}`;
  }

  /**
   * Validate a pairing key string.
   * @param {string} key
   * @returns {boolean}
   */
  function validateKey(key) {
    return typeof key === 'string' && KEY_REGEX.test(key.trim().toUpperCase());
  }

  // ─── Persistence ─────────────────────────────────────────────────────────

  /**
   * Save pairing information to localStorage.
   * @param {string} key  - SKY-XXXX-XXXX key shared with the ESP device.
   * @param {string} espIP - IP address of the ESP device (e.g. "192.168.1.42").
   * @returns {boolean} false when the key format is invalid.
   */
  function savePairing(key, espIP) {
    const trimmedKey = (key || '').trim().toUpperCase();
    if (!validateKey(trimmedKey)) {
      console.warn('[Pairing] Invalid key format:', key);
      return false;
    }

    const S = global.Storage;
    S.set(S.KEYS.PAIRING_KEY,      trimmedKey);
    S.set(S.KEYS.PAIRING_ESP_IP,   (espIP || '').trim());
    S.set(S.KEYS.PAIRING_VERIFIED, false); // verification happens in app.js
    return true;
  }

  /**
   * Retrieve stored pairing data.
   * @returns {{ key: string|null, espIP: string|null, verified: boolean }}
   */
  function getPairing() {
    const S = global.Storage;
    return {
      key:      S.get(S.KEYS.PAIRING_KEY,      null),
      espIP:    S.get(S.KEYS.PAIRING_ESP_IP,   null),
      verified: S.get(S.KEYS.PAIRING_VERIFIED, false),
    };
  }

  /**
   * @returns {boolean} true when a key and IP are both stored.
   */
  function isPaired() {
    const { key, espIP } = getPairing();
    return Boolean(key && espIP);
  }

  /** Remove all pairing data from localStorage. */
  function clearPairing() {
    const S = global.Storage;
    S.remove(S.KEYS.PAIRING_KEY);
    S.remove(S.KEYS.PAIRING_ESP_IP);
    S.remove(S.KEYS.PAIRING_VERIFIED);
  }

  // ─── UI ──────────────────────────────────────────────────────────────────

  /**
   * Inject and display the pairing modal.
   * Resolves when the user successfully submits valid pairing data.
   * @returns {Promise<{ key: string, espIP: string }>}
   */
  function showUI() {
    return new Promise((resolve) => {
      // Remove any stale modal
      const existing = document.getElementById('pairing-modal-backdrop');
      if (existing) existing.remove();

      const generatedKey = generateKey();

      const backdrop = document.createElement('div');
      backdrop.id = 'pairing-modal-backdrop';
      backdrop.className = 'modal-backdrop';
      backdrop.innerHTML = `
        <div class="modal" role="dialog" aria-modal="true" aria-labelledby="pairing-title">
          <div class="modal-header">
            <h2 class="modal-title" id="pairing-title">⚡ Pair Your ESP Device</h2>
          </div>
          <div class="modal-body">
            <p class="text-dim" style="margin-bottom:var(--space-lg)">
              Enter the IP address of your ESP device and copy the pairing key below
              into the ESP firmware configuration.
            </p>

            <div class="form-group" style="margin-bottom:var(--space-md)">
              <label class="form-label" for="pair-ip">ESP Device IP Address</label>
              <input
                class="form-input"
                id="pair-ip"
                type="text"
                inputmode="decimal"
                placeholder="e.g. 192.168.1.42"
                autocomplete="off"
                spellcheck="false"
              />
              <span class="form-hint">Find it in your router's device list or the ESP Serial Monitor.</span>
            </div>

            <div class="form-group" style="margin-bottom:var(--space-md)">
              <label class="form-label" for="pair-key">Pairing Key</label>
              <div style="display:flex;gap:var(--space-sm);align-items:center">
                <input
                  class="form-input text-mono"
                  id="pair-key"
                  type="text"
                  value="${generatedKey}"
                  placeholder="SKY-XXXX-XXXX"
                  style="letter-spacing:0.12em;text-transform:uppercase"
                  spellcheck="false"
                />
                <button class="btn btn-ghost btn-icon" id="pair-key-regen" title="Generate new key"
                  style="flex-shrink:0" aria-label="Regenerate key">↺</button>
              </div>
              <span class="form-hint">Copy this key into your ESP firmware (PAIRING_KEY constant).</span>
            </div>

            <div id="pair-error" class="form-error hidden" aria-live="polite"></div>
          </div>
          <div class="modal-footer">
            <button class="btn btn-ghost" id="pair-skip">Skip (Demo mode)</button>
            <button class="btn btn-primary" id="pair-submit">Pair Device →</button>
          </div>
        </div>
      `;

      document.body.appendChild(backdrop);

      const ipInput   = backdrop.querySelector('#pair-ip');
      const keyInput  = backdrop.querySelector('#pair-key');
      const regenBtn  = backdrop.querySelector('#pair-key-regen');
      const errorEl   = backdrop.querySelector('#pair-error');
      const submitBtn = backdrop.querySelector('#pair-submit');
      const skipBtn   = backdrop.querySelector('#pair-skip');

      function showError(msg) {
        errorEl.textContent = msg;
        errorEl.classList.remove('hidden');
      }
      function clearError() {
        errorEl.textContent = '';
        errorEl.classList.add('hidden');
      }

      regenBtn.addEventListener('click', () => {
        keyInput.value = generateKey();
        clearError();
      });

      keyInput.addEventListener('input', () => {
        keyInput.value = keyInput.value.toUpperCase();
        clearError();
      });

      submitBtn.addEventListener('click', () => {
        clearError();
        const ip  = ipInput.value.trim();
        const key = keyInput.value.trim().toUpperCase();

        if (!ip) {
          showError('Please enter the ESP device IP address.');
          ipInput.focus();
          return;
        }

        if (!validateKey(key)) {
          showError('Key must be in SKY-XXXX-XXXX format (letters & numbers only).');
          keyInput.focus();
          return;
        }

        if (savePairing(key, ip)) {
          backdrop.remove();
          resolve({ key, espIP: ip });
        }
      });

      skipBtn.addEventListener('click', () => {
        const S = global.Storage;
        S.set(S.KEYS.DEMO_MODE, true);
        backdrop.remove();
        resolve({ key: null, espIP: null });
      });

      // Auto-focus IP field
      setTimeout(() => ipInput.focus(), 80);
    });
  }

  // ─── Initialisation ──────────────────────────────────────────────────────

  /**
   * Called once on page load.
   * Shows the pairing UI on first visit (or when not yet paired).
   * @returns {Promise<void>}
   */
  async function init() {
    if (!global.Storage) {
      console.error('[Pairing] Storage module must be loaded first.');
      return;
    }

    const demoMode = global.Storage.get(global.Storage.KEYS.DEMO_MODE, false);
    if (demoMode || isPaired()) return;

    await showUI();
  }

  // ─── Public API ──────────────────────────────────────────────────────────
  global.Pairing = {
    generateKey,
    validateKey,
    savePairing,
    getPairing,
    isPaired,
    clearPairing,
    showUI,
    init,
  };

}(window));
