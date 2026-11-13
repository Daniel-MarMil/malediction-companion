# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0
"""Errors of the importer."""

from __future__ import annotations


class ImportStopped(Exception):
    """The import must stop at once and write nothing: the site refused us, robots.txt forbids
    a page, or the input is not what the importer expects."""


class FetchError(Exception):
    """One page or image could not be downloaded; the import goes on and reports it."""

    def __init__(self, message: str, status: int | None = None) -> None:
        super().__init__(message)
        self.status = status  # The HTTP status, if the server answered.


class PageFormatError(Exception):
    """A card page does not have the expected shape; the importer does not guess."""
