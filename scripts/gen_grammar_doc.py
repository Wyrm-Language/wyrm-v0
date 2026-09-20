#!/usr/bin/env python3
"""Extract '#>' documentation lines from parser.wy and print them to stdout."""

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PARSER_WY = os.path.join(SCRIPT_DIR, "..", "wy", "wyrm", "parser.wy")


INCLUDE_PREFIX = "#>>include:"


def strip_doc_prefix(line):
    rest = line[2:]  # drop leading '#>'
    i = 0
    stripped = 0
    while i < len(rest) and stripped < 2 and rest[i] == " ":
        i += 1
        stripped += 1
    return rest[i:]


def process_file(path):
    directory = os.path.dirname(path)
    with open(path, "r") as f:
        for raw_line in f:
            line = raw_line.rstrip("\n")
            if line.startswith(INCLUDE_PREFIX):
                include_name = line[len(INCLUDE_PREFIX):].strip()
                include_path = os.path.join(directory, include_name)
                process_file(include_path)
            elif line.startswith("#>"):
                print(strip_doc_prefix(line))


def main():
    process_file(PARSER_WY)


if __name__ == "__main__":
    sys.exit(main())
