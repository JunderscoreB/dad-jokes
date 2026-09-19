var Clay = require('@rebble/clay');
var clayConfig = require('./config.json');
var customClay = require('./custom-clay.js');

var clay = new Clay(clayConfig, customClay, { autoHandleEvents: false });

Pebble.addEventListener('showConfiguration', function(e) {
  var overrideDict = { 'TimeoutSec': 0 };

  Pebble.sendAppMessage(overrideDict, function() {
    console.log('Timeout temporarily disabled during configuration.');
  }, function(error) {
    console.log('Failed to send timeout override: ' + JSON.stringify(error));
  });

  var isModernPebbleApp = (typeof Pebble.getActiveWatchInfo === 'function');
  var watchInfo = isModernPebbleApp ? Pebble.getActiveWatchInfo() : null;
  var platform = watchInfo ? watchInfo.platform : 'aplite';

  var url;
  if (isModernPebbleApp) {
    console.log('Modern Pebble App detected. Launching Rebble Clay.');
    url = clay.generateUrl();
  } else {
    console.log('Legacy Pebble App detected. Launching HTML fallback.');
    var fallbackBaseUrl = 'https://your-github-username.github.io/dad-jokes-config/index.html';

    var existingSettings = localStorage.getItem('clay-settings') || '{}';
    var configString = encodeURIComponent(existingSettings);

    url = fallbackBaseUrl + '?config=' + configString + '&platform=' + platform;
  }

  Pebble.openURL(url);
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (e && !e.response) {
    console.log('Configuration canceled. Discarding changes.');
    return;
  }

  try {
    var dict = clay.getSettings(e.response);

    Pebble.sendAppMessage(dict, function() {
      console.log('Settings successfully applied: ' + JSON.stringify(dict));
    }, function(error) {
      console.log('Failed to apply settings: ' + JSON.stringify(error));
    });
  } catch (err) {
    console.log('Error parsing configuration response: ' + err);
  }
});
