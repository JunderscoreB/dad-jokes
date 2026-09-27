var Clay = require('@rebble/clay');
var clayConfig = require('./config.json');
var customClay = require('./custom-clay.js');
var BUILT_IN_JOKES = require('./jokes.js');

var clay = new Clay(clayConfig, customClay, { autoHandleEvents: false });

function getCustomJokesArray(rawString) {
  if (!rawString) return [];
  return rawString.split('|').map(function(j) { return j.trim(); }).filter(function(j) { return j.length > 0; });
}

function getInt(val, fallback) {
  if (val === undefined || val === null) return fallback;
  var parsed = parseInt(val);
  return isNaN(parsed) ? fallback : parsed;
}

function calculateTargetDate(schedMode, dict) {
  var targetDate = new Date();
  var now = new Date();

  if (schedMode === 0) {
    var specHour = getInt(dict.SpecificHour, 12);
    var specMinute = getInt(dict.SpecificMinute, 0);
    targetDate.setHours(specHour, specMinute, 0, 0);

    if (targetDate.getTime() <= now.getTime()) {
      targetDate.setDate(targetDate.getDate() + 1);
    }
  } else {
    var winStart = getInt(dict.WindowStartHour, 9);
    var winEnd = getInt(dict.WindowEndHour, 17) + 1;
    if (winEnd <= winStart) winEnd = winStart + 1;

    var startBoundary = new Date();
    startBoundary.setHours(winStart, 0, 0, 0);
    var endBoundary = new Date();
    endBoundary.setHours(winEnd, 0, 0, 0);

    if (now.getTime() > endBoundary.getTime()) {
      startBoundary.setDate(startBoundary.getDate() + 1);
      endBoundary.setDate(endBoundary.getDate() + 1);
    } else if (now.getTime() > startBoundary.getTime()) {
      startBoundary = now;
    }

    var offsetMs = Math.floor(Math.random() * (endBoundary.getTime() - startBoundary.getTime()));
    targetDate = new Date(startBoundary.getTime() + offsetMs);
  }

  return targetDate;
}

function getDeterministicPinId(targetDate) {
  return "dad-joke-pin-" + targetDate.getFullYear() + "-" + (targetDate.getMonth() + 1) + "-" + targetDate.getDate();
}

function pushTimelinePin(jokeText, targetDate) {
  Pebble.getTimelineToken(function(token) {
    var pinId = getDeterministicPinId(targetDate);

    var pin = {
      "id": pinId,
      "time": targetDate.toISOString(),
                          "layout": {
                            "type": "genericPin",
                            "title": "Joke of the Day",
                            "subtitle": jokeText,
                            "tinyIcon": "system://images/SMILE",
                            "body": jokeText
                          }
    };

    var request = new XMLHttpRequest();
    request.open('PUT', 'https://timeline-api.rebble.io/v1/user/pins/' + pin.id, true);
    request.setRequestHeader('Content-Type', 'application/json');
    request.setRequestHeader('X-User-Token', token);
    request.onload = function() {
      console.log('Timeline pin scheduled successfully for: ' + targetDate.toString() + ' with ID: ' + pin.id);
    };
    request.send(JSON.stringify(pin));
  }, function(error) {
    console.log('Failed to obtain timeline token: ' + error);
  });
}

function clearTimelinePins() {
  Pebble.getTimelineToken(function(token) {
    var now = new Date();
    for (var i = -1; i <= 5; i++) {
      var d = new Date(now.getTime() + (i * 86400000));
      var pinId = getDeterministicPinId(d);
      var request = new XMLHttpRequest();
      request.open('DELETE', 'https://timeline-api.rebble.io/v1/user/pins/' + pinId, true);
      request.setRequestHeader('X-User-Token', token);
      request.send();
    }
  }, function(error) {
    console.log('Failed to obtain timeline token for deletion: ' + error);
  });
}

function sendJokeCountToWatch() {
  var customText = localStorage.getItem('CustomJokesText') || "";
  var jokesArray = getCustomJokesArray(customText);
  var modeInt = parseInt(localStorage.getItem('CustomJokeMode') || "0");

  var totalCount = 0;
  if (modeInt === 0) totalCount = BUILT_IN_JOKES.length;
  else if (modeInt === 1) totalCount = BUILT_IN_JOKES.length + jokesArray.length;
  else if (modeInt === 2) totalCount = jokesArray.length;

  Pebble.sendAppMessage({ 'TotalJokeCount': totalCount });
}

Pebble.addEventListener('ready', function(e) {
  sendJokeCountToWatch();
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
    var enableTimelineRaw = dict.EnableTimeline !== undefined ? dict.EnableTimeline : (localStorage.getItem('EnableTimeline') || "1");
    var overrideVolumeRaw = dict.OverrideVolume !== undefined ? dict.OverrideVolume : (localStorage.getItem('OverrideVolume') || "0");
    var jokesArray = getCustomJokesArray(jokesText);

    var isTimelineEnabled = (enableTimelineRaw === true || enableTimelineRaw === "1" || enableTimelineRaw === 1);
    var isVolumeOverridden = (overrideVolumeRaw === true || overrideVolumeRaw === "1" || overrideVolumeRaw === 1);

    localStorage.setItem('CustomJokeMode', jokeMode.toString());
    localStorage.setItem('CustomJokesText', jokesText);
    localStorage.setItem('DarkMode', darkMode.toString());
    localStorage.setItem('FlickToDismiss', flickToDismiss.toString());
    localStorage.setItem('EnableTimeline', isTimelineEnabled ? "1" : "0");
    localStorage.setItem('OverrideVolume', isVolumeOverridden ? "1" : "0");
    localStorage.setItem('clay-settings', JSON.stringify(dict));

    var modeInt = getInt(jokeMode, 0);
    var schedMode = getInt(dict.ScheduleMode, 0);
    var targetDate = calculateTargetDate(schedMode, dict);

    var totalCount = 0;
    if (modeInt === 0) totalCount = BUILT_IN_JOKES.length;
    else if (modeInt === 1) totalCount = BUILT_IN_JOKES.length + jokesArray.length;
    else if (modeInt === 2) totalCount = jokesArray.length;

    var safePayload = {
      'ScheduleMode': schedMode,
      'SpecificHour': getInt(dict.SpecificHour, 12),
                        'SpecificMinute': getInt(dict.SpecificMinute, 0),
                        'WindowStartHour': getInt(dict.WindowStartHour, 9),
                        'WindowEndHour': getInt(dict.WindowEndHour, 17),
                        'JokesPerHour': getInt(dict.JokesPerHour, 1),
                        'TimeoutSec': getInt(dict.TimeoutSec, 30),
                        'AlertStyle': getInt(dict.AlertStyle, 0),
                        'SoundTune': getInt(dict.SoundTune, 1),
                        'OverrideVolume': isVolumeOverridden ? 1 : 0,
                        'AlertVolume': getInt(dict.AlertVolume, 100),
                        'FontSize': getInt(dict.FontSize, 2),
                        'TotalJokeCount': totalCount,
                        'DarkMode': (darkMode === true || darkMode === "1" || darkMode === 1) ? 1 : 0,
                        'FlickToDismiss': (flickToDismiss === false || flickToDismiss === "0" || flickToDismiss === 0) ? 0 : 1,
                        'EnableTimeline': isTimelineEnabled ? 1 : 0
    };

    if (isTimelineEnabled) {
      var useBuiltIn = false;
      if (modeInt === 0) useBuiltIn = true;
      else if (modeInt === 2) useBuiltIn = (jokesArray.length === 0);
      else useBuiltIn = (jokesArray.length === 0) || (Math.random() < 0.5);

      var jokeText = useBuiltIn
      ? BUILT_IN_JOKES[Math.floor(Math.random() * BUILT_IN_JOKES.length)]
      : jokesArray[Math.floor(Math.random() * jokesArray.length)];

      pushTimelinePin(jokeText, targetDate);
    } else {
      clearTimelinePins();
    }

    Pebble.sendAppMessage(safePayload);
  } catch (err) {
    console.log('Error parsing configuration response: ' + err);
  }
});

Pebble.addEventListener('appmessage', function(e) {
  if (typeof e.payload.RequestJokeIdx !== 'undefined') {
    var customText = localStorage.getItem('CustomJokesText') || "";
    var jokesArray = getCustomJokesArray(customText);
    var modeInt = parseInt(localStorage.getItem('CustomJokeMode') || "0");
    var idx = e.payload.RequestJokeIdx;

    var jokeToDeliver = "No joke found.";

    if (modeInt === 0) {
      if (idx < BUILT_IN_JOKES.length) jokeToDeliver = BUILT_IN_JOKES[idx];
    } else if (modeInt === 1) {
      if (idx < BUILT_IN_JOKES.length) {
        jokeToDeliver = BUILT_IN_JOKES[idx];
      } else if ((idx - BUILT_IN_JOKES.length) < jokesArray.length) {
        jokeToDeliver = jokesArray[idx - BUILT_IN_JOKES.length];
      }
    } else if (modeInt === 2) {
      if (idx < jokesArray.length) jokeToDeliver = jokesArray[idx];
    }

    Pebble.sendAppMessage({ 'DeliverJokeText': jokeToDeliver });
  }
});
