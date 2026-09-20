var Clay = require('@rebble/clay');
var clayConfig = require('./config.json');
var customClay = require('./custom-clay.js');

var clay = new Clay(clayConfig, customClay, { autoHandleEvents: false });

function getCustomJokesArray(rawString) {
  if (!rawString) return [];
  // Split exclusively by the safe pipe delimiter
  return rawString.split('|').map(function(j) { return j.trim(); }).filter(function(j) { return j.length > 0; });
}

Pebble.addEventListener('ready', function(e) {
  var customText = localStorage.getItem('CustomJokesText') || "";
  var jokesArray = getCustomJokesArray(customText);
  var mode = localStorage.getItem('CustomJokeMode') || "0";

  Pebble.sendAppMessage({
    'CustomJokeMode': parseInt(mode),
                        'CustomJokeCount': jokesArray.length
  });
});

Pebble.addEventListener('showConfiguration', function(e) {
  var overrideDict = { 'TimeoutSec': 0 };
  Pebble.sendAppMessage(overrideDict);

  var isModernPebbleApp = (typeof Pebble.getActiveWatchInfo === 'function');
  var watchInfo = isModernPebbleApp ? Pebble.getActiveWatchInfo() : null;
  var platform = watchInfo ? watchInfo.platform : 'aplite';

  var url;
  if (isModernPebbleApp) {
    url = clay.generateUrl();
  } else {
    var fallbackBaseUrl = 'https://your-github-username.github.io/dad-jokes-config/index.html';
    var existingSettings = localStorage.getItem('clay-settings') || '{}';
    url = fallbackBaseUrl + '?config=' + encodeURIComponent(existingSettings) + '&platform=' + platform;
  }
  Pebble.openURL(url);
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (e && !e.response) return;

  try {
    var decoded = decodeURIComponent(e.response);
    var rawDict = JSON.parse(decoded);
    var dict = {};

    // Intelligently flatten the JSON regardless of whether it came from Clay or the HTML fallback
    for (var key in rawDict) {
      if (rawDict[key] !== null && typeof rawDict[key] === 'object' && rawDict[key].value !== undefined) {
        dict[key] = rawDict[key].value;
      } else {
        dict[key] = rawDict[key];
      }
    }

    var jokeMode = dict.CustomJokeMode !== undefined ? dict.CustomJokeMode : (localStorage.getItem('CustomJokeMode') || "0");
    var jokesText = dict.CustomJokesText !== undefined ? dict.CustomJokesText : (localStorage.getItem('CustomJokesText') || "");
    var jokesArray = getCustomJokesArray(jokesText);

    localStorage.setItem('CustomJokeMode', jokeMode.toString());
    localStorage.setItem('CustomJokesText', jokesText);
    localStorage.setItem('clay-settings', JSON.stringify(dict));

    var safePayload = {
      'ScheduleMode': parseInt(dict.ScheduleMode || 0),
                        'SpecificHour': parseInt(dict.SpecificHour || 12),
                        'SpecificMinute': parseInt(dict.SpecificMinute || 0),
                        'WindowStartHour': parseInt(dict.WindowStartHour || 9),
                        'WindowEndHour': parseInt(dict.WindowEndHour || 17),
                        'JokesPerHour': parseInt(dict.JokesPerHour || 1),
                        'TimeoutSec': parseInt(dict.TimeoutSec || 30),
                        'AlertStyle': parseInt(dict.AlertStyle || 0),
                        'SoundTune': parseInt(dict.SoundTune || 0),
                        'AlertVolume': parseInt(dict.AlertVolume || 100),
                        'FontSize': parseInt(dict.FontSize || 2),
                        'CustomJokeMode': parseInt(jokeMode),
                        'CustomJokeCount': jokesArray.length
    };

    Pebble.sendAppMessage(safePayload);
  } catch (err) {
    console.log('Error parsing configuration response: ' + err);
  }
});

Pebble.addEventListener('appmessage', function(e) {
  if (typeof e.payload.RequestJokeIdx !== 'undefined') {
    var customText = localStorage.getItem('CustomJokesText') || "";
    var parsed = getCustomJokesArray(customText);

    var idx = e.payload.RequestJokeIdx;
    if (idx >= 0 && idx < parsed.length) {
      Pebble.sendAppMessage({ 'DeliverJokeText': parsed[idx] });
    }
  }
});
