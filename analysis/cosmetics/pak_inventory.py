#!/usr/bin/env python3
"""Read cosmetic package metadata; never install files or execute game content.

GameForPeace footer/index layouts are independently implemented using the
format documentation in FabianFG/CUE4Parse's FPakInfo, FPakEntry and
GameForPeacePakFileReader sources. No third-party implementation is bundled.
"""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import re
import struct
import zipfile
import zlib


class Reader:
    def __init__(self, data):
        self.data = data
        self.position = 0

    def take(self, length):
        if length < 0 or self.position + length > len(self.data):
            raise ValueError("Truncated package metadata")
        result = self.data[self.position:self.position + length]
        self.position += length
        return result

    def number(self, fmt):
        return struct.unpack(fmt, self.take(struct.calcsize(fmt)))[0]

    def string(self):
        length = self.number("<i")
        if length <= 0 or length > 8192:
            raise ValueError("Unsupported package path length")
        raw = self.take(length)
        if raw[-1] != 0:
            raise ValueError("Unterminated package path")
        return raw[:-1].decode("utf-8")


def entries(data):
    if len(data) < 45:
        raise ValueError("Truncated pak footer")
    footer = data[-45:]
    encrypted, magic, version = struct.unpack_from("<BII", footer)
    if encrypted != 0x6C or magic != 0xFF67FF70 or version != 10:
        raise ValueError("Unreviewed or encrypted pak format")
    index_size = struct.unpack_from("<Q", footer, 29)[0] ^ 0x8924B0E3298B7069
    index_offset = struct.unpack_from("<Q", footer, 37)[0] ^ 0xD74AF37FAA6B020D
    if not 0 < index_size <= 8 * 1024 * 1024 or index_offset + index_size + 8 >= len(data) - 45:
        raise ValueError("Invalid pak index boundaries")
    index = Reader(data[index_offset:index_offset + index_size])
    mount = index.string()
    count = index.number("<I")
    if not 0 < count <= 20000:
        raise ValueError("Invalid pak entry count")
    rows = []
    for _ in range(count):
        start = index.position
        digest = index.take(20).hex()
        offset = index.number("<Q")
        length = index.number("<Q")
        method = index.number("<I")
        compressed = index.number("<Q")
        index.take(21)
        blocks = []
        if method:
            block_count = index.number("<I")
            if block_count > 8192:
                raise ValueError("Too many compression blocks")
            for _ in range(block_count):
                block_start, block_end = index.number("<Q"), index.number("<Q")
                if block_end < block_start or offset + block_end > index_offset:
                    raise ValueError("Invalid compression block")
                blocks.append([block_start, block_end])
        block_size = index.number("<I")
        flags = index.number("<B")
        header_size = index.position - start
        if offset + header_size > index_offset or length > 256 * 1024 * 1024:
            raise ValueError("Invalid pak data entry")
        rows.append(dict(path=None, offset=offset, uncompressed_size=length,
                         compression_method=method, compressed_size=compressed,
                         blocks=blocks, block_size=block_size, flags=flags,
                         header_size=header_size, declared_sha1=digest))
    if index.position != len(index.data):
        raise ValueError("Unconsumed pak index data")
    directory_offset = index_offset + index_size
    directory_size = struct.unpack_from("<Q", data, directory_offset)[0]
    if directory_offset + 8 + directory_size != len(data) - 45:
        raise ValueError("Invalid pak directory boundaries")
    directory = Reader(data[directory_offset + 8:directory_offset + 8 + directory_size])
    directories = directory.number("<Q")
    if directories > 20000:
        raise ValueError("Too many pak directories")
    seen = set()
    for _ in range(directories):
        parent = directory.string()
        files = directory.number("<Q")
        if files > count:
            raise ValueError("Invalid directory file count")
        for _ in range(files):
            name = directory.string()
            entry_index = directory.number("<i")
            if not 0 <= entry_index < count or entry_index in seen:
                raise ValueError("Invalid or repeated directory entry")
            seen.add(entry_index)
            rows[entry_index]["path"] = mount + parent + name
    if len(seen) != count or directory.position != len(directory.data):
        raise ValueError("Incomplete pak directory")
    offsets = sorted({row["offset"] for row in rows} | {index_offset})
    end_offsets = dict(zip(offsets, offsets[1:]))
    for row in rows:
        row["stored_sha256"] = hashlib.sha256(data[row["offset"]:end_offsets[row["offset"]]]).hexdigest()
    return dict(version=version, mount=mount, entry_count=count, index_offset=index_offset,
                index_size=index_size, directory_size=directory_size,
                declared_index_sha1_matches=hashlib.sha1(index.data).digest() == footer[9:29],
                entries=rows)


def extract(data, entry):
    size = entry["uncompressed_size"]
    if entry["flags"] & 1:
        raise ValueError("Encrypted payload is not extracted")
    if size > 8 * 1024 * 1024:
        raise ValueError("Payload exceeds analysis size limit")
    offset = entry["offset"]
    if entry["compression_method"] == 0:
        result = data[offset + entry["header_size"]:offset + entry["header_size"] + size]
    elif entry["compression_method"] == 1:
        output = bytearray()
        for start, end in entry["blocks"]:
            decoder = zlib.decompressobj()
            part = decoder.decompress(data[offset + start:offset + end], size - len(output) + 1)
            if not decoder.eof or decoder.unconsumed_tail or len(output) + len(part) > size:
                raise ValueError("Invalid or oversized Zlib block")
            output.extend(part)
        result = bytes(output)
    else:
        raise ValueError("Compression method requires a separate decoder")
    if len(result) != size:
        raise ValueError("Extracted payload length differs from its entry")
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--inputs", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    manifest = json.loads(Path(__file__).with_name("inputs.json").read_text())
    for source in manifest["files"]:
        path = args.inputs / source["name"]
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            while chunk := stream.read(1024 * 1024):
                digest.update(chunk)
        if path.stat().st_size != source["size"] or digest.hexdigest() != source["sha256"]:
            raise ValueError("Source file does not match reviewed manifest")
    packages = []
    comparisons = []
    common = {}
    for source in manifest["files"]:
        if not source["name"].endswith(".zip"):
            continue
        with zipfile.ZipFile(args.inputs / source["name"]) as archive:
            for info in archive.infolist():
                if not info.filename.endswith(".pak"):
                    continue
                if info.file_size > 128 * 1024 * 1024 or info.flag_bits & 1:
                    raise ValueError("Unsupported source archive entry")
                data = archive.read(info)
                package = entries(data)
                package.update(archive=source["name"], name=Path(info.filename).name,
                               size=len(data), sha256=hashlib.sha256(data).hexdigest())
                package["compression_counts"] = dict(collections.Counter(e["compression_method"] for e in package["entries"]))
                previous = common.get(package["name"])
                if previous:
                    before = {e["path"]:e for e in previous["entries"]}
                    changed = [e["path"] for e in package["entries"]
                               if e["path"] in before and e["stored_sha256"] != before[e["path"]]["stored_sha256"]]
                    comparisons.append(dict(name=package["name"], earlier_archive=previous["archive"],
                                            later_archive=package["archive"], changed_payload_paths=changed))
                common[package["name"]] = package
                samples = []
                selected = [e for e in package["entries"] if re.search(r"/CSV/.*(?:Item|Avatar|Skin|Cloth|Equip|Weapon)",e["path"],re.I)]
                for entry in selected[:60]:
                    sample = dict(path=entry["path"])
                    try:
                        payload = extract(data, entry)
                        strings = re.findall(rb"[ -~]{6,160}", payload)
                        sample.update(size=len(payload), sha256=hashlib.sha256(payload).hexdigest(),
                                      declared_sha1_matches=hashlib.sha1(payload).hexdigest() == entry["declared_sha1"],
                                      magic=payload[:16].hex(),
                                      ascii_numeric_strings=sorted({s.decode() for s in strings if re.fullmatch(rb"[1-9][0-9]{4,9}",s)})[:300],
                                      text_sample=[s.decode() for s in strings[:120]])
                    except (ValueError,zlib.error) as error:
                        sample["extraction_error"] = str(error)
                    samples.append(sample)
                package["selected_resource_samples"] = samples
                packages.append(package)
                print(json.dumps(dict(archive=source["name"], pak=package["name"], entries=package["entry_count"],
                                      compression_counts=package["compression_counts"], selected_samples=len(samples))))
    result = dict(schema_version=1,status="resource-inventory-only",runtime_item_ids_validated=False,
                  source_manifest=manifest,packages=packages,comparisons=comparisons)
    args.output.mkdir(parents=True,exist_ok=True)
    (args.output / "pak-inventory.json").write_text(json.dumps(result,ensure_ascii=False,indent=2)+"\n")


if __name__ == "__main__":
    main()
