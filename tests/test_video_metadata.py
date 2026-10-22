#!/usr/bin/env python3
"""Test anchored MP4 metadata, malformed atoms, symlinks and cancellation."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

root=Path(__file__).resolve().parents[1]
def atom(name,payload):
    return struct.pack(">I4s",len(payload)+8,name.encode("ascii"))+payload
def mvhd():
    # Full box v0: version/flags, creation/modification, timescale, duration.
    payload=bytearray(100)
    struct.pack_into(">II",payload,12,1000,95200)
    return atom("mvhd",payload)
def tkhd():
    payload=bytearray(84)
    struct.pack_into(">II",payload,76,1920<<16,1080<<16)
    return atom("tkhd",payload)

with tempfile.TemporaryDirectory() as scratch:
    folder=Path(scratch)
    gallery=folder/"user/av_contents/video/NPXS00000/GAME123"
    gallery.mkdir(parents=True)
    relative="/user/av_contents/video/NPXS00000/GAME123/sample.mp4"
    fixture=gallery/"sample.mp4"
    binary=folder/"video_metadata_test"
    subprocess.run(
        ["cc","-std=c11","-O1","-Wall","-Wextra","-Wpedantic",
         "-Isrc/native","tests/test_video_metadata.c",
         "src/native/video_metadata.c","-o",str(binary)],
        cwd=root,check=True,timeout=40)
    ftyp=atom("ftyp",b"isom"+struct.pack(">I",0)+b"isomiso2")
    moov=atom("moov",mvhd()+atom("trak",tkhd()))
    # Simulate a large mdat before moov; parser must skip video bytes.
    with fixture.open("wb") as file:
        file.write(ftyp)
        file.write(struct.pack(">I4s",1_048_584,b"mdat"))
        file.seek(1_048_576,1)
        file.write(moov)
    def run(source,virtual,mode):
        subprocess.run([str(binary),str(folder),str(source),virtual,mode],
                       cwd=root,check=True,timeout=15)
    run(fixture,relative,"valid")
    # Paths are never user-controlled APIs; test fail-closed anyway.
    run(fixture,relative.replace("/video/","/photo/"),"invalid")
    run(fixture,relative.replace("/NPXS00000/","//NPXS00000/"),"invalid")
    run(fixture,"/user/av_contents/video/../video/NPXS00000/GAME123/sample.mp4","invalid")
    # A gallery link must not permit traversal to an unrelated file.
    link=gallery/"linked.mp4"
    link.symlink_to(fixture)
    run(link,"/user/av_contents/video/NPXS00000/GAME123/linked.mp4","invalid")
    malformed=gallery/"broken.mp4"
    malformed.write_bytes(ftyp+struct.pack(">I4s",0xffffffff,b"moov")+b"junk")
    run(malformed,"/user/av_contents/video/NPXS00000/GAME123/broken.mp4","invalid")
    # Untrusted MP4: reject truncated, enormous, looping and excessive atoms.
    bad_payloads={
        "truncated_ftyp":b"ftyp",
        "oversized_moov":ftyp+struct.pack(">I4s",0xffffffff,b"moov")+b"x"*12,
        "extended_overflow":ftyp+struct.pack(">I4sQ",1,b"moov",2**63-1),
        "tiny_atom":ftyp+struct.pack(">I4s",4,b"moov"),
        "unclosed_trak":ftyp+atom("moov",struct.pack(">I4s",2000,b"trak")+b"short"),
        "missing_mvhd":ftyp+atom("moov",atom("trak",tkhd())),
        "no_timescale":ftyp+atom("moov",atom("mvhd",bytes(100))+atom("trak",tkhd())),
        "bad_dimensions":ftyp+atom("moov",mvhd()+atom("trak",atom("tkhd",bytes(84)))),
        "too_many_boxes":ftyp+atom("moov",atom("free",b"")*500+mvhd()+atom("trak",tkhd())),
    }
    for name,payload in bad_payloads.items():
        broken=gallery/f"{name}.mp4"
        broken.write_bytes(payload)
        run(broken,f"/user/av_contents/video/NPXS00000/GAME123/{name}.mp4","invalid")
    webm=gallery/"sample.webm"
    webm.write_bytes(b"\x1aE\xdf\xa3"+b"\0"*64)
    run(webm,"/user/av_contents/video/NPXS00000/GAME123/sample.webm","invalid")
print("MP4 bounded metadata and corrupt-box fixture: PASS")
