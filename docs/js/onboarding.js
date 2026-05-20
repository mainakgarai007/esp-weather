/**
 * onboarding.js — SkyCore onboarding + sign-up flow
 */
(function (global) {
  'use strict';

  async function init() {
    if (!global.Storage) return;
    if (global.Storage.get(global.Storage.KEYS.ONBOARDING_DONE, false)) return;
    await showOnboarding();
  }

  function showOnboarding() {
    return new Promise((resolve) => {
      const existing = document.getElementById('onboarding-backdrop');
      if (existing) existing.remove();

      const backdrop = document.createElement('div');
      backdrop.id = 'onboarding-backdrop';
      backdrop.className = 'modal-backdrop';
      backdrop.innerHTML = `
        <div class="modal" role="dialog" aria-modal="true" aria-labelledby="onboarding-title">
          <div class="modal-header">
            <h2 class="modal-title" id="onboarding-title">Welcome to SkyCore</h2>
          </div>
          <div class="modal-body">
            <div class="onboarding-step" data-step="1">
              <p class="text-dim">SkyCore / LightOS Core is a futuristic ambient OS that turns weather, seasons, and device health into living light.</p>
            </div>
            <div class="onboarding-step hidden" data-step="2">
              <p class="text-dim">Your device silently communicates:</p>
              <ul class="text-dim" style="margin-top:var(--space-sm);display:grid;gap:6px">
                <li>🌦️ Weather, rain, heat, storms</li>
                <li>🌗 Moon phase, day/night</li>
                <li>📅 Weekend + holiday status</li>
                <li>📡 WiFi, internet, API health</li>
              </ul>
            </div>
            <div class="onboarding-step hidden" data-step="3">
              <p class="text-dim">Create your local SkyCore profile.</p>
              <div class="form-group">
                <label class="form-label" for="signup-name">Username</label>
                <input class="form-input" id="signup-name" type="text" placeholder="SkyCoreUser" />
              </div>
              <div class="form-group">
                <label class="form-label" for="signup-pass">Password</label>
                <input class="form-input" id="signup-pass" type="password" placeholder="••••••••" />
              </div>
              <div class="form-group">
                <label class="form-label" for="signup-email">Email (optional)</label>
                <input class="form-input" id="signup-email" type="email" placeholder="you@example.com" />
              </div>
              <div class="glass-card" style="padding:var(--space-md);margin-top:var(--space-md)">
                <p class="text-xs text-muted">Your Device Key</p>
                <div class="flex-between flex-wrap gap-sm">
                  <span class="text-mono" id="signup-key"></span>
                  <button class="btn btn-ghost" id="signup-copy">Copy Key</button>
                </div>
                <p class="text-xs text-muted" style="margin-top:var(--space-sm)">Paste this key into the ESP setup portal.</p>
              </div>
            </div>
          </div>
          <div class="modal-footer flex-between">
            <button class="btn btn-ghost" id="onboarding-back">Back</button>
            <div class="flex gap-sm">
              <button class="btn btn-ghost" id="onboarding-skip">Skip</button>
              <button class="btn btn-primary" id="onboarding-next">Next</button>
            </div>
          </div>
        </div>
      `;

      document.body.appendChild(backdrop);

      const steps = Array.from(backdrop.querySelectorAll('.onboarding-step'));
      const nextBtn = backdrop.querySelector('#onboarding-next');
      const backBtn = backdrop.querySelector('#onboarding-back');
      const skipBtn = backdrop.querySelector('#onboarding-skip');
      const keyEl = backdrop.querySelector('#signup-key');
      const copyBtn = backdrop.querySelector('#signup-copy');

      let step = 1;
      const total = steps.length;

      const pairingKey = global.Pairing ? global.Pairing.getOrCreateKey() : 'SKY-XXXX-XXXX';
      if (keyEl) keyEl.textContent = pairingKey;

      function render() {
        steps.forEach(s => s.classList.add('hidden'));
        const active = steps.find(s => Number(s.dataset.step) === step);
        if (active) active.classList.remove('hidden');
        backBtn.disabled = step === 1;
        nextBtn.textContent = step === total ? 'Finish' : 'Next';
      }

      async function hashPassword(pass) {
        if (!pass) return '';
        if (crypto?.subtle) {
          const buf = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(pass));
          return Array.from(new Uint8Array(buf)).map(b => b.toString(16).padStart(2, '0')).join('');
        }
        return btoa(pass);
      }

      async function finish() {
        const name = backdrop.querySelector('#signup-name')?.value.trim() || 'SkyCore User';
        const email = backdrop.querySelector('#signup-email')?.value.trim();
        const pass = backdrop.querySelector('#signup-pass')?.value || '';
        const profile = {
          name,
          email,
          passwordHash: await hashPassword(pass),
          createdAt: new Date().toISOString(),
          deviceKey: pairingKey,
        };
        global.Storage.setAccountProfile(profile);
        global.Storage.set(global.Storage.KEYS.ONBOARDING_DONE, true);
        backdrop.remove();
        resolve();
      }

      nextBtn.addEventListener('click', async () => {
        if (step < total) {
          step += 1;
          render();
        } else {
          await finish();
        }
      });

      backBtn.addEventListener('click', () => {
        if (step > 1) {
          step -= 1;
          render();
        }
      });

      skipBtn.addEventListener('click', () => {
        global.Storage.set(global.Storage.KEYS.ONBOARDING_DONE, true);
        backdrop.remove();
        resolve();
      });

      copyBtn?.addEventListener('click', () => {
        if (global.App) global.App.copyToClipboard(pairingKey);
      });

      render();
    });
  }

  global.Onboarding = { init };
}(window));
