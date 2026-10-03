# SPDX-License-Identifier: GPL-3.0-only
"""XML UI compilation: parser → document model → native resources → C output."""

from .generator import generate
from .resources import Resources

__all__ = ["Resources", "generate"]
