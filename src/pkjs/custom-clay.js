module.exports = function(minified) {
  var clayConfig = this;

  function toggleScheduleSettings() {
    // Determine visibility based strictly on the Schedule Mode, ignoring the Timeline toggle
    var modeItem = clayConfig.getItemByMessageKey('ScheduleMode');
    var mode = modeItem ? modeItem.get() : "0";
    var specHour = clayConfig.getItemByMessageKey('SpecificHour');
    var specMinute = clayConfig.getItemByMessageKey('SpecificMinute');
    var winStart = clayConfig.getItemByMessageKey('WindowStartHour');
    var winEnd = clayConfig.getItemByMessageKey('WindowEndHour');
    var jokesPerHour = clayConfig.getItemByMessageKey('JokesPerHour');

    if (mode === "0") {
      if (specHour) specHour.show();
      if (specMinute) specMinute.show();
      if (winStart) winStart.hide();
      if (winEnd) winEnd.hide();
      if (jokesPerHour) jokesPerHour.hide();
    } else {
      if (specHour) specHour.hide();
      if (specMinute) specMinute.hide();
      if (winStart) winStart.show();
      if (winEnd) winEnd.show();
      if (jokesPerHour) jokesPerHour.show();
    }
  }

  function setupAlertOptions() {
    var platform = clayConfig.meta.activeWatchInfo ? clayConfig.meta.activeWatchInfo.platform : 'aplite';
    var alertStyleItem = clayConfig.getItemByMessageKey('AlertStyle');

    var selectEl = alertStyleItem.$element && alertStyleItem.$element[0]
    ? alertStyleItem.$element[0].querySelector('select')
    : document.querySelector('select[name="AlertStyle"]');

    if (selectEl && platform !== 'emery') {
      var soundOnlyOpt = selectEl.querySelector('option[value="1"]');
      var vibeSoundOpt = selectEl.querySelector('option[value="2"]');
      var vibeOnlyOpt = selectEl.querySelector('option[value="0"]');

      if (soundOnlyOpt && soundOnlyOpt.parentNode) soundOnlyOpt.parentNode.removeChild(soundOnlyOpt);
      if (vibeSoundOpt && vibeSoundOpt.parentNode) vibeSoundOpt.parentNode.removeChild(vibeSoundOpt);
      if (vibeOnlyOpt) vibeOnlyOpt.textContent = 'Vibration';

      var currentVal = alertStyleItem.get();
      if (currentVal === "1" || currentVal === "2") {
        alertStyleItem.set("0");
      }
    }
  }

  function toggleAudioSettings() {
    var platform = clayConfig.meta.activeWatchInfo ? clayConfig.meta.activeWatchInfo.platform : 'aplite';
    var alertStyleItem = clayConfig.getItemByMessageKey('AlertStyle');
    var soundTune = clayConfig.getItemByMessageKey('SoundTune');
    var overrideVolume = clayConfig.getItemByMessageKey('OverrideVolume');
    var alertVolume = clayConfig.getItemByMessageKey('AlertVolume');

    if (platform !== 'emery') {
      if (soundTune) soundTune.hide();
      if (overrideVolume) overrideVolume.hide();
      if (alertVolume) alertVolume.hide();
      return;
    }

    var alertStyle = alertStyleItem.get();
    
    // Check if the current alert style permits sound
    if (alertStyle === "1" || alertStyle === "2") {
      if (soundTune) soundTune.show();
      if (overrideVolume) overrideVolume.show();
      
      // Only show the volume slider if the override toggle is enabled
      var isOverrideEnabled = overrideVolume && (overrideVolume.get() === true || overrideVolume.get() === "1" || overrideVolume.get() === 1);
      
      if (isOverrideEnabled) {
        if (alertVolume) alertVolume.show();
      } else {
        if (alertVolume) alertVolume.hide();
      }
      
    } else {
      if (soundTune) soundTune.hide();
      if (overrideVolume) overrideVolume.hide();
      if (alertVolume) alertVolume.hide();
    }
  }

  function injectTextArea() {
    var customJokesInput = clayConfig.getItemByMessageKey('CustomJokesText');
    if (!customJokesInput || !customJokesInput.$element) return;

    var inputEl = customJokesInput.$element[0].querySelector('input');
    if (inputEl && inputEl.tagName.toLowerCase() === 'input') {
      var textarea = document.createElement('textarea');
      textarea.rows = 8;
      textarea.className = inputEl.className;
      textarea.style.width = '100%';
      textarea.style.minHeight = '150px';
      textarea.style.resize = 'vertical';
      textarea.style.fontFamily = 'monospace';

      textarea.value = (customJokesInput.get() || "").split('|').join('\n');

      textarea.addEventListener('keydown', function(e) {
        if (e.keyCode === 13 || e.key === 'Enter') {
          e.stopPropagation();
        }
      }, true);

      textarea.addEventListener('input', function() {
        var safeString = textarea.value.split('\n').join('|');
        customJokesInput.set(safeString);
      });

      customJokesInput.on('change', function() {
        var expected = textarea.value.split('\n').join('|');
        if (customJokesInput.get() !== expected) {
          textarea.value = (customJokesInput.get() || "").split('|').join('\n');
        }
      });

      inputEl.style.display = 'none';
      inputEl.parentNode.insertBefore(textarea, inputEl);
      customJokesInput._injectedTextArea = textarea;
    }
  }

  clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function() {
    var modeDropdown = clayConfig.getItemByMessageKey('ScheduleMode');
    if (modeDropdown) {
      modeDropdown.on('change', toggleScheduleSettings);
    }
    toggleScheduleSettings();

    var alertStyleDropdown = clayConfig.getItemByMessageKey('AlertStyle');
    if (alertStyleDropdown) {
      alertStyleDropdown.on('change', toggleAudioSettings);
    }
    
    // Bind the change event for the Override System Volume toggle
    var overrideVolumeToggle = clayConfig.getItemByMessageKey('OverrideVolume');
    if (overrideVolumeToggle) {
      overrideVolumeToggle.on('change', toggleAudioSettings);
    }
    
    setupAlertOptions();
    toggleAudioSettings();

    injectTextArea();

    var clearBtn = clayConfig.getItemByMessageKey('ClearCustomJokesBtn');
    if (clearBtn) {
      clearBtn.on('click', function() {
        var customJokesInput = clayConfig.getItemByMessageKey('CustomJokesText');
        if (customJokesInput) {
          customJokesInput.set("");
          if (customJokesInput._injectedTextArea) {
            customJokesInput._injectedTextArea.value = "";
          }
        }
      });
    }
  });
};
