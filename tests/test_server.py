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
with tempfile.TemporaryDirectory() as temp:
 root=Path(temp)
 image=root/"user/av_contents/photo/NPXS40087/GAME"
 clip=root/"user/av_contents/video/NPXS40087/GAME"
 eta=root/"data/etaHEN"
 other=root/"data/prosperoeden"
 for d in (image,clip,eta,other):d.mkdir(parents=True)
 (image/"image.png").write_bytes(b"A"*35)
 (image/"sidecar.dat").write_text("metadata")
 (clip/"clip.MP4").write_bytes(b"B"*150)
 (eta/"debug.log").write_text("LOG")
 (eta/"config.ini").write_text("KEEP")
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
  assert set(entries)=={"image.png","clip.MP4","debug.log","config.ini"},entries
  assert entries["clip.MP4"]["bytes"]==150
  assert entries["config.ini"]["risk"]=="protected"
  for route in ("/","/app.js","/styles.css"):
   with urlopen(base+route,timeout=3) as r:assert r.status==200 and len(r.read())>100
  try:urlopen(Request(base+"/api/report",method="POST"),timeout=3);raise AssertionError("write method allowed")
  except HTTPError as e:assert e.code==405
  for route in ("/api/delete","/data/etaHEN/config.ini"):
   try:urlopen(base+route,timeout=3);raise AssertionError("private file served")
   except HTTPError as e:assert e.code==404
  assert (other/"save.bin").read_text()=="DO NOT TOUCH"
  assert (eta/"config.ini").read_text()=="KEEP"
  assert all("directory_link" not in r["path"] for r in report["items"])
 finally:
  p.terminate()
  try:p.wait(timeout=3)
  except subprocess.TimeoutExpired:p.kill();p.wait()
print("Native read-only HTTP integration: PASS")
