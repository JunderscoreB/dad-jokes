var Clay = require('@rebble/clay');
var clayConfig = require('./config.json');
var customClay = require('./custom-clay.js');

var clay = new Clay(clayConfig, customClay, { autoHandleEvents: false });

function getCustomJokesArray(rawString) {
  if (!rawString) return [];
  // Split exclusively by the safe pipe delimiter
  return rawString.split('|').map(function(j) { return j.trim(); }).filter(function(j) { return j.length > 0; });
}

function getInt(val, fallback) {
  if (val === undefined || val === null) return fallback;
  var parsed = parseInt(val);
  return isNaN(parsed) ? fallback : parsed;
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
    var darkMode = dict.DarkMode !== undefined ? dict.DarkMode : (localStorage.getItem('DarkMode') || "0");
    var flickToDismiss = dict.FlickToDismiss !== undefined ? dict.FlickToDismiss : (localStorage.getItem('FlickToDismiss') || "1");
    var jokesArray = getCustomJokesArray(jokesText);

    localStorage.setItem('CustomJokeMode', jokeMode.toString());
    localStorage.setItem('CustomJokesText', jokesText);
    localStorage.setItem('DarkMode', darkMode.toString());
    localStorage.setItem('FlickToDismiss', flickToDismiss.toString());
    localStorage.setItem('clay-settings', JSON.stringify(dict));

    var safePayload = {
      'ScheduleMode': getInt(dict.ScheduleMode, 0),
      'SpecificHour': getInt(dict.SpecificHour, 12),
      'SpecificMinute': getInt(dict.SpecificMinute, 0),
      'WindowStartHour': getInt(dict.WindowStartHour, 9),
      'WindowEndHour': getInt(dict.WindowEndHour, 17),
      'JokesPerHour': getInt(dict.JokesPerHour, 1),
      'TimeoutSec': getInt(dict.TimeoutSec, 30),
      'AlertStyle': getInt(dict.AlertStyle, 0),
      'SoundTune': getInt(dict.SoundTune, 0),
      'AlertVolume': getInt(dict.AlertVolume, 100),
      'FontSize': getInt(dict.FontSize, 2),
      'CustomJokeMode': getInt(jokeMode, 0),
      'CustomJokeCount': jokesArray.length,
      'DarkMode': (darkMode === true || darkMode === "1" || darkMode === 1) ? 1 : 0,
      'FlickToDismiss': (flickToDismiss === false || flickToDismiss === "0" || flickToDismiss === 0) ? 0 : 1
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