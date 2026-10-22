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
         "src/native/video_metadata.c","src/native/webm_metadata.c",
         "-o",str(binary)],
        cwd=root,check=True,timeout=40)
    ftyp=atom("ftyp",b"isom"+struct.pack(">I",0)+b"isomiso2")
    moov=atom("moov",mvhd()+atom("trak",tkhd()))
    # Simulate a large mdat before moov; parser must skip video bytes.
    with fixture.open("wb") as file:
        file.write(ftyp)
        file.write(struct.pack(">I4s",1_048_584,b"mdat"))
        file.seek(1_048_576,1)
        file.write(moov)
    def run(source,virtual,mode,expected_ms=None):
        args=[str(binary),str(folder),str(source),virtual,mode]
        if expected_ms is not None:
            args.append(str(expected_ms))
        subprocess.run(args,cwd=root,check=True,timeout=15)
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
    # WebM EBML Info/Tracks at end of a large Cluster, no video decode.
    def vint(value):
        for width in range(1,9):
            if value<(1<<(width*7))-1:
                return ((1<<(width*7))|value).to_bytes(width,"big")
        raise ValueError("oversized EBML test field")
    def ebml(code,payload):
        return bytes.fromhex(code)+vint(len(payload))+payload
    header=ebml("1A45DFA3",ebml("4282",b"webm"))
    info=ebml("1549A966",
        ebml("2AD7B1",(1000000).to_bytes(3,"big"))+
        ebml("4489",struct.pack(">d",95200.0)))
    track=ebml("AE",ebml("E0",
        ebml("B0",(1920).to_bytes(2,"big"))+
        ebml("BA",(1080).to_bytes(2,"big"))))
    tracks=ebml("1654AE6B",track)
    cluster=ebml("1F43B675",b"\0"*1024*1024)
    webm=gallery/"sample.webm"
    webm.write_bytes(header+ebml("18538067",cluster+info+tracks))
    run(webm,"/user/av_contents/video/NPXS00000/GAME123/sample.webm","valid")
    longer=gallery/"long_video.webm"
    long_info=ebml("1549A966",
        ebml("2AD7B1",(1000000).to_bytes(3,"big"))+
        ebml("4489",struct.pack(">d",36000000.0)))
    longer.write_bytes(header+ebml("18538067",long_info+tracks))
    run(longer,"/user/av_contents/video/NPXS00000/GAME123/long_video.webm",
        "valid",36000000)
    # WebM limits: unsupported binary, missing tracks, corrupt VINT, 
    # oversized element and abusive nesting / entry counts.
    malformed_webm={
        "no_track":header+ebml("18538067",info),
        "wrong_doctype":ebml("1A45DFA3",ebml("4282",b"matroska"))+
          ebml("18538067",info+tracks),
        "unknown_atom":header+bytes.fromhex("18538067")+b"\xff"*8,
        "endless_items":header+ebml("18538067",
             ebml("EC",b"")*500+info+tracks),
        "bad_size":header+bytes.fromhex("18538067")+b"\x81\0",
        "no_duration":header+ebml("18538067",
             ebml("1549A966",ebml("2AD7B1",(1000000).to_bytes(3,"big")))+tracks)
    }
    for name,payload in malformed_webm.items():
        file=gallery/f"{name}.webm"
        file.write_bytes(payload)
        run(file,f"/user/av_contents/video/NPXS00000/GAME123/{name}.webm","invalid")
print("MP4/WebM bounded metadata and corrupt-container fixture: PASS")
