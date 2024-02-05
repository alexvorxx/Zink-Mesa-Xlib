from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import TYPE_CHECKING, Any

if TYPE_CHECKING:
    from lava.utils import LogFollower

from lava.exceptions import MesaCIKnownIssueException
from lava.utils.console_format import CONSOLE_LOG
from lava.utils.constants import KNOWN_ISSUE_R8152_MAX_CONSECUTIVE_COUNTER, LOG_DEBUG_FEEDBACK_NOISE
from lava.utils.log_section import LogSectionType


@dataclass
class LAVALogHints:
    log_follower: LogFollower
    r8152_issue_consecutive_counter: int = field(default=0, init=False)
    reboot_counter: int = field(default=0, init=False)

    def raise_known_issue(self, message) -> None:
        raise MesaCIKnownIssueException(
            "Found known issue: "
            f"{CONSOLE_LOG['FG_MAGENTA']}"
            f"{message}"
            f"{CONSOLE_LOG['RESET']}"
        )

    def detect_failure(self, new_lines: list[dict[str, Any]]):
        for line in new_lines:
            if line["msg"] == LOG_DEBUG_FEEDBACK_NOISE:
                continue
            self.detect_r8152_issue(line)
            self.detect_forced_reboot(line)

    def detect_r8152_issue(self, line):
        if self.log_follower.phase in (
            LogSectionType.LAVA_BOOT,
            LogSectionType.TEST_CASE,
        ) and line["lvl"] in ("feedback", "target"):
            if re.search(r"r8152 \S+ eth0: Tx status -71", line["msg"]):
                self.r8152_issue_consecutive_counter += 1
                return

            if self.r8152_issue_consecutive_counter >= KNOWN_ISSUE_R8152_MAX_CONSECUTIVE_COUNTER:
                if re.search(
                    r"nfs: server \d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3} not responding, still trying",
                    line["msg"],
                ):
                    self.raise_known_issue(
                        "Probable network issue failure encountered, retrying the job"
                    )

        # Reset the status, as the `nfs... still trying` complaint was not detected
        self.r8152_issue_consecutive_counter = 0

    def detect_forced_reboot(self, line: dict[str, Any]) -> None:
        if (
            self.log_follower.phase == LogSectionType.TEST_CASE
            and line["lvl"] == "feedback"
        ):
            if re.search(r"^Reboot requested", line["msg"]):
                self.reboot_counter += 1

                if self.reboot_counter > 0:
                    self.raise_known_issue(
                        "Forced reboot detected during test phase, failing the job..."
                    )
