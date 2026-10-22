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
 (image/"same.png").write_bytes(bytes([137,80,78,71,13,10,26,10])+b"A"*27)
 # A hardlink is another directory entry for the same physical bytes:
 # it must NOT increase independently recoverable duplicate space.
 (image/"hardlink.png").hardlink_to(image/"image.png")
 (image/"same-size-but-different.jpg").write_bytes(b"Z"*35)
 (clip/"clip.MP4").write_bytes(b"\x00\x00\x00\x14ftypisom"+b"B"*138)
 (clip/"not-a-real-video.mp4").write_bytes(b"Z"*64)
 (clip/"another.webm").write_bytes(bytes.fromhex("1a45dfa3")+b"W"*128)
 with (clip/"long.mp4").open("wb") as f:
  f.write(b"\x00\x00\x00\x14ftypisom")
  f.truncate(3*1024*1024+33)
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
 # An autoload directory/leaf swapped for a symlink must be reported
 # unreadable, never followed to a private file elsewhere in the fixture.
 usb1=root/"mnt/usb1/ps5_autoloader"
 usb1.parent.mkdir(parents=True)
 usb1.symlink_to(usb,target_is_directory=True)
 usb2=root/"mnt/usb2/ps5_autoloader"
 usb2.mkdir(parents=True)
 (usb2/"autoload.txt").symlink_to(other/"save.bin")
 (other/"save.bin").write_text("DO NOT TOUCH")
 homebrew=root/"data/homebrew/HB_DEMO"
 homebrew.mkdir(parents=True)
 (homebrew/"assets.bin").write_bytes(b"H"*137)
 plug=eta/"plugins"
 plug.mkdir()
 (plug/"plugin-note.txt").write_bytes(b"P"*73)
 try:(homebrew/"linked-save").symlink_to(other,target_is_directory=True)
 except OSError:pass
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
  with urlopen(base+"/api/capabilities",timeout=2) as r:
   caps=json.load(r)
  assert caps["schemaVersion"]==1
  assert caps["service"]=="CleanMyPS5-helper"
  assert caps["protocol"]==2 and caps["transport"]=="loopback"
  assert caps["mode"]=="read-only"
  assert caps["permissionScope"]=="runtime-dependent"
  assert caps["features"]["overview"] and caps["features"]["preflight"]
  assert caps["features"]["delete"] is False
  assert caps["features"]["quarantine"] is False
  assert caps["features"]["privilegeEscalation"] is False
  assert "path" not in json.dumps(caps).lower()
  # A deliberately slow, incomplete HTTP request must not freeze the entire UI.
  stalled=socket.create_connection(("127.0.0.1",port),timeout=2)
  try:
   stalled.sendall((f"GET /api/status HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\n").encode())
   time.sleep(.08)
   started=time.monotonic()
   with urlopen(base+"/api/status",timeout=2) as r:second_status=json.load(r)
   assert second_status["app"]=="CleanMyPS5"
   assert time.monotonic()-started<2
  finally:stalled.close()
  # Four stalled partial headers exhaust the fixed worker pool. The fifth
  # connection must get a bounded 503 rather than creating unbounded threads.
  time.sleep(.15) # allow the deliberately stalled worker to release after close
  busy=[]
  try:
   for _ in range(4):
    sock=socket.create_connection(("127.0.0.1",port),timeout=2)
    sock.sendall((f"GET /api/status HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\n").encode())
    busy.append(sock)
   time.sleep(.15)
   try:
    urlopen(base+"/api/status",timeout=2)
    raise AssertionError("Expected saturated worker pool to reject fifth client")
   except HTTPError as err:assert err.code==503
  finally:
   for sock in busy:sock.close()
  # Reclaimed slots become usable immediately after the partial sockets close.
  for _ in range(40):
   try:
    with urlopen(base+"/api/status",timeout=2) as r:
     assert json.load(r)["app"]=="CleanMyPS5"
    break
   except HTTPError as err:
    if err.code!=503:raise
    time.sleep(.05)
  else:raise AssertionError("Workers failed to recover from saturation")
  assert status["version"]==(Path(__file__).resolve().parents[1]/"VERSION").read_text().strip()
  # Reject DNS-rebinding Host values and cross-site browser origins.
  for headers in ({"Host":"unexpected.example:1234"},
                  {"Origin":"https://malicious.invalid"},
                  {"Referer":"https://malicious.invalid/page"},
                  {"Sec-Fetch-Site":"cross-site"}):
   try:urlopen(Request(base+"/api/status",headers=headers),timeout=3);raise AssertionError("Cross-origin access accepted: "+repr(headers))
   except HTTPError as err:assert err.code==403,err.code
  with urlopen(base+"/api/report",timeout=3) as r:report=json.load(r)
  entries={Path(e["path"]).name:e for e in report["items"]}
  required={"sidecar.dat","same.png","same-size-but-different.jpg","clip.MP4","long.mp4","another.webm","not-a-real-video.mp4","debug.log","config.ini","autoload.txt","assets.bin","plugin-note.txt"}
  assert required<=set(entries),entries
  assert len(set(entries)&{"image.png","hardlink.png"})==1,entries
  capture=entries.get("image.png",entries.get("hardlink.png"))
  assert entries["clip.MP4"]["bytes"]==150
  assert entries["clip.MP4"]["titleId"]=="GAME"
  assert capture["previewPath"].startswith("/user/av_contents/photo/NPXS40087/GAME/")
  assert isinstance(capture["modifiedAt"],int)
  assert report["stats"]["items"]==len(report["items"])
  assert report["stats"]["visited"]>=6
  assert report["stats"]["partial"] is False
  assert entries["config.ini"]["risk"]=="protected"
  # Exact read-only capture previews: no traversal, etaHEN config, other roots or symlinks.
  preview="/api/media?file="+quote(capture["previewPath"],safe="")
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
  # A 3 MiB clip must never be served in a single response.
  longvideo="/api/video?file="+quote("/user/av_contents/video/NPXS40087/GAME/long.mp4",safe="")
  with urlopen(Request(base+longvideo,headers={"Range":"bytes=0-"}),timeout=5) as r:
   assert r.status==206
   assert int(r.headers["Content-Length"])==1024*1024
   assert r.headers["Content-Range"].startswith("bytes 0-1048575/")
   assert len(r.read())==1024*1024
  with urlopen(Request(base+longvideo,headers={"Range":"bytes=-33"}),timeout=5) as r:
   assert len(r.read())==33
  try:urlopen(base+longvideo,timeout=3);raise AssertionError("Full-video GET was accepted")
  except HTTPError as err:assert err.code==416
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
  # Which path becomes the group representative depends on directory order.
  # The independently stored copy is same.png; the image/hardlink inode appears once.
  assert len(match)==2 and "same.png" in match and bool(match&{"image.png","hardlink.png"}),match
  assert dup["hardlinksSkipped"]==1
  assert dup["groups"][0]["bytesEach"]==35
  assert dup["filesConsidered"]==3
  assert dup["partial"] is False
  assert all("directory_link" not in p and "/data/" not in p for group in dup["groups"] for p in group["paths"])
  found={x["path"]:x for x in diag["entries"]}
  assert found["/data/ps5_autoloader/autoload.txt"]["status"]=="etaHEN-reference"
  assert found["/data/pldmgr/autoload.txt"]["status"]=="other-payload-reference"
  assert found["/data/etaHEN/config.ini"]["status"]=="enabled"
  assert found["/mnt/usb0/ps5_autoloader/autoload.txt"]["status"]=="no-reference"
  assert found["/mnt/usb1/ps5_autoloader/autoload.txt"]["status"]=="unreadable"
  assert found["/mnt/usb2/ps5_autoloader/autoload.txt"]["status"]=="unreadable"
  assert "DO NOT TOUCH" not in json.dumps(diag)
  assert "PRIVATE_TOKEN" not in json.dumps(diag)
  with urlopen(base+"/api/homebrew",timeout=3) as r:hb=json.load(r)
  assert hb["schemaVersion"]==1 and hb["scope"]=="protected-homebrew-metadata"
  indexed={x["path"]:x for x in hb["items"]}
  assert indexed["/data/homebrew/HB_DEMO"]["nominalBytes"]==137
  assert indexed["/data/homebrew/HB_DEMO"]["files"]==1
  assert indexed["/data/homebrew/HB_DEMO"]["risk"]=="protected"
  assert indexed["/data/etaHEN/plugins/plugin-note.txt"]["nominalBytes"]==73
  assert not any("linked-save" in x["path"] for x in hb["items"])
  assert not hb["partial"] and "DO NOT TOUCH" not in json.dumps(hb)
  with urlopen(base+"/api/preflight",timeout=3) as r:preflight=json.load(r)
  assert preflight["schemaVersion"]==1
  assert preflight["scope"]=="known-paths-only"
  assert preflight["mode"]=="read-only"
  assert preflight["probeMethod"]=="anchored-directory-and-mount-probe"
  assert preflight["summary"]["accessible"]>=4
  assert all(isinstance(x["writableHint"],bool) and isinstance(x["readOnlyMount"],bool) for x in preflight["entries"])
  assert all(x["reason"] in ("permission-denied","not-found","symlink-blocked","not-a-directory","open-failed","read-only-mount","directory-opened") for x in preflight["entries"])
  checks={x["path"]:x["status"] for x in preflight["entries"]}
  assert checks["/user/av_contents/photo"]=="accessible"
  assert checks["/user/av_contents/video"]=="accessible"
  assert checks["/data/etaHEN"]=="accessible"
  assert len(preflight["entries"])==26
  assert checks["/mnt/usb0/ps5_autoloader"]=="accessible"
  assert checks["/mnt/usb7/ps5_autoloader"]=="absent"
  assert "PRIVATE_TOKEN" not in json.dumps(preflight)
  with urlopen(base+"/api/volumes",timeout=3) as r:volumes=json.load(r)
  assert volumes["scope"]=="mounts-not-total-ssd"
  bypath={v["path"]:v for v in volumes["volumes"]}
  assert set(bypath)=={"/user/av_contents","/data"}
  assert all(v["status"]=="ok" for v in bypath.values())
  assert all(v["totalBytes"]>=v["freeBytes"]>=v["availableBytes"]>=0 for v in bypath.values())
  assert bypath["/data"]["sharedWith"]=="/user/av_contents"
  assert all("directory_link" not in r["path"] for r in report["items"])
  assert all("/data/prosperoeden/" not in r["path"] for r in report["items"])
 finally:
  p.terminate()
  try:
   code=p.wait(timeout=3)
   assert code==0,("Shutdown must be graceful",code,p.stderr.read().decode(errors="replace"))
  except subprocess.TimeoutExpired:
   p.kill();p.wait();raise AssertionError("Server failed to stop after SIGTERM")
print("Native read-only HTTP integration: PASS")
