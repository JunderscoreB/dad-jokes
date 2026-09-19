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

    clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function() {
        var modeDropdown = clayConfig.getItemByMessageKey('ScheduleMode');
        modeDropdown.on('change', toggleScheduleSettings);
        toggleScheduleSettings();

        var alertStyleDropdown = clayConfig.getItemByMessageKey('AlertStyle');
        alertStyleDropdown.on('change', toggleAudioSettings);

        setupAlertOptions();
        toggleAudioSettings();
    });
};
