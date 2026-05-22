//@ requireOptions("--useTemporal=1")

// Companion to intl-datetimeformat-cache-dst-transition.js for Temporal.
// JSC's Temporal implementation only exposes a subset (no ZonedDateTime, no
// TimeZone.getOffsetNanosecondsFor), so we cover the two cache paths Temporal
// actually reaches:
//
//   1. Temporal.Now.timeZoneId() -> DateCache::defaultTimeZone()
//      -> timeZoneCache, which must invalidate after timeZoneDidChange().
//
//   2. Temporal.PlainDate arithmetic -> TemporalCalendar::isoDateFromFields
//      -> DateCache::yearMonthDayFromDaysWithCache, exercising the same
//      year/month/day cache as Date methods but from a separate code path.

function expect(label, got, want)
{
    if (got !== want)
        throw new Error(`${label}: expected ${JSON.stringify(want)}, got ${JSON.stringify(got)}`);
}

if (!$vm.setHostTimeZoneForTesting("America/Los_Angeles"))
    throw new Error("Failed to set host time zone to America/Los_Angeles");
$vm.timeZoneDidChange();

setTimeout(() => {
    // --- Temporal.Now.timeZoneId tracks the system TZ ---
    expect("Temporal.Now.timeZoneId", Temporal.Now.timeZoneId(), "America/Los_Angeles");

    // --- PlainDate arithmetic across the DST boundary ---
    // PlainDate is a wall-calendar object with no time zone, so DST cannot
    // change its arithmetic; what we are testing is that the shared
    // yearMonthDayFromDaysWithCache returns spec-correct values from the
    // Temporal code path. Walk a day at a time across spring-forward.
    const dayBefore = Temporal.PlainDate.from("2024-03-09");
    const dayOf     = dayBefore.add({ days: 1 });
    const dayAfter  = dayOf.add({ days: 1 });
    expect("PlainDate dayBefore", dayBefore.toString(), "2024-03-09");
    expect("PlainDate dayOf",     dayOf.toString(),     "2024-03-10");
    expect("PlainDate dayAfter",  dayAfter.toString(),  "2024-03-11");
    expect("PlainDate dayOf year",  dayOf.year,  2024);
    expect("PlainDate dayOf month", dayOf.month, 3);
    expect("PlainDate dayOf day",   dayOf.day,   10);

    // Larger jumps (well past the +/-28-day cache fast path) cross both
    // spring-forward and fall-back. Verify a fresh PlainDate has correct
    // calendar values.
    const winter = Temporal.PlainDate.from("2024-01-15");
    const summer = winter.add({ months: 5 });    // -> 2024-06-15
    const nextWinter = summer.add({ months: 7 }); // -> 2025-01-15
    expect("PlainDate winter -> summer", summer.toString(), "2024-06-15");
    expect("PlainDate summer -> next winter", nextWinter.toString(), "2025-01-15");

    // Crossing the fall-back date in single-day steps.
    const fbBefore = Temporal.PlainDate.from("2024-11-02");
    const fbOf     = fbBefore.add({ days: 1 });
    const fbAfter  = fbOf.add({ days: 1 });
    expect("PlainDate fbBefore", fbBefore.toString(), "2024-11-02");
    expect("PlainDate fbOf",     fbOf.toString(),     "2024-11-03");
    expect("PlainDate fbAfter",  fbAfter.toString(),  "2024-11-04");
    expect("PlainDate fbOf dayOfWeek", fbOf.dayOfWeek, 7); // Sunday in ISO

    // --- Cross-API consistency ---
    // PlainDate and Date should agree on the calendar date of an instant in LA.
    // Date.UTC(2024, 10, 3, 10, 30) = 02:30 PST -> 2024-11-03 in LA.
    const d = new Date(Date.UTC(2024, 10, 3, 10, 30));
    expect("Date getDate matches PlainDate", d.getDate(), fbOf.day);
    expect("Date getMonth+1 matches PlainDate", d.getMonth() + 1, fbOf.month);
    expect("Date getFullYear matches PlainDate", d.getFullYear(), fbOf.year);

    // Flip TZ to verify Temporal.Now picks up the change.
    if (!$vm.setHostTimeZoneForTesting("Asia/Tokyo"))
        throw new Error("Failed to set host time zone to Asia/Tokyo");
    $vm.timeZoneDidChange();

    setTimeout(() => {
        expect("Temporal.Now.timeZoneId after change",
            Temporal.Now.timeZoneId(), "Asia/Tokyo");

        // PlainDate arithmetic is TZ-independent, so it stays consistent.
        const stillDayOf = Temporal.PlainDate.from("2024-03-09").add({ days: 1 });
        expect("PlainDate post-change", stillDayOf.toString(), "2024-03-10");
    }, 100);
}, 100);
