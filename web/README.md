# Web flasher

Static page deployed to GitHub Pages by the `pages` job of the CI workflow, after the format
check, the unit tests and the firmware build have all passed on `main`.

- `index.html`: the page, using [ESP Web Tools](https://esphome.github.io/esp-web-tools/) to
  flash over Web Serial (Chromium based browsers only).
- `manifest.json`: ESP Web Tools manifest. `__VERSION__` is replaced by `git describe` in CI.
  The three firmware parts are copied from the build artifact into `firmware/`.
- `build-info.json` is generated in CI (version, commit, date) and shown on the page.

Local preview after `pio run -e esp32s3`:

```bash
mkdir -p /tmp/site/firmware
cp web/index.html /tmp/site/
sed 's/__VERSION__/local/' web/manifest.json > /tmp/site/manifest.json
cp .pio/build/esp32s3/{bootloader,partitions,firmware}.bin /tmp/site/firmware/
python3 -m http.server -d /tmp/site 8000     # Web Serial needs localhost or HTTPS
```
