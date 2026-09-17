var Clay = require('pebble-clay');
var clayConfig = require('./config.json');

// Initialize Clay, but disable autoHandleEvents so we can manually control 
// the configuration lifecycle and inject our temporary timeout override.
var clay = new Clay(clayConfig, null, { autoHandleEvents: false });

Pebble.addEventListener('showConfiguration', function(e) {
  // 1. Temporarily disable the watchapp timeout while the settings page is open
  // This sends a dictionary payload to the watch via AppMessage.
  var overrideDict = { 'TimeoutSec': 0 };
  
  Pebble.sendAppMessage(overrideDict, function() {
    console.log('Timeout temporarily disabled during configuration.');
  }, function(error) {
    console.log('Failed to send timeout override: ' + JSON.stringify(error));
  });

  // 2. Open the configuration URL
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener('webviewclosed', function(e) {
  // If the user backs out without clicking "Save", e.response will be falsy.
  // By returning early, we ignore accidental adjustments entirely.
  if (e && !e.response) {
    console.log('Configuration canceled. Discarding changes.');
    return;
  }

  // The user deliberately tapped 'Save'. Fetch the sticky settings from the response.
  var dict = clay.getSettings(e.response);

  // Send the deliberately saved settings back to the watch. 
  // This payload will include their actual configured 'TimeoutSec' preference, 
  // automatically overwriting the temporary '0' we set earlier.
  Pebble.sendAppMessage(dict, function() {
    console.log('Settings successfully applied: ' + JSON.stringify(dict));
  }, function(error) {
    console.log('Failed to apply settings: ' + JSON.stringify(error));
  });
});
