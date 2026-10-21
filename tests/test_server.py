#!/usr/bin/env python3
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from urllib.request import urlopen,Request
from urllib.error import HTTPError
import socket
from urllib.parse import quote
with tempfile.TemporaryDirectory() as temp:
 root=Path(temp)
 image=root/"user/av_contents/photo/NPXS40087/GAME"
 clip=root/"user/av_contents/video/NPXS40087/GAME"
 eta=root/"data/etaHEN"
 other=root/"data/prosperoeden"
 for d in (image,clip,eta,other):d.mkdir(parents=True)
 (image/"image.png").write_bytes(bytes([137,80,78,71,13,10,26,10])+b"A"*27)
 (image/"sidecar.dat").write_text("metadata")
 (image/"same.jpg").write_bytes(bytes([137,80,78,71,13,10,26,10])+b"A"*27)
 (image/"same-size-but-different.jpg").write_bytes(b"Z"*35)
  (clip/"clip.MP4").write_bytes(b"\x00\x00\x00\x14ftypisom"+b"B"*138)
 (clip/"not-a-real-video.mp4").write_bytes(b"Z"*64)
 (clip/"another.webm").write_bytes(bytes.fromhex("1a45dfa3")+b"W"*128)
 (eta/"debug.log").write_text("LOG")
 (eta/"config.ini").write_text("toolbox_auto_start = 1\nPRIVATE_TOKEN=not-for-web")
 loader=root/"data/ps5_autoloader"
 loader.mkdir(parents=True)
 (loader/"autoload.txt").write_text("# etaHEN.elf (comment)\nkstuff.elf\netaHEN.elf\n")
 manager=root/"data/pldmgr"
 manager.mkdir(parents=True)
 (manager/"autoload.txt").write_text("# etaHEN.elf\nother.elf\n")
 usb=root/"mnt/usb0/ps5_autoloader"
 usb.mkdir(parents=True)
 (usb/"autoload.txt").write_text("!2000\n# etaHEN.elf\n")
 (other/"save.bin").write_text("DO NOT TOUCH")
 try:(image/"link.png").symlink_to(other/"save.bin")
 except OSError:pass
 with socket.socket() as sk:
  sk.bind(("127.0.0.1",0));port=sk.getsockname()[1]
 # An alias to the media root is never followed, even if it appears inside an allowlisted subtree.
 try:(image/"directory_link").symlink_to(other,target_is_directory=True)
 except OSError:pass
 p=subprocess.Popen([sys.argv[1],"--root",str(root),"--port",str(port)],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 base="http://127.0.0.1:"+str(port)
 try:
  for _ in range(40):
   try:
    with urlopen(base+"/api/status",timeout=1) as r:status=json.load(r)
    break
   except OSError:time.sleep(.05)
  else:raise AssertionError("server failed to start")
  assert status["mode"]=="read-only"
  assert status["target"]=="host"
  with urlopen(base+"/api/report",timeout=3) as r:report=json.load(r)
  entries={Path(e["path"]).name:e for e in report["items"]}
  assert set(entries)=={"image.png","same.jpg","same-size-but-different.jpg","clip.MP4","another.webm","not-a-real-video.mp4","debug.log","config.ini"},entries
  assert entries["clip.MP4"]["bytes"]==150
  assert entries["clip.MP4"]["titleId"]=="GAME"
  assert entries["image.png"]["previewPath"]=="/user/av_contents/photo/NPXS40087/GAME/image.png"
  assert isinstance(entries["image.png"]["modifiedAt"],int)
  assert report["stats"]["items"]==8
  assert report["stats"]["visited"]>=6
  assert report["stats"]["partial"] is False
  assert entries["config.ini"]["risk"]=="protected"
  # Exact read-only capture previews: no traversal, etaHEN config, other roots or symlinks.
  preview="/api/media?file="+quote(entries["image.png"]["previewPath"],safe="")
  with urlopen(base+preview,timeout=3) as r:
   assert r.headers.get_content_type()=="image/png"
   assert r.read()==(bytes([137,80,78,71,13,10,26,10])+b"A"*27)
  forbidden=[
   "/data/etaHEN/config.ini",
   "/user/av_contents/video/NPXS40087/GAME/clip.MP4",
   "/user/av_contents/photo/NPXS40087/GAME/link.png",
   "/user/av_contents/photo/NPXS40087/GAME/directory_link/save.bin",
   "/user/av_contents/photo/NPXS40087/GAME/sidecar.dat",
   "/user/av_contents/photo/NPXS40087/GAME/../GAME/image.png",
   "/user/av_contents/photo/NPXS40087/GAME/same-size-but-different.jpg",
   "/user/av_contents/photo/NPXS40087/GAME/%2e%2e/image.png",
  ]
  for forbidden_path in forbidden:
   url=base+"/api/media?file="+quote(forbidden_path,safe="")
   try:urlopen(url,timeout=3);raise AssertionError("Preview unexpectedly exposed: "+forbidden_path)
   except HTTPError as ex:assert ex.code in (400,404,413),ex.code
  # Chunked video preview: only the allowlisted original file and exact byte ranges.
  video="/api/video?file="+quote("/user/av_contents/video/NPXS40087/GAME/clip.MP4",safe="")
  with urlopen(Request(base+video,method="HEAD"),timeout=3) as r:
   assert r.status==200 and int(r.headers["Content-Length"])==150
   assert r.headers.get_content_type()=="video/mp4"
   assert r.headers["Accept-Ranges"]=="bytes"
  with urlopen(Request(base+video,headers={"Range":"bytes=0-19"}),timeout=3) as r:
   assert r.status==206 and r.headers["Content-Range"]=="bytes 0-19/150"
   assert r.read()==b"\x00\x00\x00\x14ftypisom"+b"B"*8
  with urlopen(Request(base+video,headers={"Range":"bytes=-8"}),timeout=3) as r:
   assert r.status==206 and r.headers["Content-Range"]=="bytes 142-149/150"
   assert r.read()==b"B"*8
  webm="/api/video?file="+quote("/user/av_contents/video/NPXS40087/GAME/another.webm",safe="")
  with urlopen(Request(base+webm,headers={"Range":"bytes=0-3"}),timeout=3) as r:
   assert r.headers.get_content_type()=="video/webm" and r.read()==bytes.fromhex("1a45dfa3")
  for bad in ("bytes=999-","bytes=0-1,5-7","bytes=-0","bytes=99999999999999999999999-"):
   try:urlopen(Request(base+video,headers={"Range":bad}),timeout=3);raise AssertionError("Invalid Range: "+bad)
   except HTTPError as e:assert e.code==416
  for bad in ("/data/etaHEN/config.ini","/user/av_contents/photo/NPXS40087/GAME/image.png",
              "/user/av_contents/video/NPXS40087/GAME/not-a-real-video.mp4",
              "/user/av_contents/video/NPXS40087/GAME/../GAME/clip.MP4"):
   try:urlopen(Request(base+"/api/video?file="+quote(bad,safe=""),headers={"Range":"bytes=0-20"}),timeout=3);raise AssertionError("Unexpected video access")
   except HTTPError as e:assert e.code in (400,404,413)
  for route in ("/","/app.js","/styles.css"):
   with urlopen(base+route,timeout=3) as r:assert r.status==200 and len(r.read())>100
  try:urlopen(Request(base+"/api/report",method="POST"),timeout=3);raise AssertionError("write method allowed")
  except HTTPError as e:assert e.code==405
  for route in ("/api/delete","/data/etaHEN/config.ini"):
   try:urlopen(base+route,timeout=3);raise AssertionError("private file served")
   except HTTPError as e:assert e.code==404
  assert (other/"save.bin").read_text()=="DO NOT TOUCH"
  assert (eta/"config.ini").read_text().startswith("toolbox_auto_start = 1")
  with urlopen(base+"/api/diagnostics",timeout=3) as r:diag=json.load(r)
  assert diag["checked"]==11
  with urlopen(base+"/api/duplicates",timeout=5) as r:dup=json.load(r)
  assert dup["algorithm"]=="byte-compare"
  assert dup["mode"]=="read-only"
  assert len(dup["groups"])==1,dup
  match={Path(p).name for p in dup["groups"][0]["paths"]}
  assert match=={"image.png","same.jpg"},match
  assert dup["groups"][0]["bytesEach"]==35
  assert dup["filesConsidered"]==3
  assert dup["partial"] is False
  found={x["path"]:x for x in diag["entries"]}
  assert found["/data/ps5_autoloader/autoload.txt"]["status"]=="etaHEN-reference"
  assert found["/data/pldmgr/autoload.txt"]["status"]=="no-reference"
  assert found["/data/etaHEN/config.ini"]["status"]=="enabled"
  assert found["/mnt/usb0/ps5_autoloader/autoload.txt"]["status"]=="no-reference"
  assert "PRIVATE_TOKEN" not in json.dumps(diag)
  with urlopen(base+"/api/volumes",timeout=3) as r:volumes=json.load(r)
  assert volumes["scope"]=="mounts-not-total-ssd"
  bypath={v["path"]:v for v in volumes["volumes"]}
  assert set(bypath)=={"/user/av_contents","/data"}
  assert all(v["status"]=="ok" for v in bypath.values())
  assert all(v["totalBytes"]>=v["freeBytes"]>=v["availableBytes"]>=0 for v in bypath.values())
  assert bypath["/data"]["sharedWith"]=="/user/av_contents"
  assert all("directory_link" not in r["path"] for r in report["items"])
 finally:
  p.terminate()
  try:p.wait(timeout=3)
  except subprocess.TimeoutExpired:p.kill();p.wait()
print("Native read-only HTTP integration: PASS")
