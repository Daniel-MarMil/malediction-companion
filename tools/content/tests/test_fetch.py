# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0
"""Polite downloads: pauses, robots.txt, size limits and how the site's refusals are handled."""

from __future__ import annotations

import io
import unittest

from mc_content.errors import FetchError, ImportStopped
from mc_content.fetch import MIN_INTERVAL, Fetcher, Response, _read_limited, retry_delay

from .fixtures import SITE, FakeTime

PAGE = SITE + "/card/a-test/"


class ScriptedTransport:
    """Answers each URL with the replies given, in order, and records when each request starts."""

    def __init__(self, time: FakeTime, replies: dict[str, list[Response]]) -> None:
        self.time = time
        self.replies = replies
        self.calls: list[tuple[str, float, dict[str, str]]] = []

    def get(self, url: str, headers: dict[str, str], max_bytes: int) -> Response:
        self.calls.append((url, self.time.now, headers))
        self.time.now += 0.5  # Each request takes half a second.
        queue = self.replies.get(url, [])
        return queue.pop(0) if queue else Response(200, b"ok")


def make(replies: dict[str, list[Response]] | None = None, robots: bytes = b"User-agent: *\nDisallow:\n"):
    time = FakeTime()
    all_replies = {SITE + "/robots.txt": [Response(200, robots)]}
    all_replies.update(replies or {})
    transport = ScriptedTransport(time, all_replies)
    fetcher = Fetcher(transport, clock=time.clock, sleep=time.sleep, now=lambda: 0.0)
    return fetcher, transport, time


class PausesAndRobots(unittest.TestCase):
    def test_reads_robots_first_and_waits_between_requests(self) -> None:
        fetcher, transport, _ = make()
        for number in range(3):
            fetcher.get(f"{SITE}/card/test-{number}/", 1000)
        urls = [call[0] for call in transport.calls]
        self.assertEqual(urls[0], SITE + "/robots.txt")
        self.assertEqual(len(urls), 4)
        # At least MIN_INTERVAL from the end of one request (start + 0.5 s) to the start of the next.
        starts = [call[1] for call in transport.calls]
        for previous, current in zip(starts, starts[1:]):
            self.assertGreaterEqual(current - (previous + 0.5), MIN_INTERVAL - 1e-9)

    def test_sends_the_user_agent_with_the_repository_and_no_email(self) -> None:
        fetcher, transport, _ = make()
        fetcher.get(PAGE, 1000)
        agent = transport.calls[-1][2]["User-Agent"]
        self.assertIn("seekers-companion-content-importer", agent)
        self.assertIn("https://codeberg.org/Daniel-MarMil/seekers-companion", agent)
        self.assertNotIn("@", agent)

    def test_robots_can_forbid_a_page(self) -> None:
        fetcher, transport, _ = make(robots=b"User-agent: *\nDisallow: /card/\n")
        with self.assertRaises(ImportStopped):
            fetcher.get(PAGE, 1000)
        self.assertEqual([call[0] for call in transport.calls], [SITE + "/robots.txt"])

    def test_without_robots_everything_is_allowed(self) -> None:
        fetcher, _, _ = make({SITE + "/robots.txt": [Response(404)]})
        self.assertEqual(fetcher.get(PAGE, 1000), b"ok")

    def test_robots_that_cannot_be_read_stop_the_import(self) -> None:
        fetcher, _, _ = make({SITE + "/robots.txt": [Response(403)]})
        with self.assertRaises(ImportStopped):
            fetcher.get(PAGE, 1000)

    def test_only_the_site_over_https(self) -> None:
        fetcher, transport, _ = make()
        for url in ("https://example.invalid/card/x/", "http://malediction.gg/card/x/"):
            with self.assertRaises(ImportStopped):
                fetcher.get(url, 1000)
        self.assertEqual(transport.calls, [])


class Refusals(unittest.TestCase):
    def test_401_and_403_stop_the_import(self) -> None:
        for status in (401, 403):
            fetcher, transport, _ = make({PAGE: [Response(status)]})
            with self.assertRaises(ImportStopped):
                fetcher.get(PAGE, 1000)
            self.assertEqual(len(transport.calls), 2)  # robots.txt and the page, no retry

    def test_429_waits_for_retry_after_and_retries_once(self) -> None:
        fetcher, transport, time = make({PAGE: [Response(429, headers={"retry-after": "7"}), Response(200, b"late")]})
        self.assertEqual(fetcher.get(PAGE, 1000), b"late")
        self.assertIn(7.0, time.sleeps)
        self.assertEqual(len(transport.calls), 3)

    def test_a_second_429_or_5xx_stops_the_import(self) -> None:
        fetcher, _, _ = make({PAGE: [Response(503, headers={"retry-after": "1"}), Response(500)]})
        with self.assertRaises(ImportStopped):
            fetcher.get(PAGE, 1000)

    def test_a_very_long_retry_after_stops_instead_of_waiting(self) -> None:
        fetcher, _, time = make({PAGE: [Response(429, headers={"retry-after": "86400"})]})
        with self.assertRaises(ImportStopped):
            fetcher.get(PAGE, 1000)
        self.assertNotIn(86400.0, time.sleeps)

    def test_retry_after_as_a_date_or_missing(self) -> None:
        self.assertEqual(retry_delay({"retry-after": "Thu, 01 Jan 1970 00:00:30 GMT"}, now=10.0), 20.0)
        self.assertEqual(retry_delay({"retry-after": "Thu, 01 Jan 1970 00:00:30 GMT"}, now=100.0), 0.0)
        self.assertEqual(retry_delay({}, now=0.0), 60.0)
        self.assertEqual(retry_delay({"retry-after": "soon"}, now=0.0), 60.0)

    def test_404_fails_only_that_page(self) -> None:
        fetcher, _, _ = make({PAGE: [Response(404)]})
        with self.assertRaises(FetchError) as caught:
            fetcher.get(PAGE, 1000)
        self.assertEqual(caught.exception.status, 404)
        self.assertEqual(fetcher.get(SITE + "/card/other/", 1000), b"ok")


class SizeLimit(unittest.TestCase):
    def test_a_declared_size_above_the_limit_is_refused(self) -> None:
        with self.assertRaises(FetchError):
            _read_limited(io.BytesIO(b"x"), {"content-length": "11"}, 10, PAGE)

    def test_a_body_above_the_limit_is_refused(self) -> None:
        with self.assertRaises(FetchError):
            _read_limited(io.BytesIO(b"x" * 11), {}, 10, PAGE)
        self.assertEqual(_read_limited(io.BytesIO(b"x" * 10), {"content-length": "10"}, 10, PAGE), b"x" * 10)


if __name__ == "__main__":
    unittest.main()
