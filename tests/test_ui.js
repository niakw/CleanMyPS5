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

const {mediaOld,buildSmartSummary}=require("../web/app.js");
const snapshotNow=Date.UTC(2026,9,21);
const oldTime=Math.floor((snapshotNow-91*86400000)/1000);
const recentTime=Math.floor((snapshotNow-20*86400000)/1000);
assert.equal(mediaOld({category:"media",modifiedAt:oldTime},snapshotNow),true);
assert.equal(mediaOld({category:"media",modifiedAt:recentTime},snapshotNow),false);
assert.equal(mediaOld({category:"logs",modifiedAt:oldTime},snapshotNow),false);
const summary=buildSmartSummary({
 items:[{category:"media",bytes:75*1024*1024,modifiedAt:oldTime,risk:"review"},
        {category:"media",bytes:500,modifiedAt:recentTime,risk:"review"},
        {category:"logs",bytes:100,modifiedAt:oldTime,risk:"review"},
        {category:"residuals",bytes:100,modifiedAt:0,risk:"protected"}],
 liveReport:true,scanStats:{partial:false},
 duplicates:{groups:[{bytesEach:1000,paths:["a","b","c"]}],partial:false},
 preflight:{entries:[]},volumes:[],diagnostics:{entries:[{status:"enabled"}]},homebrew:{partial:false,items:[{path:"/data/homebrew/HB_DEMO"}]},
},snapshotNow);
assert.equal(summary.mediaFiles,2);
assert.equal(summary.largeFiles,1);
assert.equal(summary.oldFiles,1);
assert.equal(summary.verifiedCopyBytes,2000);
assert.equal(summary.suspiciousAutoloads,1);
assert.equal(summary.logFiles,1);
assert.equal(summary.protectedFiles,1);
assert.equal(summary.homebrewEntries,1);
assert.equal(summary.partial,false);
assert.equal(buildSmartSummary({items:[]},snapshotNow).partial,true);

const {captureLensGroups}=require("../web/app.js");
const lens=captureLensGroups([
 {category:"media",titleId:"GAME_A",bytes:100,path:"/photo/cap.png"},
 {category:"media",titleId:"GAME_A",bytes:300,path:"/video/cap.mp4"},
 {category:"media",titleId:"GAME_B",bytes:200,path:"/photo/p.png"},
 {category:"logs",titleId:"GAME_C",bytes:1000,path:"/log.txt"},
],1);
assert.equal(lens.totalBytes,600);
assert.equal(lens.allGames,2);
assert.equal(lens.groups.length,2);
assert.equal(lens.groups[0].titleId,"GAME_A");
assert.equal(lens.groups[0].photos,100);
assert.equal(lens.groups[0].videos,300);
assert.equal(lens.groups[1].titleId,"Autres jeux");
assert.equal(lens.groups[1].bytes,200);
const {mediaMatches}=require("../web/app.js");
const movie={path:"/user/av_contents/video/NPXS40087/GAME/Zelda Finale.mp4",titleId:"GAME"};
const still={path:"/user/av_contents/photo/NPXS40087/GAME/screen.png",titleId:"GAME"};
assert.equal(mediaMatches(movie,{kind:"videos",query:"zelda"}),true);
assert.equal(mediaMatches(movie,{kind:"images",query:""}),false);
assert.equal(mediaMatches(still,{kind:"images",query:"SCREEN"}),true);
assert.equal(mediaMatches(still,{kind:"all",query:"GAME"}),true);
assert.equal(mediaMatches(still,{kind:"all",query:"something-else"}),false);
console.log("UI report, Analyse intelligente, capture lens, search and sorting: PASS");


const {assessOfficial,RETURN_STEPS}=require("../web/app.js");
assert.equal(RETURN_STEPS.length,8);
assert.equal(RETURN_STEPS.some(x=>x.id==='shortcut'),true);
const flagged=assessOfficial({
 diagnostics:{entries:[{path:"/known",status:"etaHEN-reference"},{path:"/cfg",status:"enabled"}]},
 homebrew:{partial:false,items:[{path:"/data/homebrew/demo",kind:"homebrew"}]},
 preflight:{entries:[{path:"/data/etaHEN",status:"accessible"}]}
});
assert.equal(flagged.reviewCount,4);
assert.equal(flagged.canGuaranteeSafe,false);
assert.equal(flagged.findings.find(x=>x.id==="autoload").level,"review");
assert.equal(flagged.findings.find(x=>x.id==="network").level,"unknown");
assert.equal(flagged.findings.some(x=>x.level==="safe"),false);
const incomplete=assessOfficial({});
assert.equal(incomplete.partial,true);
assert.equal(incomplete.canGuaranteeSafe,false);
const quiet=assessOfficial({
 diagnostics:{entries:[]},homebrew:{partial:false,items:[]},preflight:{entries:[]}
});
assert.equal(quiet.reviewCount,0);
assert.equal(quiet.canGuaranteeSafe,false);
assert.equal(quiet.findings.find(x=>x.id==="psn").level,"unknown");
console.log("Return to Live assessment: PASS");

const extraPayload=assessOfficial({
 diagnostics:{entries:[{path:"/loader",status:"other-payload-reference"}]},
 homebrew:{partial:false,items:[]},preflight:{entries:[]}
});
assert.equal(extraPayload.findings.find(x=>x.id==="autoload").level,"review");

const {OFFICIAL_ROUTES}=require("../web/app.js");
assert.deepEqual(OFFICIAL_ROUTES.map(x=>x.id),["undecided","stop","update","reset","reinstall"]);
assert.equal(OFFICIAL_ROUTES.some(x=>/garanti/i.test(x.impact)),true);

const {officialPlan}=require("../web/app.js");
const privateFixture={
 diagnostics:{entries:[{path:"/secret/private-autoload.txt",status:"other-payload-reference"}]},
 homebrew:{partial:false,items:[{path:"/secret/private-homebrew"}]},
 preflight:{entries:[]},officialCheckedAt:"2026-10-21T12:00:00Z",
 officialManual:new Set(["backup"]),officialRoute:"reinstall"
};
const exported=officialPlan(privateFixture,"2026-10-21T12:00:00Z");
const safeJson=JSON.stringify(exported);
assert.equal(exported.guaranteesAccess,false);
assert.equal(exported.filesDeleted,0);
assert.equal(exported.manualSteps.filter(x=>x.checked).length,1);
assert.equal(exported.chosenRoute,"reinstall");
assert.equal(safeJson.includes("private-autoload"),false);
assert.equal(safeJson.includes("private-homebrew"),false);
assert.equal(officialPlan({...privateFixture,officialRoute:"unexpected"}).chosenRoute,"undecided");

const {safeCoverage,genericMatches}=require("../web/app.js");
assert.deepEqual(safeCoverage(null),[]);
assert.deepEqual(safeCoverage([{path:42}]),[]);
const coverage=safeCoverage([{path:"/data",status:"partial",items:2,nominalBytes:10}]);
assert.equal(coverage[0].status,"partial");
assert.equal(safeCoverage([{path:"/data",status:"invented",items:-1}])[0].status,"unavailable");
assert.equal(genericMatches({path:"/data/Unknown/cache/blob",bytes:60*1024*1024,modifiedAt:oldTime},{query:"UNKNOWN",mode:"over50"},snapshotNow),true);
assert.equal(genericMatches({path:"/data/Unknown/boot.elf",bytes:20,modifiedAt:oldTime},{mode:"large"},snapshotNow),false);
assert.equal(genericMatches({path:"/data/Unknown/logs/trace.txt",bytes:20,modifiedAt:oldTime},{mode:"old"},snapshotNow),true);
const newMedia=safeItems({schemaVersion:1,items:[item("new","/user/av_contents/photo/NPXS99999/OTHER/a.png",10)]});
assert.equal(newMedia[0].previewPath,"/user/av_contents/photo/NPXS99999/OTHER/a.png");
console.log("Generic metadata coverage, file filters and alternate capture namespace: PASS");

const {inventoryExport}=require("../web/app.js");
const metadataExported=inventoryExport({items:[{id:"protected",path:"/data/AnyApp/config.ini",bytes:20,modifiedAt:1700000000,category:"residuals",risk:"protected",reason:"Configuration"}],coverage:[{path:"/data",status:"partial",items:1}],ruleVersion:"test",warnings:[]});
assert.equal(metadataExported.items[0].risk,"protected");
assert.equal(metadataExported.items[0].modifiedAt,1700000000);
assert.equal(metadataExported.cleanupValidated,false);
assert.equal(metadataExported.coverage[0].status,"partial");
console.log("Complete metadata export preserves protected entries and dates: PASS");
