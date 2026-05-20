# SkyCore / LightOS Core — Deployment Guide

## GitHub Pages Deployment

SkyCore’s web dashboard is static and GitHub Pages–ready. The site is located in the `/docs` folder.

### Steps

1. Push your repository to GitHub.
2. Go to **Settings → Pages**.
3. Under **Source**, choose:
   - **Branch**: `main`
   - **Folder**: `/docs`
4. Save.

Your dashboard will be available at:

```
https://<your-username>.github.io/<repo-name>/
```

## Firmware Download

The firmware download link is served from:

```
/docs/firmware/SkyCore.ino
```

If you update the firmware, copy the `.ino` file into `/docs/firmware/` to keep the download link current.

## PWA Notes

- The dashboard includes a PWA manifest and service worker.
- Offline support caches key pages, scripts, styles, and the firmware file.
- Installable on mobile by using “Add to Home Screen”.
