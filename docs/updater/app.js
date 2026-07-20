/* NitroTron3 firmware updater — page logic.
   DFU-over-WebUSB via dfu.js / dfuse.js (devanlai/webdfu, ISC — see
   LICENSE-webdfu.txt). Flashes the STM32 system bootloader (VID 0x0483 /
   PID 0xDF11) at the internal-flash base address. Firmware images are
   served same-origin from firmware/manifest.json — GitHub release assets
   cannot be fetched cross-origin (no CORS on the asset CDN). */

(function() {
'use strict';

const DFU_VID = 0x0483;               // STMicroelectronics
const DFU_PID = 0xDF11;               // STM32 system bootloader (DFU mode)
const DEFAULT_FLASH_ADDRESS = 0x08000000;
const DEFAULT_XFER_SIZE = 1024;
const DEFAULT_FLASH_SIZE = 131072;    // 128 KB internal flash

const $ = (id) => document.getElementById(id);
const ui = {
  nosupport:    $('nosupport'),
  connect:      $('connect'),
  devinfo:      $('devinfo'),
  fwlist:       $('fwlist'),
  localfile:    $('localfile'),
  flash:        $('flash'),
  flashstate:   $('flashstate'),
  leds:         $('leds'),
  progressfill: $('progressfill'),
  log:          $('log'),
};

let manifest = null;
let device = null;                    // dfuse.Device
let transferSize = DEFAULT_XFER_SIZE;
let flashing = false;

function log(msg, isError) {
  const line = document.createElement('div');
  line.textContent = msg;
  if (isError) line.className = 'err';
  ui.log.appendChild(line);
  ui.log.scrollTop = ui.log.scrollHeight;
}

function setProgress(frac) {
  const pct = Math.max(0, Math.min(1, frac)) * 100;
  ui.progressfill.style.width = pct.toFixed(1) + '%';
}

function hex(n) { return '0x' + n.toString(16); }

function updateButtons() {
  ui.flash.disabled = flashing || !device || !selectedSource();
}

/* --- firmware sources -------------------------------------------------- */

function selectedSource() {
  if (ui.localfile.files && ui.localfile.files.length > 0) {
    return { local: ui.localfile.files[0] };
  }
  const checked = document.querySelector('#fwlist input[type=radio]:checked');
  if (checked && manifest) {
    return { entry: manifest.firmware[Number(checked.value)] };
  }
  return null;
}

async function loadManifest() {
  try {
    const resp = await fetch('firmware/manifest.json', { cache: 'no-cache' });
    if (!resp.ok) throw new Error('http ' + resp.status);
    manifest = await resp.json();
    renderList();
  } catch (e) {
    ui.fwlist.textContent = 'firmware list unavailable — flash a local .bin below.';
  }
}

function renderList() {
  ui.fwlist.textContent = '';
  manifest.firmware.forEach(function(fw, i) {
    const label = document.createElement('label');
    label.className = 'fwrow';

    const radio = document.createElement('input');
    radio.type = 'radio';
    radio.name = 'fw';
    radio.value = String(i);
    if (i === 0) radio.checked = true;
    radio.addEventListener('change', function() {
      ui.localfile.value = '';
      updateButtons();
    });
    label.appendChild(radio);

    const ver = document.createElement('span');
    ver.className = 'fwver';
    ver.textContent = fw.version + ' — ' + fw.variant;
    label.appendChild(ver);

    const meta = document.createElement('span');
    meta.className = 'fwmeta';
    meta.textContent = fw.released || '';
    label.appendChild(meta);

    if (fw.notes) {
      const a = document.createElement('a');
      a.href = fw.notes;
      a.textContent = 'release notes';
      a.target = '_blank';
      a.rel = 'noopener';
      a.className = 'fwmeta';
      label.appendChild(a);
    }
    ui.fwlist.appendChild(label);
  });
}

async function getFirmwareImage() {
  const src = selectedSource();
  if (!src) throw new Error('no firmware selected');

  if (src.local) {
    log('local file: ' + src.local.name + ' (' + src.local.size +
        ' bytes) — no checksum verification');
    return await src.local.arrayBuffer();
  }

  const fw = src.entry;
  log('fetching ' + fw.file + ' ...');
  const resp = await fetch('firmware/' + fw.file, { cache: 'no-cache' });
  if (!resp.ok) throw new Error('firmware download failed (http ' + resp.status + ')');
  const data = await resp.arrayBuffer();

  if (fw.sha256 && window.crypto && crypto.subtle) {
    const digest = await crypto.subtle.digest('SHA-256', data);
    const hexDigest = Array.from(new Uint8Array(digest))
        .map(function(b) { return b.toString(16).padStart(2, '0'); }).join('');
    if (hexDigest !== fw.sha256.toLowerCase()) {
      throw new Error('sha256 mismatch — download corrupted, not flashing');
    }
    log('sha256 verified (' + hexDigest.slice(0, 16) + '...)');
  }
  return data;
}

/* --- device ------------------------------------------------------------ */

// Chrome sometimes has no interfaceName on the alternates before the device
// was opened; read the string descriptors ourselves (webdfu demo pattern).
async function fixInterfaceNames(usbDevice, interfaces) {
  if (interfaces.every(function(intf) { return intf.name != null; })) return;
  const temp = new dfu.Device(usbDevice, interfaces[0]);
  await temp.device_.open();
  await temp.device_.selectConfiguration(1);
  const mapping = await temp.readInterfaceNames();
  await temp.close();
  for (const intf of interfaces) {
    if (intf.name == null) {
      const conf = intf.configuration.configurationValue;
      const num = intf['interface'].interfaceNumber;
      const alt = intf.alternate.alternateSetting;
      intf.name = mapping[conf][num][alt];
    }
  }
}

async function readTransferSize(dev) {
  try {
    const rawConfig = await dev.readConfigurationDescriptor(0);
    const configDesc = dfu.parseConfigurationDescriptor(rawConfig);
    if (configDesc.bConfigurationValue ===
        dev.settings.configuration.configurationValue) {
      for (const desc of configDesc.descriptors) {
        if (desc.bDescriptorType === 0x21 && 'wTransferSize' in desc) {
          return desc.wTransferSize;
        }
      }
    }
  } catch (e) {
    log('warning: could not read dfu descriptor (' + e + '), using ' +
        DEFAULT_XFER_SIZE);
  }
  return DEFAULT_XFER_SIZE;
}

async function connect() {
  try {
    const usbDevice = await navigator.usb.requestDevice({
      filters: [{ vendorId: DFU_VID, productId: DFU_PID }]
    });
    const interfaces = dfu.findDeviceDfuInterfaces(usbDevice);
    if (interfaces.length === 0) {
      throw new Error('no dfu interface on this device');
    }
    await fixInterfaceNames(usbDevice, interfaces);

    // Prefer the internal-flash alternate ("@Internal Flash/0x08000000/...").
    const setting = interfaces.find(function(i) {
      return i.name && i.name.indexOf('Internal Flash') >= 0;
    }) || interfaces[0];

    device = new dfuse.Device(usbDevice, setting);
    device.logDebug = function() {};
    device.logInfo = function(m) { log(m); };
    device.logWarning = function(m) { log('warning: ' + m); };
    device.logError = function(m) { log(m, true); };
    device.logProgress = function(done, total) {
      if (total) setProgress(done / total);
    };

    await device.open();
    transferSize = await readTransferSize(device);

    const status = await device.getStatus();
    if (status.state === dfu.dfuERROR) {
      await device.clearStatus();
    }

    const name = usbDevice.productName || 'stm32 bootloader';
    ui.devinfo.textContent = 'connected: ' + name;
    log('connected: ' + name + ' / "' + (setting.name || 'dfu') +
        '" / transfer size ' + transferSize);
    updateButtons();
  } catch (e) {
    device = null;
    if (e && e.name === 'NotFoundError') {
      log('no device selected. is the pedal in dfu mode (2-second hold)?');
      log('windows: if the list was empty, bind winusb to "stm32 bootloader" with zadig.');
    } else {
      log(String(e), true);
    }
    updateButtons();
  }
}

/* --- flashing ---------------------------------------------------------- */

async function flash() {
  if (!device || flashing) return;
  flashing = true;
  updateButtons();
  ui.connect.disabled = true;
  ui.leds.hidden = false;
  ui.leds.classList.add('blinking');
  ui.flashstate.textContent = 'flashing — do not unplug';
  setProgress(0);

  try {
    const data = await getFirmwareImage();

    const flashSize = (manifest && manifest.flash_size) || DEFAULT_FLASH_SIZE;
    if (data.byteLength > flashSize) {
      throw new Error('image is ' + data.byteLength +
          ' bytes — larger than internal flash (' + flashSize + ' bytes)');
    }
    if (data.byteLength < 1024) {
      throw new Error('image is implausibly small (' + data.byteLength +
          ' bytes) — is this a firmware .bin?');
    }

    device.startAddress = (manifest && manifest.flash_address)
        ? parseInt(manifest.flash_address, 16)
        : DEFAULT_FLASH_ADDRESS;

    log('writing ' + data.byteLength + ' bytes to ' +
        hex(device.startAddress) + ' ...');
    await device.do_download(transferSize, data, false);

    setProgress(1);
    log('done. the pedal is restarting into the new firmware.');
    ui.flashstate.textContent = 'done — pedal restarting';
    try { await device.close(); } catch (e) { /* device already gone */ }
    device = null;
    ui.devinfo.textContent = '';
  } catch (e) {
    log(String(e), true);
    log('nothing is bricked — the dfu bootloader lives in rom. ' +
        're-enter dfu mode (2-second hold) and try again.', true);
    ui.flashstate.textContent = 'failed';
  } finally {
    flashing = false;
    ui.leds.classList.remove('blinking');
    ui.connect.disabled = false;
    updateButtons();
  }
}

/* --- init ---------------------------------------------------------------*/

if (!navigator.usb) {
  ui.nosupport.hidden = false;
  ui.connect.disabled = true;
} else {
  ui.connect.addEventListener('click', connect);
  navigator.usb.addEventListener('disconnect', function(evt) {
    if (device && evt.device === device.device_ && !flashing) {
      device = null;
      ui.devinfo.textContent = 'device disconnected';
      updateButtons();
    }
  });
}

ui.flash.addEventListener('click', flash);
ui.localfile.addEventListener('change', function() {
  if (ui.localfile.files.length > 0) {
    const checked = document.querySelector('#fwlist input[type=radio]:checked');
    if (checked) checked.checked = false;
  }
  updateButtons();
});

loadManifest().then(updateButtons);

})();
