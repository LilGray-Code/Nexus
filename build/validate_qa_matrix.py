#!/usr/bin/env python3
"""Validate the Nexus subsystem QA matrix against the checked-out repository.

This audits coverage metadata; it does not execute the C tests or certify
hardware. The owning workflows remain the executable test gates.
"""
from __future__ import annotations

import csv
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MATRIX = ROOT / "docs/quality/test-matrix.csv"

REQUIRED_SUBSYSTEMS = {
    "kernel-core",
    "memory",
    "cpu",
    "filesystem",
    "graphics",
    "gpu",
    "display",
    "audio",
    "network",
    "wifi",
    "usb",
    "storage",
    "installer",
    "application-compatibility",
    "security",
    "performance",
    "qemu-integration",
    "physical-hardware",
    "settings",
    "firmware-acpi",
    "input",
}
ALLOWED_EVIDENCE = {
    "host-automated",
    "partial-automated",
    "qemu-automated",
    "manual-required",
}
REQUIRED_COLUMNS = {
    "subsystem",
    "test_files",
    "workflow_files",
    "evidence_status",
    "negative_tests",
    "recovery_tests",
    "qemu_vm_coverage",
    "physical_hardware_coverage",
    "known_gap",
}


def split_paths(value: str) -> list[str]:
    return [item.strip() for item in value.split(";") if item.strip()]


def main() -> int:
    errors: list[str] = []
    if not MATRIX.is_file():
        print(f"FAIL: missing QA matrix: {MATRIX.relative_to(ROOT)}")
        return 1

    with MATRIX.open("r", encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream)
        columns = set(reader.fieldnames or ())
        missing_columns = REQUIRED_COLUMNS - columns
        if missing_columns:
            errors.append("missing columns: " + ", ".join(sorted(missing_columns)))
        rows = list(reader)

    seen: set[str] = set()
    found: set[str] = set()
    for number, row in enumerate(rows, start=2):
        subsystem = (row.get("subsystem") or "").strip()
        prefix = f"row {number} ({subsystem or 'unnamed'})"
        if not subsystem:
            errors.append(f"{prefix}: empty subsystem")
            continue
        if subsystem in seen:
            errors.append(f"{prefix}: duplicate subsystem")
        seen.add(subsystem)
        found.add(subsystem)

        status = (row.get("evidence_status") or "").strip()
        if status not in ALLOWED_EVIDENCE:
            errors.append(f"{prefix}: invalid evidence_status {status!r}")
        if "complete" in status.lower():
            errors.append(f"{prefix}: 'complete' is not an evidence level")

        test_files = split_paths(row.get("test_files") or "")
        workflow_files = split_paths(row.get("workflow_files") or "")
        if not test_files:
            errors.append(f"{prefix}: no test or evidence artifact listed")
        if not workflow_files:
            errors.append(f"{prefix}: no owning workflow or MANUAL marker listed")

        workflow_text: list[str] = []
        if workflow_files == ["MANUAL"]:
            if status != "manual-required":
                errors.append(f"{prefix}: MANUAL requires manual-required status")
        else:
            if "MANUAL" in workflow_files:
                errors.append(f"{prefix}: MANUAL cannot be mixed with workflow paths")
            for relative in workflow_files:
                path = ROOT / relative
                if not path.is_file():
                    errors.append(f"{prefix}: missing workflow {relative}")
                else:
                    try:
                        workflow_text.append(path.read_text(encoding="utf-8"))
                    except OSError as exc:
                        errors.append(f"{prefix}: cannot read workflow {relative}: {exc}")

        for relative in test_files:
            path = ROOT / relative
            if not path.is_file():
                errors.append(f"{prefix}: missing test/evidence file {relative}")
            elif workflow_files != ["MANUAL"]:
                # Prevent a test from being listed as coverage without belonging
                # to at least one of the declared executable workflows.
                if not any(relative in text for text in workflow_text):
                    errors.append(
                        f"{prefix}: {relative} is not referenced by its declared workflow(s)"
                    )

        for field in (
            "negative_tests",
            "recovery_tests",
            "qemu_vm_coverage",
            "physical_hardware_coverage",
            "known_gap",
        ):
            value = (row.get(field) or "").strip()
            if len(value) < 12:
                errors.append(f"{prefix}: {field} must describe evidence or an explicit gap")

        if status == "qemu-automated" and "qemu" not in subsystem:
            errors.append(f"{prefix}: qemu-automated status reserved for QEMU integration")
        if subsystem == "physical-hardware" and status != "manual-required":
            errors.append(f"{prefix}: bare-metal evidence must remain manual-required")
        if subsystem == "physical-hardware" and workflow_files != ["MANUAL"]:
            errors.append(f"{prefix}: physical validation cannot be represented by CI")

    missing = REQUIRED_SUBSYSTEMS - found
    extra = found - REQUIRED_SUBSYSTEMS
    if missing:
        errors.append("missing required subsystems: " + ", ".join(sorted(missing)))
    if extra:
        errors.append("unexpected subsystem rows: " + ", ".join(sorted(extra)))

    if errors:
        for error in errors:
            print("FAIL:", error)
        print(f"QA matrix audit failed with {len(errors)} issue(s).")
        return 1

    print(
        f"PASS: {len(rows)} subsystem rows, {len(REQUIRED_SUBSYSTEMS)} required "
        "areas, every test/evidence file and owning workflow exists, and "
        "failure/recovery evidence is documented."
    )
    print("NOTE: this validates the test catalog only; it does not run tests or certify hardware.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
