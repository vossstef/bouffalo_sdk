#!/usr/bin/env python3
"""Seed a Kconfig .config file from the effective CMake defconfig.

The build system exports every Make CONFIG_* variable to
``build/generated/defconfig.cmake``.  This script converts that file back to
the Kconfig ``.config`` format so that menuconfig starts from the values the
build would actually use.

Only symbols that are defined in the Kconfig tree are written.  Symbols that
exist in defconfig.cmake but are not defined in Kconfig stay in
defconfig.cmake and are never touched by menuconfig.
"""

import argparse
import os
import re
import sys


SET_RE = re.compile(r"^\s*set\(\s*(CONFIG_[A-Za-z0-9_]+)\s+(.*)\)\s*$")


def parse_defconfig_cmake(path):
    values = {}
    with open(path, "r", encoding="utf-8") as handle:
        for raw_line in handle:
            match = SET_RE.match(raw_line)
            if not match:
                continue
            name, value = match.groups()
            values[name] = value.strip()
    return values


def load_kconfig(kconfig_file):
    kconfig_dir = os.path.dirname(os.path.abspath(__file__))
    if kconfig_dir not in sys.path:
        sys.path.insert(0, kconfig_dir)
    from kconfiglib import Kconfig

    return Kconfig(kconfig_file, warn_to_stderr=False, suppress_traceback=True)


def defined_symbols(kconf):
    return {sym.name: (sym.type, sym) for sym in kconf.unique_defined_syms}


def config_suffix(name):
    """Strip the CONFIG_ prefix from a .config assignment name."""
    return name[7:] if name.startswith("CONFIG_") else name


def format_value(name, value, symbols, fallback_quote):
    from kconfiglib import BOOL, STRING, TRISTATE

    type_info = symbols.get(config_suffix(name))
    if type_info is not None:
        sym_type = type_info[0]
    else:
        sym_type = None

    if value in ("y", "n"):
        return "y" if value == "y" else None

    if sym_type in (BOOL, TRISTATE):
        return "y" if value in ("y", "1", "true") else None

    if sym_type == STRING or fallback_quote:
        value = value.strip("\"'")
        return '"{0}"'.format(value)

    # INT/HEX or an unknown type that looks numeric.
    if value == "":
        return None
    return value


def write_dotconfig(output, values, symbols):
    lines = ["#", "# Automatically generated from the effective build configuration.",
             "# Edit with menuconfig instead of by hand.", "#"]
    fallback_names = {name for name in values if config_suffix(name) not in symbols}
    for name in sorted(values):
        value = values[name]
        if not value:
            continue
        formatted = format_value(name, value, symbols, name in fallback_names)
        if formatted is None:
            lines.append("# {0} is not set".format(name))
        else:
            lines.append("{0}={1}".format(name, formatted))

    with open(output, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")


def write_kconfig_config(output, kconf, symbols):
    """Write symbol values in .config form.

    This SDK names Kconfig symbols with an explicit CONFIG_ prefix
    (e.g. ``config CONFIG_DEBUG``), so Kconfiglib's write_config() would emit
    double-prefixed entries.  Generate the file ourselves instead, using the
    same visibility rules as Kconfiglib (config_string non-empty).
    """
    lines = ["#", "# Automatically generated from the effective build configuration.",
             "# Edit with menuconfig instead of by hand.", "#"]
    for sym in sorted(kconf.unique_defined_syms, key=lambda s: s.name):
        if not sym.config_string:
            continue
        config_key = "CONFIG_" + sym.name
        formatted = format_value(config_key, sym.str_value, symbols, False)
        if formatted is None:
            lines.append("# {0} is not set".format(config_key))
        else:
            lines.append("{0}={1}".format(config_key, formatted))

    with open(output, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config-cmake", required=True,
                        help="Path to build/generated/defconfig.cmake")
    parser.add_argument("--output", default=".config",
                        help="Output .config path (default: .config)")
    parser.add_argument("--kconfig", default="Kconfig",
                        help="Kconfig entry file (default: Kconfig)")
    parser.add_argument("--force", action="store_true",
                        help="Overwrite an existing .config")
    args = parser.parse_args()

    if not os.path.exists(args.config_cmake):
        print("Error: {0} does not exist, run 'make cmake_config' first"
              .format(args.config_cmake), file=sys.stderr)
        return 1

    values = parse_defconfig_cmake(args.config_cmake)
    kconf = load_kconfig(args.kconfig)
    symbols = defined_symbols(kconf)

    # Drop assignments to symbols that are not defined in Kconfig.  Keeping
    # them would only make Kconfiglib print "undefined symbol" warnings, and
    # menuconfig cannot edit them anyway.
    filtered = {name: value for name, value in values.items()
                if config_suffix(name) in symbols}

    import tempfile

    with tempfile.NamedTemporaryFile(
            "w", suffix=".config", delete=False, encoding="utf-8") as handle:
        fragment = handle.name
        write_dotconfig(fragment, filtered, symbols)

    # Always start from the effective defconfig, then let an existing .config
    # override it.  This way newly defined Kconfig symbols and defconfig
    # changes are picked up on every menuconfig run while user edits survive.
    kconf.load_config(fragment, replace=True)
    if os.path.exists(args.output) and not args.force:
        # The existing .config deliberately repeats defconfig values; do not
        # warn about assignments that override or match the base fragment.
        kconf.warn_assign_override = False
        kconf.warn_assign_redun = False
        kconf.load_config(args.output, replace=False)
        mode = "merged"
    else:
        mode = "seeded"
    write_kconfig_config(args.output, kconf, symbols)

    os.unlink(fragment)
    print("{0} {1} ({2} symbols defined in Kconfig)"
          .format("Merged" if mode == "merged" else "Wrote",
                  args.output, len(filtered)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
