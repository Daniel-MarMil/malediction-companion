# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0
"""Polite downloads from malediction.gg.

- One request at a time, with at least MIN_INTERVAL seconds between the end of one and the
  start of the next.
- robots.txt is read before anything else, and every URL is checked against it.
- Every response has a size limit.
- 401 or 403 stops the import. 429 or 5xx waits for Retry-After once; if it happens again,
  the import stops.
- Only the site's own host, over HTTPS; redirects to other hosts are refused.
"""

from __future__ import annotations

import email.utils
import time
import urllib.error
import urllib.request
import urllib.robotparser
from collections.abc import Callable
from dataclasses import dataclass, field
from typing import Protocol
from urllib.parse import urlsplit

from .errors import FetchError, ImportStopped

SITE = "https://malediction.gg"
USER_AGENT = "seekers-companion-content-importer/1 (+https://codeberg.org/Daniel-MarMil/seekers-companion)"
MIN_INTERVAL = 2.0
# Without Retry-After, a 429 or 5xx waits this long before the only retry.
DEFAULT_RETRY_AFTER = 60.0
# A longer Retry-After stops the import instead of waiting.
MAX_RETRY_AFTER = 600.0
ROBOTS_MAX_BYTES = 512 * 1024
TIMEOUT = 30.0


@dataclass
class Response:
    status: int
    body: bytes = b""
    headers: dict[str, str] = field(default_factory=dict)  # lowercase names


class Transport(Protocol):
    def get(self, url: str, headers: dict[str, str], max_bytes: int) -> Response: ...


class _SameHostRedirects(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):  # noqa: ANN001, ANN201
        old, new = urlsplit(req.full_url), urlsplit(newurl)
        if new.scheme != "https" or new.hostname != old.hostname:
            raise FetchError(f"{req.full_url}: redirect to another site refused: {newurl}")
        return super().redirect_request(req, fp, code, msg, headers, newurl)


class UrllibTransport:
    """The real network, through urllib. The fetcher never sees an exception from urllib."""

    def __init__(self, timeout: float = TIMEOUT) -> None:
        self._timeout = timeout
        self._opener = urllib.request.build_opener(_SameHostRedirects())

    def get(self, url: str, headers: dict[str, str], max_bytes: int) -> Response:
        request = urllib.request.Request(url, headers=headers)
        try:
            with self._opener.open(request, timeout=self._timeout) as reply:
                names = {name.lower(): value for name, value in reply.headers.items()}
                return Response(reply.status, _read_limited(reply, names, max_bytes, url), names)
        except urllib.error.HTTPError as error:
            names = {name.lower(): value for name, value in (error.headers or {}).items()}
            error.close()
            return Response(error.code, b"", names)
        except (urllib.error.URLError, OSError) as error:
            raise FetchError(f"{url}: {error}") from error


def _read_limited(reply, headers: dict[str, str], max_bytes: int, url: str) -> bytes:  # noqa: ANN001
    declared = headers.get("content-length", "")
    if declared.isdigit() and int(declared) > max_bytes:
        raise FetchError(f"{url}: {declared} bytes, more than the limit of {max_bytes}")
    body = reply.read(max_bytes + 1)
    if len(body) > max_bytes:
        raise FetchError(f"{url}: more than the limit of {max_bytes} bytes")
    return body


def retry_delay(headers: dict[str, str], now: float) -> float:
    """Seconds that Retry-After asks for: a number of seconds or an HTTP date."""
    value = headers.get("retry-after", "").strip()
    if not value:
        return DEFAULT_RETRY_AFTER
    if value.isdigit():
        return float(value)
    try:
        when = email.utils.parsedate_to_datetime(value)
    except (TypeError, ValueError):
        return DEFAULT_RETRY_AFTER
    return max(0.0, when.timestamp() - now)


class Fetcher:
    def __init__(
        self,
        transport: Transport,
        *,
        site: str = SITE,
        user_agent: str = USER_AGENT,
        min_interval: float = MIN_INTERVAL,
        clock: Callable[[], float] = time.monotonic,
        sleep: Callable[[float], None] = time.sleep,
        now: Callable[[], float] = time.time,
        log: Callable[[str], None] = lambda message: None,
    ) -> None:
        self._transport = transport
        self._site = site.rstrip("/")
        self._host = urlsplit(self._site).hostname
        self._user_agent = user_agent
        self._min_interval = min_interval
        self._clock = clock
        self._sleep = sleep
        self._now = now
        self._log = log
        self._last_request_end: float | None = None
        self._robots: urllib.robotparser.RobotFileParser | None = None
        self.requests = 0

    def get(self, url: str, max_bytes: int) -> bytes:
        """Downloads url. Raises FetchError if this URL fails and ImportStopped if the import must stop."""
        parts = urlsplit(url)
        if parts.scheme != "https" or parts.hostname != self._host:
            raise ImportStopped(f"{url}: only {self._site} is read")
        robots = self._load_robots()
        if not robots.can_fetch(self._user_agent, url):
            raise ImportStopped(f"{url}: robots.txt does not allow it")
        return self._get(url, max_bytes)

    def _load_robots(self) -> urllib.robotparser.RobotFileParser:
        if self._robots is not None:
            return self._robots
        url = self._site + "/robots.txt"
        robots = urllib.robotparser.RobotFileParser(url)
        try:
            body = self._get(url, ROBOTS_MAX_BYTES)
        except FetchError as error:
            if error.status not in (404, 410):
                raise ImportStopped(f"cannot read robots.txt: {error}") from error
            robots.allow_all = True  # No robots.txt: everything is allowed.
        else:
            robots.parse(body.decode("utf-8", errors="replace").splitlines())
        self._robots = robots
        return robots

    def _get(self, url: str, max_bytes: int) -> bytes:
        for attempt in (1, 2):
            reply = self._request(url, max_bytes)
            if reply.status == 200:
                return reply.body
            if reply.status in (401, 403):
                raise ImportStopped(f"{url}: HTTP {reply.status}; the site refused access, so the import stops")
            if reply.status == 429 or reply.status >= 500:
                if attempt == 2:
                    raise ImportStopped(f"{url}: HTTP {reply.status} again after waiting; the import stops")
                delay = retry_delay(reply.headers, self._now())
                if delay > MAX_RETRY_AFTER:
                    raise ImportStopped(f"{url}: HTTP {reply.status} asks to wait {delay:.0f} s; the import stops")
                self._log(f"HTTP {reply.status} at {url}; waiting {delay:.0f} s as Retry-After asks")
                self._sleep(delay)
                continue
            raise FetchError(f"{url}: HTTP {reply.status}", reply.status)
        raise AssertionError("unreachable")  # pragma: no cover

    def _request(self, url: str, max_bytes: int) -> Response:
        if self._last_request_end is not None:
            remaining = self._last_request_end + self._min_interval - self._clock()
            if remaining > 0:
                self._sleep(remaining)
        headers = {"User-Agent": self._user_agent, "Accept-Encoding": "identity"}
        try:
            self.requests += 1
            return self._transport.get(url, headers, max_bytes)
        finally:
            self._last_request_end = self._clock()
