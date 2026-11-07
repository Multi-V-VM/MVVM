#!/usr/bin/env python3
"""Delete one real variable DIE from the single-CU DWARF4 test fixture.

Keep code, allocated data (including the independent inventory), locations and
all remaining DIEs intact. Repair reference offsets; a renamed variable or a
stripped debug section is not a substitute for this test.
"""
import argparse
from collections import Counter
import io
import json
from pathlib import Path
import struct
import subprocess
import tempfile
from elftools.elf.elffile import ELFFile


def delete_entry(source: Path, destination: Path, function: str, slot: str | None):
    elf = ELFFile(io.BytesIO(source.read_bytes()))
    assert elf.little_endian, "fixture requires little endian ELF"
    dwarf = elf.get_dwarf_info()
    units = list(dwarf.iter_CUs())
    assert len(units) == 1, "fixture requires exactly one compilation unit"
    cu = units[0]
    assert cu.cu_offset == 0 and cu.header.version == 4 and cu.structs.dwarf_format == 32
    dies = list(cu.iter_DIEs())
    parents = [d for d in dies if d.tag == "DW_TAG_subprogram"
               and d.attributes.get("DW_AT_name") and d.attributes["DW_AT_name"].value.decode() == function]
    assert len(parents) == 1, function
    candidates = [d for d in parents[0].iter_children() if d.tag == "DW_TAG_variable"
                  and d.attributes["DW_AT_name"].value.decode().startswith("mvvm_slot_")
                  and (slot is None or d.attributes["DW_AT_name"].value.decode() == slot)]
    assert candidates, "required variable not found"
    victim = candidates[0]
    assert not victim.has_children
    start, length = victim.offset, victim.size
    end = start + length
    data = bytearray(elf.get_section_by_name(".debug_info").data())
    assert len(data) == cu.header.unit_length + 4
    refs = {"DW_FORM_ref1": 1, "DW_FORM_ref2": 2, "DW_FORM_ref4": 4, "DW_FORM_ref8": 8,
            "DW_FORM_ref_addr": 4}
    for die in dies:
        if die is victim:
            continue
        for attr in die.attributes.values():
            if not attr.form.startswith("DW_FORM_ref"):
                continue
            assert attr.form in refs, f"unsupported test reference {attr.form}"
            target = attr.raw_value
            assert not start <= target < end, "another DIE references removed variable"
            if target >= end:
                data[attr.offset:attr.offset + refs[attr.form]] = (target - length).to_bytes(refs[attr.form], "little")
    data[0:4] = (cu.header.unit_length - length).to_bytes(4, "little")
    del data[start:end]
    updates = {".debug_info": data}
    # GNU/public indexes contain DIE offsets too. Preserve their validity.
    for name in (".debug_pubnames", ".debug_pubtypes", ".debug_gnu_pubnames", ".debug_gnu_pubtypes"):
        section = elf.get_section_by_name(name)
        if section is None:
            continue
        index = bytearray(section.data())
        at = 0
        while at < len(index):
            size, version, cu_offset, cu_length = struct.unpack_from("<IHII", index, at)
            assert version == 2 and cu_offset == 0 and cu_length == len(data) + length
            struct.pack_into("<I", index, at + 10, len(data))
            entry = at + 14
            while entry < at + 4 + size:
                offset = struct.unpack_from("<I", index, entry)[0]
                if not offset:
                    break
                assert not start <= offset < end
                if offset >= end:
                    struct.pack_into("<I", index, entry, offset - length)
                entry += 4 + int("gnu" in name)
                entry = index.index(0, entry) + 1
            at += size + 4
        updates[name] = index
    with tempfile.TemporaryDirectory() as tmp:
        command = ["objcopy"]
        for name, contents in updates.items():
            path = Path(tmp) / name
            path.write_bytes(contents)
            command += ["--update-section", f"{name}={path}"]
        subprocess.run(command + [str(source), str(destination)], check=True)
    changed = ELFFile(io.BytesIO(destination.read_bytes()))
    for section in elf.iter_sections():
        if section["sh_flags"] & 2:  # SHF_ALLOC
            other = changed.get_section_by_name(section.name)
            assert other is not None and section.data() == other.data(), section.name
    def inventory(items):
        return Counter((d.tag, d.attributes["DW_AT_name"].value) for d in items
                       if "DW_AT_name" in d.attributes)
    remaining = list(next(changed.get_dwarf_info().iter_CUs()).iter_DIEs())
    expected = inventory(dies)
    expected[(victim.tag, victim.attributes["DW_AT_name"].value)] -= 1
    assert inventory(remaining) == +expected and len(remaining) == len(dies) - 1
    for die in remaining:
        for name, attr in die.attributes.items():
            if attr.form in refs:
                assert die.get_DIE_from_attribute(name) is not None
    return {"function": function, "deleted_slot": victim.attributes["DW_AT_name"].value.decode(),
            "deleted_die_bytes": length, "dies_before": len(dies), "dies_after": len(remaining),
            "allocated_sections_unchanged": True}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--function", default="aot_func#0")
    parser.add_argument("--slot")
    args = parser.parse_args()
    print(json.dumps(delete_entry(args.source, args.destination, args.function, args.slot)))
