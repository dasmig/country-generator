#!/usr/bin/env python3
"""Write country-generator's holidays.tsv and holiday_events.tsv.

Source: python-holidays (MIT), at the pinned version below. The rules are
inferred from its output, not written by hand (design, section 11.3):

1. Read every country python-holidays supports, for each year of its span
   within 1970 to 2050 (narrowed where python-holidays warns that its data is
   shorter, as for India's 2001 to 2035), in the public, bank, optional,
   government, half-day and unofficial categories: the real dates
   (observed=False) in the default language and in en_US, and the moved days
   (observed=True).
2. Group the entries into one series per holiday and kind by the English
   name, with python-holidays' "(estimated)" label removed. Consecutive days
   with the same name are one multi-day holiday (`days`).
3. Fit each series with the fewest runs of years one rule explains: a fixed
   date, an n-th weekday, a weekday on or after or before a date, Western or
   Orthodox Easter plus N, or a calendar event plus N. Event dates come from
   python-holidays' calendars (`holidays.calendars`); a country whose dates
   differ from them by up to three days in some years gets its own rows for
   those years, and an event date a written rule relies on is never moved.
   A series no rule explains in few runs becomes the country's own event (its
   dates), and a series with several dates a year may be split by their rank.
4. Fit an observed policy to each rule's years from the moved days: a named
   policy or a weekday map read off the series (section 8.4, as extended).
5. Evaluate the rows as the library will, year by year, and compare with
   python-holidays. Where a move differs, that year gets no move and the moved
   days python-holidays has become the country's own event, until every real
   date and every moved day agrees, for every country and year.

Rows always carry their from_year and to_year. holiday_years.tsv gives each
country's span: python-holidays' years for it within 1970 to 2050, which can
run past its last row (Ukraine has no holidays under martial law from 2023).

Usage:
    python scripts/prepare_holidays.py --report build/holidays_report.txt
    python scripts/prepare_holidays.py --countries US GB    # a look; writes no table

Run it from the repository root with python-holidays at the pinned version
(pip install holidays==0.107) and after prepare_locales.py, whose weekends it
uses.
"""

import argparse
import datetime
import importlib
import inspect
import re
import unicodedata
import warnings
from collections import Counter, defaultdict
from pathlib import Path

import holidays
from holidays.constants import BANK, GOVERNMENT, HALF_DAY, OPTIONAL, PUBLIC, UNOFFICIAL

HOLIDAYS_VERSION = "0.107"
FIRST_YEAR, LAST_YEAR = 1970, 2050

KINDS = {PUBLIC: "public", BANK: "bank", OPTIONAL: "optional", GOVERNMENT: "optional",
         HALF_DAY: "optional", UNOFFICIAL: "observance"}
CATEGORIES = (PUBLIC, BANK, OPTIONAL, GOVERNMENT, HALF_DAY, UNOFFICIAL)

DOW = ("mon", "tue", "wed", "thu", "fri", "sat", "sun")      # date.weekday() order
EASTER_SPAN = 100        # |N| for easter+N and orthodox+N
EVENT_SPAN = 10          # |N| for event:KEY+N
OVERRIDE_SPAN = 3        # a country's event date may differ from the shared one by this many days

# The calendars the shared events come from, and a prefix where keys would clash.
CALENDARS = {
    "islamic": ("_IslamicLunar", ""), "chinese": ("_ChineseLunisolar", ""), "hebrew": ("_HebrewLunisolar", ""),
    "hindu": ("_HinduLunisolar", ""), "buddhist": ("_BuddhistLunisolar", ""),
    "balinese_saka": ("_BalineseSakaLunar", ""), "burmese": ("_BurmeseLunisolar", "burmese_"),
    "mongolian": ("_MongolianLunisolar", ""), "persian": ("_Persian", "persian_"),
    "sinhala": ("_SinhalaLunar", ""), "thai": ("_ThaiLunisolar", "thai_"),
    "tibetan": ("_TibetanLunisolar", ""),
}
# Left out: the Mandaean calendar, a 365-day year that drifts a day every four
# years and so imitates fixed dates; no country's holidays in these categories use it.

_EPOCH = datetime.date(1970, 1, 1)


# ---------------------------------------------------------------------------
# Calendar arithmetic, as the library does it (section 8.3)
# ---------------------------------------------------------------------------

def easter(y):
    a, b, c = y % 19, y // 100, y % 100
    d, e = b // 4, b % 4
    f = (b + 8) // 25
    g = (b - f + 1) // 3
    h = (19 * a + b - d - g + 15) % 30
    i, k = c // 4, c % 4
    l = (32 + 2 * e + 2 * i - h - k) % 7
    m = (a + 11 * h + 22 * l) // 451
    n = h + l - 7 * m + 114
    return datetime.date(y, n // 31, n % 31 + 1)


def orthodox(y):
    a, b, c = y % 4, y % 7, y % 19
    d = (19 * c + 15) % 30
    e = (2 * a + 4 * b - d + 34) % 7
    n = d + e + 114
    return datetime.date(y, n // 31, n % 31 + 1) + datetime.timedelta(days=y // 100 - y // 400 - 2)


def month_length(y, m):
    return ((datetime.date(y + m // 12, m % 12 + 1, 1)) - datetime.date(y, m, 1)).days


def nth_weekday(y, m, n, dow):
    if n > 0:
        first = datetime.date(y, m, 1)
        return first + datetime.timedelta(days=(dow - first.weekday()) % 7 + 7 * (n - 1))
    last = datetime.date(y, m, month_length(y, m))
    return last - datetime.timedelta(days=(last.weekday() - dow) % 7)


# ---------------------------------------------------------------------------
# Events
# ---------------------------------------------------------------------------

class Events:
    """Shared event dates from python-holidays' calendars, and a country's own."""

    def __init__(self):
        self.shared = {}                      # key -> sorted dates
        for module, (cls_name, prefix) in CALENDARS.items():
            cls = getattr(importlib.import_module(f"holidays.calendars.{module}"), cls_name)
            calendar = cls()
            for name, method in inspect.getmembers(calendar, inspect.ismethod):
                if name.startswith("_") or not name.endswith(("_date", "_dates")):
                    continue
                key = prefix + name.rsplit("_date", 1)[0]
                if key in self.shared:
                    raise SystemExit(f"event key {key} twice")
                dates = set()
                for y in range(FIRST_YEAR - 1, LAST_YEAR + 2):
                    try:
                        got = method(y)
                    except (TypeError, ValueError, KeyError):
                        continue
                    for item in (got if name.endswith("_dates") else [got]):
                        day = item[0] if isinstance(item, tuple) else item
                        if isinstance(day, datetime.date):
                            dates.add(day)
                self.add(key, dates)
        # The Ethiopian calendar module has only its leap-year rule: Meskerem 1 is
        # September 11, or 12 when the Ethiopian year starting then is a leap year.
        from holidays.calendars.ethiopian import is_ethiopian_leap_year
        new_year = {y: datetime.date(y, 9, 12 if is_ethiopian_leap_year(y) else 11)
                    for y in range(FIRST_YEAR - 2, LAST_YEAR + 2)}
        self.add("ethiopian_new_year", new_year.values())
        self.add("meskel", (d + datetime.timedelta(days=16) for d in new_year.values()))
        self.add("timkat", (d + datetime.timedelta(days=130) for d in new_year.values()))
        # Japan's equinox days, from python-holidays' own table of them.
        from holidays.countries.japan import Japan
        japan, equinox = Japan(), defaultdict(set)
        for y in range(FIRST_YEAR - 1, LAST_YEAR + 2):
            japan._year = y
            equinox["vernal_equinox"].add(datetime.date(y, *japan._vernal_equinox_date))
            equinox["autumnal_equinox"].add(datetime.date(y, *japan._autumnal_equinox_date))
        for key, dates in equinox.items():
            self.add(key, dates)
        self.override = defaultdict(dict)      # (country, key) -> {shared date: country date}
        self.own = {}                          # (country, key) -> sorted dates of a country's own event
        self.pinned = set()                    # (country, key, shared date) a written rule relies on
        self.by_day = defaultdict(list)        # date -> [(key, shared date)]
        for key, dates in self.shared.items():
            for d in dates:
                self.by_day[d].append((key, d))

    def add(self, key, dates):
        """A calendar's dates, multi-day runs reduced to their first day; a calendar
        with dates under 30 days apart (a weekly sabbath) is left out."""
        firsts = [d for d in sorted(set(dates)) if d - datetime.timedelta(days=1) not in dates]
        if firsts and all((b - a).days >= 30 for a, b in zip(firsts, firsts[1:])):
            self.shared[key] = firsts

    def dates(self, country, key):
        if (country, key) in self.own:
            return self.own[(country, key)]
        own = self.override.get((country, key))
        base = self.shared[key]
        return sorted(own.get(d, d) for d in base) if own else base

    def near(self, day, span):
        """[(key, shared date)] within span days of day."""
        out = []
        for k in range(-span, span + 1):
            out.extend(self.by_day.get(day + datetime.timedelta(days=k), ()))
        return out


# ---------------------------------------------------------------------------
# Reading python-holidays
# ---------------------------------------------------------------------------

def label_regex(label):
    return re.compile("^" + re.escape(label).replace("%s", "(.+)") + "$") if "%s" in label else None


def strip(name, regexes):
    for r in regexes:
        if r and (m := r.match(name)):
            return m[1]
    return name


def tr_label(h, attr):
    return h.tr(getattr(h, attr)) if getattr(h, attr, None) else None


def substituted(h):
    """(regex, name) for python-holidays' substituted days, "Day off (substituted
    from %s)": the name is the label's text before its parenthesis ("Day off",
    "休息日"), so a country's transfer days are one series, not one per date."""
    label = tr_label(h, "substituted_label")
    if not label or "%s" not in label:
        return None
    return label_regex(label), re.split(r"\s*[(（]", label, maxsplit=1)[0]


def unsubstitute(name, sub):
    return sub[1] if sub and sub[0].match(name) else name


class Country:
    """python-holidays' output for one country, grouped into series."""

    def __init__(self, code):
        probe = holidays.country_holidays(code)
        cls = probe.__class__
        self.code = code
        self.first = max(FIRST_YEAR, cls.start_year)
        self.last = min(LAST_YEAR, cls.end_year)
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always")
            holidays.country_holidays(code, years=range(self.first, self.last + 1))
        for w in caught:      # "Requested Holidays are available only from 2001 to 2035."
            if m := re.search(r"available only from (\d{4}) to (\d{4})", str(w.message)):
                self.first, self.last = max(self.first, int(m[1])), min(self.last, int(m[2]))
        self.python_weekend = {DOW[d] for d in getattr(probe, "weekend", ())}
        languages = getattr(cls, "supported_languages", ()) or ()
        english = "en_US" if "en_US" in languages else None
        years = range(self.first, self.last + 1)
        self.series = defaultdict(lambda: {"dates": [], "names": {}})
        self.real = defaultdict(list)      # kind -> real dates, one per holiday
        self.observed = []                 # (date, kind, base English name, observed English name)
        self.observed_label = {}           # kind -> native "%s (observed)"
        self.seen_observed = set()
        self.claimed = {}                  # (date, English name) -> the first kind that lists it
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            for cat in (c for c in CATEGORIES if c in cls.supported_categories):
                self._read(code, cat, years, english)

    def _read(self, code, cat, years, english):
        kind = KINDS[cat]
        native = holidays.country_holidays(code, years=years, categories=(cat,), observed=False)
        en = holidays.country_holidays(code, years=years, categories=(cat,), observed=False, language=english)
        obs = holidays.country_holidays(code, years=years, categories=(cat,), observed=True, language=english)
        est_native = [label_regex(x) for x in [tr_label(native, "estimated_label")] if x]
        est_en = [label_regex(x) for x in [tr_label(en, "estimated_label")] if x]
        obs_labels = [label_regex(x) for x in (tr_label(obs, "observed_estimated_label"),
                                                tr_label(obs, "observed_label")) if x]
        self.observed_label.setdefault(kind, tr_label(native, "observed_label") or "%s")
        sub_en, sub_native = substituted(en), substituted(native)
        for day in sorted(en):
            names_en, names_native = en.get_list(day), native.get_list(day)
            if len(names_en) != len(names_native):
                names_native = names_en
            for n_en, n_native in zip(names_en, names_native):
                n_en, n_native = unsubstitute(n_en, sub_en), unsubstitute(n_native, sub_native)
                base = strip(n_en, est_en)
                if self.claimed.setdefault((day, base), kind) != kind:
                    continue                    # a stronger kind lists it (US government repeats federal days)
                s = self.series[(kind, base)]
                if day in s["names"]:
                    continue                    # listed by two categories of one kind
                s["dates"].append(day)
                s["names"][day] = strip(n_native, est_native)
                self.real[kind].append(day)
        real = Counter((d, n) for d in en for n in en.get_list(d))
        for day in sorted(obs):
            for n in obs.get_list(day):
                if real[(day, n)]:
                    real[(day, n)] -= 1
                    continue
                entry = (day, kind, strip(n, obs_labels), n)
                if self.claimed.setdefault(("observed", day, entry[2]), kind) != kind:
                    continue
                if entry not in self.seen_observed:
                    self.seen_observed.add(entry)
                    self.observed.append(entry)


def occurrences(dates):
    """Sorted dates -> [(start, days)] of consecutive runs."""
    out = []
    for d in sorted(set(dates)):
        if out and d == out[-1][0] + datetime.timedelta(days=out[-1][1]):
            out[-1] = (out[-1][0], out[-1][1] + 1)
        else:
            out.append((d, 1))
    return out


# ---------------------------------------------------------------------------
# Rule fitting
# ---------------------------------------------------------------------------

FORM_ORDER = {"fixed": 0, "nth": 1, "after": 2, "before": 2, "easter": 3, "orthodox": 4, "event": 5}


def gregorian_candidates(s, y):
    """Every non-event rule that puts a holiday of year y on date s."""
    out = {("fixed", f"{s.month:02}-{s.day:02}")}
    n = (s.day - 1) // 7 + 1
    dow = DOW[s.weekday()]
    out.add(("nth", f"{s.month:02}:{n}:{dow}"))
    if s.day + 7 > month_length(y, s.month):
        out.add(("nth", f"{s.month:02}:-1:{dow}"))
    for k in range(7):
        for form, base in (("after", s - datetime.timedelta(days=k)), ("before", s + datetime.timedelta(days=k))):
            if base.year == y and not (base.month == 2 and base.day == 29):
                out.add((form, f"{base.month:02}-{base.day:02}{'>' if form == 'after' else '<'}{dow}"))
    for form, ref in (("easter", easter(y)), ("orthodox", orthodox(y))):
        n = (s - ref).days
        if abs(n) <= EASTER_SPAN:
            out.add((form, form + (f"{n:+d}" if n else "")))
    return out


def rule_dates(rule, y, country, events):
    """The start dates a rule gives in year y."""
    form, text = rule
    if form == "fixed":
        m, d = int(text[:2]), int(text[3:])
        return [datetime.date(y, m, d)] if d <= month_length(y, m) else []
    if form == "nth":
        m, n, dow = text.split(":")
        return [nth_weekday(y, int(m), int(n), DOW.index(dow))]
    if form in ("after", "before"):
        base = datetime.date(y, int(text[:2]), int(text[3:5]))
        dow = DOW.index(text[6:])
        step = (dow - base.weekday()) % 7 if form == "after" else -((base.weekday() - dow) % 7)
        return [base + datetime.timedelta(days=step)]
    if form in ("easter", "orthodox"):
        n = int(text[len(form):] or 0)
        return [(easter(y) if form == "easter" else orthodox(y)) + datetime.timedelta(days=n)]
    key, n = event_parts(text)
    return [d + datetime.timedelta(days=n) for d in events.dates(country, key)
            if (d + datetime.timedelta(days=n)).year == y]


def event_parts(text):
    m = re.fullmatch(r"event:([a-z0-9_]+?)([+-]\d+)?", text)
    return m[1], int(m[2] or 0)


def event_text(key, n):
    return f"event:{key}" + (f"{n:+d}" if n else "")


def event_overrides(key, n, starts, y, country, events, span=OVERRIDE_SPAN):
    """The (key, shared date, country date) overrides event:key+n needs to give this
    year's starts, () when it gives them as is, None when it cannot."""
    own = events.override.get((country, key), {})
    shared = [e for e in events.shared[key] if (own.get(e, e) + datetime.timedelta(days=n)).year == y]
    if len(shared) != len(starts):
        return None
    out = []
    for e, s in zip(shared, starts):
        target = s - datetime.timedelta(days=n)
        if target == own.get(e, e):
            continue
        if e in own or (country, key, e) in events.pinned or abs((target - e).days) > span:
            return None
        out.append((key, e, target))
    return tuple(out)


def exact_events(starts, days, y, country, events):
    """The (key, n) of the events that give exactly this year's starts."""
    out = set()
    for s in starts:
        for key, e in events.near(s, EVENT_SPAN):
            n = (s - e).days
            if event_overrides(key, n, starts, y, country, events, span=0) == ():
                out.add((key, n))
    return out


def rule_cost(item):
    (rule, days), overrides = item
    form, text = rule
    return (len(overrides), FORM_ORDER[form], "-1" in text, len(text), text)


def greedy(years, candidates):
    """The fewest runs of consecutive years with a rule in common, longest first.
    candidates(y) -> {(rule, days): overrides}. A year with none is a None run."""
    runs, i = [], 0
    while i < len(years):
        current = candidates(years[i])
        if not current:
            runs.append(None)
            i += 1
            continue
        acc = {k: list(v) for k, v in current.items()}
        j = i + 1
        while j < len(years) and years[j] == years[j - 1] + 1:
            keep = {k: v for k, v in candidates(years[j]).items() if k in current}
            if not keep:
                break
            current = keep
            for k, v in keep.items():
                acc[k].extend(v)
            j += 1
        (rule, days), ov = min(((k, tuple(acc[k])) for k in current), key=rule_cost)
        runs.append((rule, days, years[i], years[j - 1], ov))
        i = j
    return runs


def rows_cost(runs):
    """About how many event rows' worth of text a fit writes."""
    return sum(1000 if r is None else 3 + len(r[4]) for r in runs)


STOP_WORDS = {"day", "days", "holiday", "holidays", "the", "of", "s", "and", "national", "public", "observed",
              "birthday", "new", "year", "eve", "feast", "festival", "first", "second", "third", "great", "holy"}


def similarity(name, key):
    """Words a holiday's name and an event key share, so Eid al-Fitr prefers
    eid_al_fitr to an event that only happens to give the same dates."""
    words = lambda t: set(t.split("_")) - STOP_WORDS
    return len(words(slug(name)) & words(key))


def fit(series_years, country, events, name=""):
    """{year: [(start, days)]} -> ("rules", runs) or ("event", [(start, days)]).

    Runs are (rule, days, from, to, overrides). Gregorian rules first; an event
    with the country's own dates only where they make the fit smaller; the
    series' own dates as a country event where even that splinters."""
    years = sorted(series_years)

    def candidates(event_rule):
        def at(y):
            occ = series_years[y]
            if len({d for _, d in occ}) != 1:
                return {}
            starts, days = [s for s, _ in occ], occ[0][1]
            out = {}
            if len(starts) == 1:
                out = {(rule, days): () for rule in gregorian_candidates(starts[0], y)}
            if event_rule is not None:
                key, n = event_rule
                ov = event_overrides(key, n, starts, y, country, events)
                if ov is not None:
                    out[(("event", event_text(key, n)), days)] = ov
            return out
        return at

    best = greedy(years, candidates(None))
    if len(best) > 2 or None in best:
        hits = Counter()
        for y in years:
            occ = series_years[y]
            if len({d for _, d in occ}) == 1:
                hits.update(exact_events([s for s, _ in occ], occ[0][1], y, country, events))
        # The best-matching event by hits, and the best one whose key shares words
        # with the name, which may need more of the country's own dates: the
        # named one unless it costs half as much again.
        rank = lambda kv: (kv[1], similarity(name, kv[0][0]), -abs(kv[0][1]), kv[0][0])
        by_hits = [kv for kv in hits.items() if kv[1] >= 3 and kv[1] * 2 >= len(years)]
        named = [kv for kv in hits.items() if similarity(name, kv[0][0]) and kv[1] >= 3 and kv[1] * 4 >= len(years)]
        tries = []
        for pool in (named, by_hits):
            if pool:
                rule = max(pool, key=rank)[0]
                tries.append(greedy(years, candidates(rule)))
        if tries:
            runs = tries[0] if len(tries) == 1 or rows_cost(tries[0]) <= 1.5 * rows_cost(tries[1]) else tries[1]
            if rows_cost(runs) < rows_cost(best):
                best = runs
    all_occ = [o for y in years for o in series_years[y]]
    if rows_cost(best) > 3 * len({d for _, d in all_occ}) + len(all_occ):
        return "event", all_occ
    return "rules", best


# ---------------------------------------------------------------------------
# Series to rows
# ---------------------------------------------------------------------------

# Named policies; a fitted series may also get a weekday map such as
# "tue<mon,wed<mon,thu>mon,fri>mon" (Argentina): each listed weekday moves to the
# next (>) or previous (<) given weekday. A day already taken by a holiday or an
# earlier move pushes the move on to the next free weekday, unless the policy
# ends in ",overlap" (python-holidays' "to next Monday" rules, as against its
# "to next workday" ones).
POLICIES = ("none", "sun>mon", "sat<fri,sun>mon", "sat>mon,sun>mon", "next_weekday", "weekend>next")
KIND_ORDER = {"public": 0, "bank": 1, "optional": 2, "observance": 3}


def slug(text):
    t = unicodedata.normalize("NFKD", text).encode("ascii", "ignore").decode().lower()
    return re.sub(r"[^a-z0-9]+", "_", t).strip("_") or "holiday"


def clean(text):
    if any(ch in text for ch in "\t\n\r"):
        raise SystemExit(f"{text!r} holds a tab or newline")
    return text


def by_year(occ):
    out = defaultdict(list)
    for start, days in sorted(occ):
        out[start.year].append((start, days))
    return out


def fit_cost(how, result):
    if how == "event":
        return 3 * len({d for _, d in result}) + len(result)
    return rows_cost(result)


def fit_series(occ, country, events, name=""):
    """[(occurrences, how, result)]: the series whole, or split by rank in the year
    when every year has the same number of dates and that is smaller."""
    whole = fit(by_year(occ), country, events, name)
    years = by_year(occ)
    counts = {len(v) for v in years.values()}
    if len(counts) == 1 and counts != {1}:
        parts = [[v[i] for v in years.values()] for i in range(counts.pop())]
        fits = [fit(by_year(p), country, events, name) for p in parts]
        if sum(fit_cost(*f) for f in fits) < fit_cost(*whole):
            return [(p, *f) for p, f in zip(parts, fits)]
    return [(occ, *whole)]


def commit(how, result, country, key, events):
    """Record a fit's overrides or own dates; the [(rule, days, from, to)] it writes."""
    if how == "rules":
        for rule, _, a, b, ov in result:
            for k, shared, target in ov:
                events.override[(country, k)][shared] = target
            if rule[0] == "event":
                k, n = event_parts(rule[1])
                own = events.override.get((country, k), {})
                events.pinned.update((country, k, e) for e in events.shared.get(k, ())
                                     if a <= (own.get(e, e) + datetime.timedelta(days=n)).year <= b)
        return [(rule[1], days, a, b) for rule, days, a, b, _ in result]
    out = []
    lengths = sorted({d for _, d in result})
    for days in lengths:
        own = f"{country.lower()}_{key}" + (f"_{days}d" if len(lengths) > 1 else "")
        starts = sorted(s for s, d in result if d == days)
        events.own[(country, own)] = starts
        out.append((f"event:{own}", days, starts[0].year, starts[-1].year))
    return out


def weekend_days(start, end):
    i, j = DOW.index(start), DOW.index(end)
    return {DOW[(i + k) % 7] for k in range((j - i) % 7 + 1)}


def days_of(start, days):
    return [start + datetime.timedelta(days=i) for i in range(days)]


def simulate(policy, dates, taken, weekend, shared=None):
    """{day: observed day} a policy gives one occurrence's days; adds to taken.
    shared: {(day, policy): observed day} of holidays already moved, so that two
    holidays on one date under one policy move together."""
    out = {}
    if policy == "none":
        return out
    full_policy, shared = policy, shared if shared is not None else {}
    policy, overlap = policy.removesuffix(",overlap"), policy.endswith(",overlap")

    def free(t, step):
        while not overlap and (t in taken or DOW[t.weekday()] in weekend):
            t += datetime.timedelta(days=step)
        return t

    if policy == "weekend>next":
        t = max(dates) + datetime.timedelta(days=1)
        for d in dates:
            if DOW[d.weekday()] in weekend:
                t = free(t, 1)
                out[d] = t
                taken.add(t)
                t += datetime.timedelta(days=1)
        return out
    moves = {} if policy == "next_weekday" else parse_policy(policy)
    for d in dates:
        dow = DOW[d.weekday()]
        if policy == "next_weekday":
            if dow not in weekend:
                continue
            t, step = d + datetime.timedelta(days=1), 1
        elif dow in moves:
            target, step = moves[dow]
            t = d + datetime.timedelta(days=step)
            while DOW[t.weekday()] != target:
                t += datetime.timedelta(days=step)
        else:
            continue
        if (d, full_policy) in shared:
            out[d] = shared[(d, full_policy)]
            continue
        t = free(t, step)
        out[d] = shared[(d, full_policy)] = t
        taken.add(t)
    return out


def parse_policy(policy):
    """"sat<fri,sun>mon" -> {"sat": ("fri", -1), "sun": ("mon", 1)}."""
    out = {}
    for item in policy.split(","):
        m = re.fullmatch(r"([a-z]{3})([<>])([a-z]{3})", item)
        if not m or m[1] not in DOW or m[3] not in DOW or m[1] == m[3]:
            raise SystemExit(f"bad observed policy {policy!r}")
        out[m[1]] = (m[3], 1 if m[2] == ">" else -1)
    return out


def policy_text(moves):
    return ",".join(f"{d}{'>' if step > 0 else '<'}{t}" for d, (t, step) in sorted(moves.items(), key=lambda kv: DOW.index(kv[0])))


def derived_policy(occ_obs, days):
    """A weekday map read off a series' one-day moves, the most common target per weekday."""
    seen = defaultdict(Counter)
    for start, obs in occ_obs.items():
        if days == 1 and len(obs) == 1 and obs[0] != start:
            step = 1 if obs[0] > start else -1
            seen[DOW[start.weekday()]][(DOW[obs[0].weekday()], step)] += 1
    if not seen:
        return None
    return policy_text({dow: c.most_common(1)[0][0] for dow, c in seen.items()})


def parse_rule(text):
    if text.startswith("event:"):
        return ("event", text)
    for form in ("easter", "orthodox"):
        if text.startswith(form):
            return (form, text)
    if ":" in text:
        return ("nth", text)
    if ">" in text:
        return ("after", text)
    if "<" in text:
        return ("before", text)
    return ("fixed", text)


# ---------------------------------------------------------------------------
# The library's evaluation (sections 8.3 and 8.4)
# ---------------------------------------------------------------------------

def evaluate(rows, country, year, events, weekend):
    """[(date, observed or None, row)] of one year, as the library computes it:
    every rule's days for the year and its neighbours, sorted by date, key and
    day; then each occurrence's weekend days moved by its policy, onto days not
    taken by a holiday of the same kind or an earlier move; a holiday on the same
    date as one already moved under the same policy moves to the same day."""
    entries = []
    for y in (year - 1, year, year + 1):
        for row in rows:
            if row["from"] <= y <= row["to"]:
                for start in rule_dates(parse_rule(row["rule"]), y, country, events):
                    entries.extend((d, row["key"], i + 1, row, start)
                                   for i, d in enumerate(days_of(start, row["days"])))
    entries.sort(key=lambda e: (e[0], e[1], e[2]))
    out = []
    for kind in {e[3]["kind"] for e in entries}:
        mine = [e for e in entries if e[3]["kind"] == kind]
        taken = {e[0] for e in mine}
        done, shared = set(), {}
        for _, key, _, row, start in mine:
            if (key, row["rule"], start) in done:
                continue
            done.add((key, row["rule"], start))
            dates = days_of(start, row["days"])
            moved = simulate(row["observed"], dates, taken, weekend, shared)
            if start.year == year:
                out.extend((x, moved.get(x), row) for x in dates)
    return out


# ---------------------------------------------------------------------------
# One country
# ---------------------------------------------------------------------------

def assign_observed(c):
    """{(kind, name): [observed days]} and the moved days of no series."""
    by_series, orphans = defaultdict(list), defaultdict(list)
    names = defaultdict(list)
    for kind, name in c.series:
        names[kind].append(name)
    for day, kind, base, full in c.observed:
        if (kind, base) in c.series:
            by_series[(kind, base)].append(day)
        elif within := [n for n in names[kind] if n in full]:
            by_series[(kind, max(within, key=len))].append(day)
        else:
            orphans[(kind, full, full, slug(full))].append(day)
    return by_series, orphans


def fit_policies(occ_obs, days, a, b, taken, weekend):
    """[(from, to, policy)] for one rule's years, and the moved days no policy explains.
    occ_obs: {start: [observed days]}."""
    policies = list(POLICIES)
    if (extra := derived_policy(occ_obs, days)) and extra not in policies:
        policies.append(extra)
    policies += [p + ",overlap" for p in policies if p != "none"]
    ok_by_year = {y: set(policies) for y in range(a, b + 1)}
    unexplained = []
    for start, obs in sorted(occ_obs.items()):
        dates = days_of(start, days)
        others = taken - set(obs)
        ok = {p for p in policies if set(simulate(p, dates, set(others), weekend).values()) == set(obs)}
        if not ok:
            ok = {"none"}
            unexplained.extend(obs)
        ok_by_year[start.year] &= ok
    runs = []
    for y in range(a, b + 1):
        ok = ok_by_year[y] or {"none"}
        if runs and runs[-1][2] & ok:
            runs[-1][1] = y
            runs[-1][2] &= ok
        else:
            runs.append([y, y, set(ok)])
    return [(y0, y1, min(ok, key=policies.index)) for y0, y1, ok in runs], unexplained


def build_country(c, events, weekend, report):
    rows, keys = [], set()
    by_series, orphans = assign_observed(c)
    taken = defaultdict(set)
    for kind, dates in c.real.items():
        taken[kind] |= set(dates)
    for day, kind, _, _ in c.observed:
        taken[kind].add(day)

    for (kind, name), s in sorted(c.series.items(), key=lambda kv: (KIND_ORDER[kv[0][0]], kv[0][1])):
        key = slug(name)
        if key in keys:
            key = f"{key}_{kind}"
        keys.add(key)
        observed = sorted(by_series.get((kind, name), []))
        for n_part, (part_occ, how, result) in enumerate(fit_series(occurrences(s["dates"]), c.code, events, name)):
            part_key = key if n_part == 0 else f"{key}_{n_part + 1}"
            keys.add(part_key)
            part_set = set(part_occ)
            for rule, days, a, b in commit(how, result, c.code, part_key, events):
                # The rule's occurrences, each with the observed days nearest it.
                mine = {st: [] for y in range(a, b + 1)
                        for st in rule_dates(parse_rule(rule), y, c.code, events) if (st, days) in part_set}
                rest = []
                for o in observed:
                    near = min(mine, key=lambda st: (abs((o - st).days - (days - 1) / 2), st), default=None)
                    if near is not None and abs((o - near).days) <= days + 7:
                        mine[near].append(o)
                    else:
                        rest.append(o)
                observed = rest
                native = Counter(s["names"].get(d) for st in mine for d in days_of(st, days))
                native_name = clean(max(native.items(), key=lambda kv: (kv[1], kv[0] or ""))[0] or name)
                runs, unexplained = fit_policies(mine, days, a, b, taken[kind], weekend)
                for y0, y1, policy in runs:
                    rows.append({"country": c.code, "key": part_key, "name": native_name, "name_en": clean(name),
                                 "kind": kind, "rule": rule, "days": days, "observed": policy,
                                 "from": y0, "to": y1})
                if unexplained:
                    label = c.observed_label.get(kind) or "%s"
                    orphans[(kind, f"{name} (observed)", label.replace("%s", native_name),
                             f"{part_key}_observed")].extend(unexplained)
        if observed:
            orphans[(kind, f"{name} (observed)", f"{name} (observed)", f"{key}_observed")].extend(observed)
    # Moved days no policy explains, and moved days of no series: the country's own events.
    for (kind, name_en, native, key), dates in sorted(orphans.items()):
        while key in keys:
            key += "_2"
        keys.add(key)
        own = f"{c.code.lower()}_{key}"
        dates = sorted(set(dates))
        events.own[(c.code, own)] = dates
        rows.append({"country": c.code, "key": key, "name": clean(native), "name_en": clean(name_en),
                     "kind": kind, "rule": f"event:{own}", "days": 1, "observed": "none",
                     "from": dates[0].year, "to": dates[-1].year, "moved": True})
        report["moved days kept as events"] += len(dates)
    return rows


def add_moved(c, rows, events, kind, key, name_en, native, dates):
    """Moved days kept as the country's own event, under `key`, one row."""
    if any(r["key"] == key and r["kind"] != kind for r in rows):
        key = f"{key}_{kind}"
    own = f"{c.code.lower()}_{key}"
    row = next((r for r in rows if r["rule"] == f"event:{own}"), None)
    dates = sorted(set(dates) | set(events.own.get((c.code, own), ())))
    events.own[(c.code, own)] = dates
    if row is None:
        rows.append({"country": c.code, "key": key, "name": clean(native), "name_en": clean(name_en),
                     "kind": kind, "rule": f"event:{own}", "days": 1, "observed": "none",
                     "from": dates[0].year, "to": dates[-1].year, "moved": True})
    else:
        row["from"], row["to"] = dates[0].year, dates[-1].year


def repair(c, rows, events, weekend, report):
    """Make the observed days agree with python-holidays where the fitted policies,
    evaluated with the library's collisions, do not: a year whose move is wrong
    gets no move, and the moved days python-holidays has become events."""
    for _ in range(10):
        diffs = [d for d in check_country(c, rows, events, weekend) if d[1] == "observed"]
        if not diffs:
            return
        moved_to = defaultdict(list)
        for y in range(c.first, c.last + 1):
            for d, obs, row in evaluate(rows, c.code, y, events, weekend):
                if obs and not row.get("moved"):
                    moved_to[(row["kind"], obs)].append((row, d.year))
        stop = defaultdict(set)                  # id(row) -> years with no move
        for kind, _, day, ours, theirs in diffs:
            if ours > theirs:
                for row, year in moved_to[(kind, day)]:
                    stop[id(row)].add(year)
            for d, k, base, full in c.observed:
                if d == day and k == kind and ours < theirs:
                    series = next((r for r in rows if r["kind"] == kind and r["name_en"] == base
                                   and not r.get("moved")), None)
                    if series:
                        label = c.observed_label.get(kind) or "%s"
                        add_moved(c, rows, events, kind, f"{series['key']}_observed", f"{base} (observed)",
                                  label.replace("%s", series["name"]), [day])
                    else:
                        add_moved(c, rows, events, kind, slug(full), full, full, [day])
                    report["moved days kept as events"] += 1
        for row in [r for r in rows if id(r) in stop]:
            rows.remove(row)
            years = stop[id(row)]
            start = row["from"]
            for y in range(row["from"], row["to"] + 2):
                if y in years or y == row["to"] + 1:
                    if start < y:
                        rows.append({**row, "from": start, "to": y - 1})
                    if y in years:
                        rows.append({**row, "from": y, "to": y, "observed": "none"})
                    start = y + 1
            report["years with the move dropped"] += len(years)
    report["countries the repair did not settle"] += 1


def check_country(c, rows, events, weekend):
    """[(kind, what, date, ours, theirs)] where the rows and python-holidays differ."""
    ours = {"date": defaultdict(Counter), "observed": defaultdict(Counter)}
    first, last = datetime.date(c.first, 1, 1), datetime.date(c.last, 12, 31)
    for y in range(c.first, c.last + 1):
        for d, obs, row in evaluate(rows, c.code, y, events, weekend):
            if row.get("moved"):
                ours["observed"][row["kind"]][d] += 1
                continue
            ours["date"][row["kind"]][d] += 1
            if obs and first <= obs <= last:
                ours["observed"][row["kind"]][obs] += 1
    theirs = {"date": defaultdict(Counter), "observed": defaultdict(Counter)}
    for kind, dates in c.real.items():
        theirs["date"][kind].update(dates)
    for day, kind, _, _ in c.observed:
        theirs["observed"][kind][day] += 1
    diffs = []
    for what in ("date", "observed"):
        for kind in set(ours[what]) | set(theirs[what]):
            a, b = ours[what][kind], theirs[what][kind]
            diffs += [(kind, what, d, a[d], b[d]) for d in set(a) | set(b) if a[d] != b[d]]
    return sorted(diffs, key=lambda x: (x[2], x[0]))


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def read_tsv(path):
    lines = [l for l in Path(path).read_text(encoding="utf-8").splitlines() if l and not l.startswith("#")]
    header = lines[0].split("\t")
    return [dict(zip(header, l.split("\t"))) for l in lines[1:]]


def continued(rows):
    """The rows with name and name_en left empty where the row above has the same
    country, key and names (a run that changes only its years, rule or policy)."""
    out, prev = [], None
    for r in rows:
        key = (r["country"], r["key"], r["name"], r["name_en"])
        out.append({**r, "name": "", "name_en": ""} if key == prev else r)
        prev = key
    return out


def filled(rows):
    """continued() undone, as the library's loader does it."""
    out = []
    for r in rows:
        if not r["name"] and not r["name_en"] and out and (out[-1]["country"], out[-1]["key"]) == (r["country"], r["key"]):
            r = {**r, "name": out[-1]["name"], "name_en": out[-1]["name_en"]}
        out.append(r)
    return out


def write(path, comment, columns, rows):
    out = [f"# {line}" for line in comment] + ["\t".join(columns)]
    out += ["\t".join(str(r[c]) for c in columns) for r in rows]
    Path(path).write_text("\n".join(out) + "\n", encoding="utf-8", newline="\n")
    return Path(path).stat().st_size


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tier", choices=["lite", "full", "both"], default="both")
    ap.add_argument("--resources", default="resources")
    ap.add_argument("--report", help="write the per-country report to this file")
    ap.add_argument("--countries", nargs="*", help="only these countries, for a look; writes no table")
    a = ap.parse_args()
    if holidays.__version__ != HOLIDAYS_VERSION:
        raise SystemExit(f"python-holidays {holidays.__version__} is installed; the tables pin {HOLIDAYS_VERSION}")

    full = {r["cca2"] for r in read_tsv(Path(a.resources) / "full" / "countries.tsv")}
    weekends = {}
    for r in read_tsv(Path(a.resources) / "full" / "locales.tsv"):
        weekends.setdefault(r["country"], weekend_days(r["weekend_start"], r["weekend_end"]))
    supported = sorted(holidays.list_supported_countries(include_aliases=False))
    events = Events()
    report, lines, rows, spans = Counter(), [], [], {}
    for code in a.countries or [c for c in supported if c in full]:
        c = Country(code)
        weekend = weekends.get(code, {"sat", "sun"})
        country_rows = build_country(c, events, weekend, report)
        repair(c, country_rows, events, weekend, report)
        country_rows.sort(key=lambda r: (KIND_ORDER[r["kind"]], r["key"], r["from"]))
        diffs = check_country(c, country_rows, events, weekend)
        rows.extend(country_rows)
        spans[code] = (c.first, c.last)         # also for a country with no holidays (BV, HM, IO)
        forms = Counter(parse_rule(r["rule"])[0] for r in country_rows)
        note = "" if c.python_weekend == weekend else \
            f"; weekend {','.join(sorted(weekend))} (python-holidays {','.join(sorted(c.python_weekend))})"
        lines.append(f"{code} {c.first}-{c.last}: {len({r['key'] for r in country_rows})} holidays in "
                     f"{len(country_rows)} rows ({', '.join(f'{k} {v}' for k, v in forms.most_common())}); "
                     f"{len(diffs)} differences{note}")
        lines += [f"    {d[0]} {d[1]} {d[2]}: ours {d[3]}, python-holidays {d[4]}" for d in diffs[:10]]
        report["countries"] += 1
        report["countries with differences"] += bool(diffs)
        report["differences"] += len(diffs)
    report["rows"] = len(rows)
    for form, n in Counter(parse_rule(r["rule"])[0] for r in rows).most_common():
        report[f"rules: {form}"] = n
    for policy, n in Counter(r["observed"] for r in rows).most_common():
        report[f"observed: {policy}"] = n
    lines.append("countries.tsv codes python-holidays does not cover: " + ", ".join(sorted(full - set(supported))))
    if a.report:
        Path(a.report).write_text("\n".join(lines) + "\n", encoding="utf-8")
    else:
        print("\n".join(lines))
    for k, v in report.items():
        print(f"{k}: {v}")
    if a.countries:
        return

    comment = [f"source: python-holidays {HOLIDAYS_VERSION} (MIT); rules inferred by scripts/prepare_holidays.py"]
    holiday_comment = comment + ["an empty name and name_en repeat those of the row above, which has the same country and key"]
    for tier in (["lite", "full"] if a.tier == "both" else [a.tier]):
        in_tier = {r["cca2"] for r in read_tsv(Path(a.resources) / tier / "countries.tsv")}
        tier_rows = [{**r, "from_year": r["from"], "to_year": r["to"]} for r in rows if r["country"] in in_tier]
        need = defaultdict(set)              # shared event -> the years its rules use, and their neighbours
        for r in tier_rows:
            if r["rule"].startswith("event:"):
                need[event_parts(r["rule"])[0]].update(range(r["from"] - 1, r["to"] + 2))
        used = set(need)
        ev_rows = [{"event": k, "country": "", "date": d.isoformat()}
                   for k in sorted(used & set(events.shared)) for d in events.shared[k] if d.year in need[k]]
        ev_rows += [{"event": k, "country": cc, "date": d.isoformat()}
                    for (cc, k), changes in sorted(events.override.items()) if cc in in_tier and k in used
                    for d in sorted(changes.values())]
        ev_rows += [{"event": k, "country": cc, "date": d.isoformat()}
                    for (cc, k), dates in sorted(events.own.items()) if cc in in_tier for d in dates]
        columns = ["country", "key", "name", "name_en", "kind", "rule", "days", "observed", "from_year", "to_year"]
        size = write(Path(a.resources) / tier / "holidays.tsv", holiday_comment, columns, continued(tier_rows))
        read_back = filled(read_tsv(Path(a.resources) / tier / "holidays.tsv"))
        if read_back != [{c: str(r[c]) for c in columns} for r in tier_rows]:
            raise SystemExit(f"{tier}/holidays.tsv does not read back as the rows written")
        ev_size = write(Path(a.resources) / tier / "holiday_events.tsv", comment,
                        ["event", "country", "date"], ev_rows)
        write(Path(a.resources) / tier / "holiday_years.tsv", comment, ["country", "from_year", "to_year"],
              [{"country": cc, "from_year": f, "to_year": t} for cc, (f, t) in sorted(spans.items()) if cc in in_tier])
        print(f"{tier}: {len(tier_rows)} holiday rows in {len({r['country'] for r in tier_rows})} countries, "
              f"{size / 1024:.0f} KB; {len(ev_rows)} event rows, {ev_size / 1024:.0f} KB")


if __name__ == "__main__":
    main()
