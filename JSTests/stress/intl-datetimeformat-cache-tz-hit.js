function expect(label, got, want)
{
    if (got !== want)
        throw new Error(`${label}: expected "${want}", got "${got}"`);
}

function cacheableTZ()
{
    return new Intl.DateTimeFormat().resolvedOptions().timeZone;
}

function testTZ(timezones, index)
{
    if (index >= timezones.length)
        return;

    const tz = timezones[index];
    const before = cacheableTZ();

    if (!$vm.setHostTimeZoneForTesting(tz))
        throw new Error("Failed to set host time zone to " + tz);

    const stillCached = cacheableTZ();
    expect("before notification fires: cacheable returns old TZ", stillCached, before);

    $vm.timeZoneDidChange();

    const stillCached2 = cacheableTZ();
    expect("before notification fires 2: cacheable returns old TZ", stillCached2, before);

    // Get outside the VMEntryScope so clearForTimezoneChange() runs.
    setTimeout(() => {
        const afterInvalidate = cacheableTZ();
        expect("after notification fires: new TZ in effect", afterInvalidate, tz);
        testTZ(timezones, index + 1);
    }, 100);
}

testTZ(["UTC", "Pacific/Kiritimati", "America/Los_Angeles"], 0);
