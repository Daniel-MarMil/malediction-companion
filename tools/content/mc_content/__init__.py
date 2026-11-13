# SPDX-FileCopyrightText: 2026 Daniel Martín Millet
# SPDX-License-Identifier: Apache-2.0
"""Importer of the official card pack (ADR-06, ADR-09, ADR-12 and ADR-13).

It reads the public pages of the malediction.gg card library, slowly and with a cache outside
the repository, and writes the pack (format 2) and its images to content/official/. Nothing it
downloads goes into the repository.
"""
