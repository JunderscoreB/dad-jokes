module.exports = function(minified) {
    var clayConfig = this;

    function toggleScheduleSettings() {
        var mode = clayConfig.getItemByMessageKey('ScheduleMode').get();
        var specHour = clayConfig.getItemByMessageKey('SpecificHour');
        var specMinute = clayConfig.getItemByMessageKey('SpecificMinute');
        var winStart = clayConfig.getItemByMessageKey('WindowStartHour');
        var winEnd = clayConfig.getItemByMessageKey('WindowEndHour');
        var jokesPerHour = clayConfig.getItemByMessageKey('JokesPerHour');

        if (mode === "0") {
            specHour.show();
            specMinute.show();
            winStart.hide();
            winEnd.hide();
            jokesPerHour.hide();
        } else {
            specHour.hide();
            specMinute.hide();
            winStart.show();
            winEnd.show();
            jokesPerHour.show();
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
        var alertVolume = clayConfig.getItemByMessageKey('AlertVolume');

        if (platform !== 'emery') {
            soundTune.hide();
            alertVolume.hide();
            return;
        }

        var alertStyle = alertStyleItem.get();
        if (alertStyle === "1" || alertStyle === "2") {
            soundTune.show();
            alertVolume.show();
        } else {
            soundTune.hide();
            alertVolume.hide();
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

            // Unpack safe pipes (|) back into physical newlines (\n) on load
            textarea.value = (customJokesInput.get() || "").split('|').join('\n');

            // Use Capture Phase (true) to intercept the Enter key BEFORE Clay sees it
            textarea.addEventListener('keydown', function(e) {
                if (e.keyCode === 13 || e.key === 'Enter') {
                    e.stopPropagation();
                }
            }, true);

            // Pack newlines back into safe pipes as the user types
            textarea.addEventListener('input', function() {
                var safeString = textarea.value.split('\n').join('|');
                customJokesInput.set(safeString);
            });

            // If Clay clears the input programmatically, sync it to the textarea
            customJokesInput.on('change', function() {
                var expected = textarea.value.split('\n').join('|');
                if (customJokesInput.get() !== expected) {
                    textarea.value = (customJokesInput.get() || "").split('|').join('\n');
                }
            });

            // Hide the single-line input and inject our textarea
            inputEl.style.display = 'none';
            inputEl.parentNode.insertBefore(textarea, inputEl);
            customJokesInput._injectedTextArea = textarea;
        }
    }

    clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function() {
        var modeDropdown = clayConfig.getItemByMessageKey('ScheduleMode');
        modeDropdown.on('change', toggleScheduleSettings);
        toggleScheduleSettings();

        var alertStyleDropdown = clayConfig.getItemByMessageKey('AlertStyle');
        alertStyleDropdown.on('change', toggleAudioSettings);
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
