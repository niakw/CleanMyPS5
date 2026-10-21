"use strict";
const assert=require("node:assert/strict");
const {safeItems,compareMedia}=require("../web/app.js");
const item=(id,path,bytes,modifiedAt=0,risk="review")=>({
  id,path,bytes,modifiedAt,risk,category:"media",
  previewPath:path.replace(/^\/tmp\/fixture/,""),
});
const report={schemaVersion:1,items:[
 item("a","/tmp/fixture/user/av_contents/photo/NPXS40087/GAME/Zeta.png",128,400),
 item("b","/tmp/fixture/user/av_contents/photo/NPXS40087/GAME/alpha.png",4096,300),
 item("c","/tmp/fixture/user/av_contents/video/NPXS40087/GAME/b.mp4",512,0,"protected"),
]};
const list=safeItems(report);
assert.equal(list.length,3);
assert.equal(list[0].sortName,"zeta.png");
assert.equal(list[0].previewPath,"/user/av_contents/photo/NPXS40087/GAME/Zeta.png");
assert.equal(list[2].videoPath,"/user/av_contents/video/NPXS40087/GAME/b.mp4");
assert.equal(list[2].risk,"protected");
assert.deepEqual(list.slice().sort((a,b)=>compareMedia(a,b,"size")).map(x=>x.id),["b","c","a"]);
assert.deepEqual(list.slice().sort((a,b)=>compareMedia(a,b,"date")).map(x=>x.id),["a","b","c"]);
assert.deepEqual(list.slice().sort((a,b)=>compareMedia(a,b,"name")).map(x=>x.id),["b","c","a"]);
assert.throws(()=>safeItems({schemaVersion:2,items:[]}),/Format incompatible/);
assert.throws(()=>safeItems({schemaVersion:1,items:[item("x","/a.png",1),item("x","/b.png",2)]}),/Identifiant en double/);
assert.throws(()=>safeItems({schemaVersion:1,items:[item("bad","/a.png",-1)]}),/Entrée invalide/);
assert.throws(()=>safeItems({schemaVersion:1,items:[item("bad","/a.png",Number.MAX_SAFE_INTEGER+1)]}),/Entrée invalide/);
const unknown=safeItems({schemaVersion:1,items:[{id:"u",path:"/data/unknown",bytes:1,category:"weird",risk:"weird"}]})[0];
assert.equal(unknown.category,"other");
assert.equal(unknown.risk,"protected");
assert.equal(unknown.previewPath,"");
assert.equal(unknown.videoPath,"");
const badPreview=safeItems({schemaVersion:1,items:[{
 id:"t",path:"/data/secret.png",bytes:1,category:"media",
 previewPath:"/data/etaHEN/config.png",
} ]})[0];
assert.equal(badPreview.previewPath,"");
console.log("UI report validation and media sorting: PASS");
