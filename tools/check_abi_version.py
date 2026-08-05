#!/usr/bin/env python3
"""Refuse a change to the Program ABI header that does not bump TGL_ABI_VERSION.

The version constant exists to stop the Grid loading a Program built against a different layout.
Nothing enforces it by itself: a forgotten bump leaves both sides agreeing on the number while
disagreeing about the bytes, which is the exact silent memory corruption the number is kept for. So
the header's fingerprint is recorded beside it and checked here.

The fingerprint hashes the header with the version line removed, so bumping the version is not itself
a change that demands another bump, and with comments and blank space removed, so that rewording a
doc comment does not either. What is left is the declarations, which is what the version is about: a
guard that demanded a version bump to fix a typo would be resented and then circumvented.

String literals are left intact, so rewording a static assertion's message does demand a bump. That
is the conservative side of a line that has to fall somewhere: the cost of a needless bump is mild
annoyance, and the cost of a missed one is two builds agreeing on a number while disagreeing about
the bytes. Deleting the fingerprint file and re-recording is the escape, and it shows up in a diff.

    check    the recorded fingerprint still describes the header. This is what CI runs.
    update   re-record it, refusing unless the version moved when the content did.

Exit code is 0 when the tree is consistent and 1 when it is not.
"""

import argparse
import hashlib
import io
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
HEADER = os.path.join(REPO, "libs", "program-abi", "include", "tgl", "tgl_program_abi.h")
FINGERPRINT = os.path.join(REPO, "libs", "program-abi", "abi_fingerprint.txt")

VERSION_MACRO = "TGL_ABI_VERSION"


def parse_version_define(line):
    """The value of a `#define TGL_ABI_VERSION <n>` line, or None if this is not one.

    Tokenised rather than matched with a regular expression. The obvious pattern needs runs of
    optional whitespace either side of a group that can fail, which backtracks quadratically on a
    line padded with tabs; splitting on whitespace is linear, and easier to read besides.
    """
    tokens = line.split()

    # Both `#define X 1u` and `# define X 1u` are legal, and tokenise differently.
    if tokens[:1] == ["#"]:
        tokens = tokens[1:]
    elif tokens[:1] == ["#define"]:
        tokens = ["define"] + tokens[1:]
    else:
        return None

    if len(tokens) != 3 or tokens[0] != "define" or tokens[1] != VERSION_MACRO:
        return None

    value = tokens[2]
    if value[-1:] in ("u", "U"):
        value = value[:-1]

    return int(value) if value.isdigit() else None


def strip_comments(text):
    """Remove C comments, leaving string and character literals alone.

    Hand-written rather than a regex because a regex cannot tell a comment from the same characters
    inside a string literal, and this header is full of string literals.
    """
    out = []
    i, n = 0, len(text)

    while i < n:
        c = text[i]

        if c in ('"', "'"):
            quote = c
            out.append(c)
            i += 1
            while i < n:
                if text[i] == "\\" and i + 1 < n:
                    out.append(text[i : i + 2])
                    i += 2
                    continue
                out.append(text[i])
                if text[i] == quote:
                    i += 1
                    break
                i += 1
            continue

        if c == "/" and i + 1 < n and text[i + 1] == "*":
            end = text.find("*/", i + 2)
            i = n if end == -1 else end + 2
            out.append(" ")
            continue

        if c == "/" and i + 1 < n and text[i + 1] == "/":
            end = text.find("\n", i)
            i = n if end == -1 else end
            out.append(" ")
            continue

        out.append(c)
        i += 1

    return "".join(out)


def read_header():
    """The header's declared version, and a hash of the declarations around it."""
    text = io.open(HEADER, encoding="utf-8", newline="").read()

    versions = []
    kept = []
    for line in text.splitlines():
        version = parse_version_define(line)
        if version is None:
            kept.append(line)
        else:
            versions.append(version)

    if len(versions) != 1:
        sys.stderr.write("%s is defined %d times in %s; expected exactly one.\n" % (VERSION_MACRO, len(versions), HEADER))
        raise SystemExit(1)

    # The version line goes so that bumping it is not itself a change; comments go so that rewording
    # one is not either; and whitespace is collapsed so that reflowing a declaration or reindenting
    # the file cannot move the hash. Line endings never reach the hash at all, so the answer does not
    # depend on which platform checked the tree out.
    body = " ".join(strip_comments("\n".join(kept)).split())
    return versions[0], hashlib.sha256(body.encode("utf-8")).hexdigest()


def read_fingerprint():
    if not os.path.isfile(FINGERPRINT):
        return None, None

    recorded_version, recorded_hash = None, None
    for line in io.open(FINGERPRINT, encoding="utf-8", newline=""):
        line = line.strip()
        if line.startswith("version="):
            recorded_version = int(line.split("=", 1)[1])
        elif line.startswith("sha256="):
            recorded_hash = line.split("=", 1)[1]
    return recorded_version, recorded_hash


def write_fingerprint(version, digest):
    io.open(FINGERPRINT, "w", encoding="utf-8", newline="\n").write(
        "# Fingerprint of tgl_program_abi.h with the TGL_ABI_VERSION line removed.\n"
        "# Regenerate with: python tools/check_abi_version.py update\n"
        "version=%d\n"
        "sha256=%s\n" % (version, digest)
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("mode", choices=("check", "update"))
    mode = parser.parse_args().mode

    version, digest = read_header()
    recorded_version, recorded_hash = read_fingerprint()

    if recorded_hash is None:
        if mode == "check":
            sys.stderr.write("No fingerprint recorded. Run: python tools/check_abi_version.py update\n")
            return 1
        write_fingerprint(version, digest)
        print("Recorded ABI version %d, fingerprint %s." % (version, digest[:16]))
        return 0

    unchanged = digest == recorded_hash

    if mode == "check":
        if unchanged and version == recorded_version:
            print("ABI header matches its recorded fingerprint at version %d." % version)
            return 0

        if not unchanged and version == recorded_version:
            sys.stderr.write(
                "The Program ABI header changed but TGL_ABI_VERSION is still %d.\n"
                "\n"
                "Any member added, removed or reordered, any signature or unit changed, moves the\n"
                "layout an already-built Program was compiled against. Leaving the number alone lets\n"
                "such a Program load and read the wrong bytes, which is what the number exists to\n"
                "prevent and is not something that fails loudly at run time.\n"
                "\n"
                "Bump TGL_ABI_VERSION, then: python tools/check_abi_version.py update\n" % version
            )
            return 1

        sys.stderr.write(
            "The recorded fingerprint is stale: it says version %d, the header says version %d.\n"
            "Run: python tools/check_abi_version.py update\n" % (recorded_version, version)
        )
        return 1

    if not unchanged and version == recorded_version:
        sys.stderr.write(
            "Refusing to record a changed header at the same version %d. Bump TGL_ABI_VERSION first.\n" % version
        )
        return 1

    write_fingerprint(version, digest)
    print("Recorded ABI version %d, fingerprint %s." % (version, digest[:16]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
