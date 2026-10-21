"use strict";
// The UI never sends a delete request. Reports are untrusted data, rendered via textContent.
const labels={overview:"Vue d'ensemble",media:"Captures & vidéos",cache:"Caches & journaux",residuals:"Apps & fichiers",storage:"Stockage",official:"Return to Live",tools:"Outils"};
const categoryLabels={media:"Captures & vidéos",cache:"Caches",logs:"Journaux",residuals:"Applications",other:"Autres"};
const categoryColors={media:"#67baff",cache:"#b28dff",logs:"#68d9be",residuals:"#f1b86f",other:"#8192b8"};
const state={tab:"overview",items:[],selected:new Set(),source:"none",scannedAt:null,warnings:[],demo:false,backend:false,scanning:false,target:"host",page:0,pageSize:36,mediaMode:"all",mediaTitle:"all",mediaSort:"size",mediaKind:"all",mediaQuery:"",scanStats:null,coverage:[],ruleVersion:"",diagnostics:null,diagnosing:false,duplicates:null,checkingDuplicates:false,liveReport:false,preview:null,previousFocus:null,volumes:null,volumeBusy:false,reviewOpen:false,preflight:null,preflightBusy:false,reportRevision:0,smartBusy:false,smartStage:'',smartSummary:null,homebrew:null,homebrewBusy:false,officialBusy:false,officialStage:"",officialCheckedAt:null,officialManual:new Set(),officialRoute:"undecided"};
const $=s=>document.querySelector(s);
function node(tag,cls,text){const el=document.createElement(tag);if(cls)el.className=cls;if(text!==undefined)el.textContent=text;return el;}
function add(parent,...children){children.forEach(x=>parent.append(x));return parent;}
function bytes(value){if(!Number.isFinite(value)||value<0)return "0 o";const u=["o","Ko","Mo","Go","To"];let i=0;while(value>=1024&&i<u.length-1){value/=1024;i++;}return (i?value.toLocaleString("fr-FR",{maximumFractionDigits:1}):Math.round(value).toLocaleString("fr-FR"))+" "+u[i];}
function setTitle(title,lead){const e=add(node("div",""),node("div","eyebrow","ANALYSE DU STOCKAGE"),node("h1","page-heading",title),node("p","lead",lead));return e;}
function button(text,cls,fn){const b=node("button",cls+" focusable",text);b.type="button";b.addEventListener("click",fn);return b;}
function mediaOld(item,now=Date.now()){
 return item.category==="media"&&item.modifiedAt>0&&now-item.modifiedAt*1000>=90*86400000;
}
function buildSmartSummary(snapshot,now=Date.now()){
 const items=Array.isArray(snapshot.items)?snapshot.items:[];
 const media=items.filter(x=>x.category==="media");
 const groups=snapshot.duplicates?.groups||[];
 const verifiedCopyBytes=groups.reduce((sum,g)=>sum+Math.max(0,g.paths.length-1)*g.bytesEach,0);
 const autoload=snapshot.diagnostics?.entries||[];
 const checks={
  inventory:!!snapshot.liveReport,
  duplicates:snapshot.duplicates?true:null,
  autoload:snapshot.diagnostics?true:null,
  preflight:!!snapshot.preflight,
  volumes:!!snapshot.volumes,
  homebrew:!!snapshot.homebrew
 };
 const partial=snapshot.scanStats?.partial===true||snapshot.duplicates?.partial===true||snapshot.homebrew?.partial===true||
   Object.values(checks).some(x=>x===false);
 return {
  files:items.length,mediaFiles:media.length,
  mediaBytes:media.reduce((sum,x)=>sum+x.bytes,0),
  largeFiles:media.filter(x=>x.bytes>=50*1024*1024).length,
  oldFiles:media.filter(x=>mediaOld(x,now)).length,
  logFiles:items.filter(x=>x.category==="logs").length,
  homebrewEntries:snapshot.homebrew?.items?.length||0,
  protectedFiles:items.filter(x=>x.risk==="protected").length,
  exactGroups:groups.length,verifiedCopyBytes,
  suspiciousAutoloads:autoload.filter(x=>x.status==="etaHEN-reference"||x.status==="enabled").length,
  checks,partial,
 };
}
function numberFor(cat){return state.items.filter(x=>x.category===cat).reduce((a,x)=>a+x.bytes,0);}
function total(){return state.items.reduce((a,x)=>a+x.bytes,0);}
function selectedBytes(){return state.items.filter(x=>state.selected.has(x.id)).reduce((a,x)=>a+x.bytes,0);}
function announce(t){const el=$("#toast");el.textContent=t;el.classList.add("show");clearTimeout(announce.timer);announce.timer=setTimeout(()=>el.classList.remove("show"),3400);}
function safeItems(report){
  if(!report||report.schemaVersion!==1||!Array.isArray(report.items)||report.items.length>50000)throw Error("Format incompatible (schemaVersion 1 attendu, max. 50 000 éléments).");
  const ids=new Set();
  return report.items.map((v,i)=>{
    if(!v||typeof v!=="object"||typeof v.path!=="string"||v.path.length>2048||typeof v.bytes!=="number"||!Number.isSafeInteger(v.bytes)||v.bytes<0)throw Error("Entrée invalide à la ligne "+(i+1));
    const id=String(v.id??i);if(ids.has(id))throw Error("Identifiant en double");ids.add(id);
    return {id,path:v.path,bytes:v.bytes,category:Object.prototype.hasOwnProperty.call(categoryLabels,v.category)?v.category:"other",
      risk:["review","safe","protected"].includes(v.risk)?v.risk:"protected",reason:String(v.reason||"Analyse locale, aucune suppression automatique.").slice(0,400),
      titleId:typeof v.titleId==="string"&&/^[A-Za-z0-9_]{1,24}$/.test(v.titleId)?v.titleId:"",
      modifiedAt:Number.isSafeInteger(v.modifiedAt)&&v.modifiedAt>0&&v.modifiedAt<4102444800?v.modifiedAt:0,
      possibleTwin:false,previewPath:typeof v.previewPath==="string"&&
        /^\/user\/av_contents\/photo\/.+\.(png|jpe?g)$/i.test(v.previewPath)&&v.previewPath.length<2048?v.previewPath:"",
      videoPath:typeof v.previewPath==="string"&&
        /^\/user\/av_contents\/video\/.+\.(mp4|webm)$/i.test(v.previewPath)&&v.previewPath.length<2048?v.previewPath:"",
      sortName:v.path.split("/").pop().toLocaleLowerCase("fr")};
  });
}

function inventoryExport(snapshot){
 return {schemaVersion:1,kind:"metadata-inventory",mode:"metadata-only",cleanupValidated:false,
  source:"CleanMyPS5",ruleVersion:snapshot.ruleVersion||"",coverage:safeCoverage(snapshot.coverage),
  stats:snapshot.scanStats||null,warnings:(snapshot.warnings||[]).slice(0,32),
  items:snapshot.items.map(x=>({id:x.id,path:x.path,bytes:x.bytes,modifiedAt:x.modifiedAt,category:x.category,risk:x.risk,reason:x.reason,titleId:x.titleId||""}))};
}
function exportInventory(){
 if(!state.items.length)return;
 const url=URL.createObjectURL(new Blob([JSON.stringify(inventoryExport(state),null,2)],{type:"application/json"}));
 const a=document.createElement("a");a.href=url;a.download="cleanmyps5-inventaire.json";document.body.append(a);a.click();a.remove();
 setTimeout(()=>URL.revokeObjectURL(url),1500);announce("Inventaire des métadonnées exporté localement.");
}

function safeCoverage(value){
 if(!Array.isArray(value))return [];
 const statuses=["complete","partial","absent","unavailable","alias-skipped","not-scanned"];
 return value.slice(0,64).filter(x=>x&&typeof x.path==="string"&&x.path.startsWith("/")&&x.path.length<=2048).map(x=>({
  path:x.path,status:statuses.includes(x.status)?x.status:"unavailable",
  items:Number.isSafeInteger(x.items)&&x.items>=0?x.items:0,
  nominalBytes:Number.isSafeInteger(x.nominalBytes)&&x.nominalBytes>=0?x.nominalBytes:0
 }));
}
function coveragePanel(){
 const panel=node("section","data-panel");
 add(panel,panelHeader("Périmètre et règles",state.ruleVersion||"Rapport importé"));
 const statuses={complete:"Parcours terminé",partial:"Partiel",absent:"Absent",unavailable:"Inaccessible / lien refusé","alias-skipped":"Alias ignoré","not-scanned":"Non parcouru"};
 const list=node("div","preflight-list");
 for(const entry of state.coverage){
  const line=node("div","preflight-line");add(line,node("strong","",entry.path),node("span","",statuses[entry.status]+" · "+entry.items+" fichiers · "+bytes(entry.nominalBytes)));list.append(line);
 }
 add(panel,list,node("p","filter-warning","Métadonnées uniquement. Sauvegardes, comptes, licences, profils, runtimes et autres répertoires personnels connus sont exclus. Liens symboliques et fichiers spéciaux ignorés ; les alias d’inodes ne sont comptés qu’une fois. Des données privées dans un dossier inconnu restent possibles : aucun contenu n’est ouvert par cet inventaire."));
 return panel;
}
function genericMatches(item,{query="",mode="all"},now=Date.now()){
 if(query&&!item.path.toLocaleLowerCase("fr").includes(query.trim().toLocaleLowerCase("fr")))return false;
 if(mode==="over50"&&item.bytes<50*1024*1024)return false;
 if(mode==="large"&&item.bytes<250*1024*1024)return false;
 if(mode==="old"&&!(item.modifiedAt>0&&now-item.modifiedAt*1000>=90*86400000))return false;
 return true;
}
function genericToolbar(){
 const tools=node("div","media-tools");
 const search=node("input","focusable media-search");search.type="search";search.maxLength=100;search.value=state.mediaQuery;
 search.placeholder="Rechercher un chemin…";search.setAttribute("aria-label","Rechercher dans les fichiers");
 search.addEventListener("input",()=>{const pos=search.selectionStart;state.mediaQuery=search.value;state.page=0;render();const next=document.querySelector(".media-search");if(next){next.focus({preventScroll:true});if(pos!==null)next.setSelectionRange(pos,pos);}});
 add(tools,search,button("Trier : "+({size:"Taille ↓",date:"Date ↓",name:"Nom A–Z"}[state.mediaSort]),"secondary sort-cycle",cycleMediaSort));
 for(const [mode,label] of [["all","Tous"],["over50","≥ 50 Mio"],["large","≥ 250 Mio"],["old","Modifiés il y a ≥ 90 j"]]){
  const b=button(label,"secondary mode-filter"+(state.mediaMode===mode?" chosen":""),()=>setMediaMode(mode));b.dataset.mediaMode=mode;b.setAttribute("aria-pressed",String(state.mediaMode===mode));tools.append(b);
 }
 tools.append(node("p","filter-warning","Taille et ancienneté servent à explorer. Elles ne prouvent pas qu’un fichier peut être supprimé."));return tools;
}

function useReport(report,isDemo=false,live=false){
  const items=safeItems(report).sort((a,b)=>b.bytes-a.bytes);
  state.reportRevision++;
  const sizes=new Map();
  for(const item of items)if(item.category==="media"&&item.bytes>0)sizes.set(item.bytes,(sizes.get(item.bytes)||0)+1);
  for(const item of items)item.possibleTwin=item.category==="media"&&item.bytes>0&&(sizes.get(item.bytes)||0)>1;
  state.items=items;state.selected.clear();state.page=0;state.mediaMode="all";state.mediaTitle="all";state.mediaSort="size";state.mediaKind="all";state.mediaQuery="";state.demo=isDemo;state.liveReport=!!live&&!isDemo;
  // A previous exact match must never survive a refreshed/imported inventory.
  state.duplicates=null;state.diagnostics=null;state.volumes=null;state.preflight=null;state.smartSummary=null;state.homebrew=null;
  state.officialCheckedAt=null;state.officialManual.clear();
  state.coverage=safeCoverage(report.coverage);
  state.ruleVersion=typeof report.ruleVersion==="string"?report.ruleVersion.slice(0,40):"";
  state.scanStats=report.stats&&typeof report.stats==="object"?{visited:Number(report.stats.visited)||0,items:Number(report.stats.items)||0,partial:report.stats.partial===true}:null;
  state.source=isDemo?"Démonstration (fictive)":String(report.source||"Rapport importé").slice(0,60);
  state.scannedAt=typeof report.scannedAt==="string"?report.scannedAt:null;
  state.warnings=Array.isArray(report.warnings)?report.warnings.slice(0,10).map(String):[];
  render();announce(isDemo?"Mode démonstration : valeurs fictives":"Rapport importé en lecture seule");
}
function demo(){const G=1024**3,M=1024**2;useReport({schemaVersion:1,source:"Démo",scannedAt:new Date().toISOString(),warnings:["Exemple fictif : aucun espace réel de la console n'a été mesuré."],items:[
 {id:"d1",category:"media",path:"/user/av_contents/video/NPXS40087/EXEMPLE/clip.mp4",bytes:Math.round(5.6*G),risk:"review",reason:"Vidéo personnelle : vérifier avant toute suppression."},
 {id:"d2",category:"media",path:"/user/av_contents/photo/NPXS40087/EXEMPLE/capture.png",titleId:"EXEMPLE",modifiedAt:1792468800,bytes:85*M,risk:"review",reason:"Capture d'écran personnelle."},
 {id:"d6",category:"media",path:"/user/av_contents/photo/NPXS40087/EXEMPLE/capture2.png",titleId:"EXEMPLE",modifiedAt:1792468800,bytes:85*M,risk:"review",reason:"Même taille que capture.png : pas un doublon vérifié."},
 {id:"d3",category:"logs",path:"/data/etaHEN/debug.log",bytes:23*M,risk:"review",reason:"Journal diagnostic : archive recommandée."},
 {id:"d4",category:"residuals",path:"/data/etaHEN/config.ini",bytes:12*1024,risk:"protected",reason:"Configuration etaHEN : ce n'est pas un résidu avéré."},
 {id:"d5",category:"cache",path:"/data/unknown-cache",bytes:700*M,risk:"protected",reason:"Cache non qualifié : exclusion."}
 ]},true);}
function panelHeader(title,detail){const r=node("div","items-head");add(r,node("h3","",title),node("span","section-note",detail));return r;}
function categories(){
 const list=[["media","Captures & vidéos","▧","blue"],["cache","Caches & journaux","◎","purple"],["residuals","Apps & fichiers","▤","teal"]];
 const cards=node("div","card-grid");
 list.forEach(([key,title,icon,color])=>{
  const b=node("button","summary-card focusable");b.type="button";
  add(b,node("span","card-icon "+color,icon),node("span","card-title",title),node("strong","card-metric",bytes(key==="cache"?numberFor("cache")+numberFor("logs"):key==="residuals"?numberFor("residuals")+numberFor("other"):numberFor(key))),node("span","card-caption",state.items.length?"Éléments détectés · inspecter":"En attente d'un rapport"));
  b.addEventListener("click",()=>go(key));cards.append(b);
 });return cards;
}
function notice(title,desc){const el=node("div","notice");add(el,node("span","","ⓘ"),add(node("div"),node("strong","",title),node("div","",desc)));return el;}

const OFFICIAL_ROUTES=[
 {id:"undecided",title:"Comparer d'abord",impact:"Aucune action automatique. Comparer les conséquences avant de choisir."},
 {id:"stop",title:"Arrêter les payloads",impact:"Conserve le firmware actuel. N'efface pas les installations, modifications ou historiques déjà existants."},
 {id:"update",title:"Mise à jour officielle",impact:"Change le firmware ; le jailbreak peut devenir inutilisable. Ce n'est pas une opération d'effacement des traces ou des données utilisateur."},
 {id:"reset",title:"Réinitialiser la console",impact:"Efface les données utilisateur et paramètres associés. Ce n'est pas la réinstallation complète du logiciel système."},
 {id:"reinstall",title:"Réinstaller le logiciel système",impact:"Procédure officielle de dernier recours : efface les données et réinstalle le système. Aucun effet garanti sur l'historique distant ou le statut du compte."}
];
const RETURN_STEPS=[
 {id:"backup",title:"Sauvegarder les données personnelles",detail:"Vérifier les sauvegardes de jeux, les captures et les données importantes sur un support adapté. L'export du rapport JSON ne sauvegarde aucun fichier."},
 {id:"autoload",title:"Désactiver les chargements automatiques",detail:"Contrôler le gestionnaire utilisé, les fichiers d'autoload connus et les extensions configurées. Ne pas effacer un dossier actif à l'aveugle."},
 {id:"apps",title:"Vérifier les applications et plugins installés",detail:"Identifier chaque homebrew et son mode de désinstallation ; protéger les données de jeu, licences et bases système. Ne pas supposer qu'un dossier est orphelin."},
 {id:"shortcut",title:"Inspecter les raccourcis WebKit et la Toolbox",detail:"Contrôler les icônes d'accueil, le gestionnaire de payloads et les éléments de paramètres ajoutés par les homebrews. Une entrée visible peut provenir d'une injection en mémoire ; ne pas supprimer un fichier de configuration au hasard."},
 {id:"network",title:"Contrôler DNS, proxy et filtrage réseau",detail:"Consulter manuellement les paramètres réseau et les éventuelles listes de blocage. Le scanner ne peut pas certifier les réglages actifs du réseau."},
 {id:"power",title:"Effectuer un arrêt complet et vérifier le démarrage",detail:"Après avoir désactivé les chargeurs, tester un démarrage normal sans relancer volontairement de payload. Cette vérification doit être effectuée sur la vraie console."},
 {id:"system",title:"Choisir une procédure Sony adaptée",detail:"Distinguer mise à jour (changement de firmware), réinitialisation (perte de données) et réinstallation du logiciel système (perte de données et réinstallation). Procédures officielles uniquement."},
 {id:"account",title:"Vérifier l'état des comptes et des services en ligne",detail:"Aucune analyse locale ne peut connaître les décisions de Sony ni effacer un historique déjà détenu à distance. Ne considérer aucun indicateur local comme une garantie contre une suspension."}
];
function assessOfficial(snapshot){
 const entries=Array.isArray(snapshot.diagnostics?.entries)?snapshot.diagnostics.entries:[];
 const loaded=Array.isArray(snapshot.homebrew?.items)?snapshot.homebrew.items:[];
 const preflight=Array.isArray(snapshot.preflight?.entries)?snapshot.preflight.entries:[];
 const refs=entries.filter(x=>x.status==="etaHEN-reference"||x.status==="other-payload-reference");
 const toolbox=entries.filter(x=>x.status==="enabled");
 const inaccessible=entries.filter(x=>["unreadable","too-large","unknown"].includes(x.status)).length;
 const hasDirectory=preflight.some(x=>x.path==="/data/etaHEN"&&x.status==="accessible");
 const partial=snapshot.homebrew?.partial===true||!snapshot.diagnostics||!snapshot.homebrew||!snapshot.preflight;
 const findings=[
  {id:"autoload",label:"Chargement automatique",level:refs.length?"review":!snapshot.diagnostics?"unknown":"limited",
   detail:refs.length?refs.length+" fichier(s) de configuration avec référence ELF/BIN active dans les emplacements contrôlés.":!snapshot.diagnostics?"Analyse locale non exécutée.":"Aucune référence reconnue dans l'échantillon connu, sans preuve d'absence d'autres chargeurs."},
  {id:"toolbox",label:"Démarrage de la Toolbox",level:toolbox.length?"review":!snapshot.diagnostics?"unknown":"limited",
   detail:toolbox.length?"Option de démarrage observée comme activée.":"État limité aux fichiers de configuration connus."},
  {id:"homebrew",label:"Homebrews et plugins",level:loaded.length?"review":!snapshot.homebrew?"unknown":"limited",
   detail:loaded.length?loaded.length+" entrée(s) répertoriée(s) comme protégées : aucun élément n'est présumé inutile.":"Aucun élément visible dans le catalogue borné ; d'autres emplacements peuvent exister."},
  {id:"local",label:"Répertoires connus",level:hasDirectory?"review":!snapshot.preflight?"unknown":"limited",
   detail:hasDirectory?"Le dossier etaHEN est accessible : sa présence ne prouve ni son exécution ni son caractère supprimable.":"L'accès aux dossiers est vérifié partiellement ; cela ne décrit pas tous les changements du système."},
  {id:"network",label:"DNS, proxy et connexions",level:"unknown",
   detail:"Impossible à certifier à partir des dossiers examinés. Vérification manuelle nécessaire."},
  {id:"firmware",label:"Firmware et démarrage",level:"unknown",
   detail:"Version effective, modifications persistantes et compatibilité de mise à jour non certifiées par cet outil."},
  {id:"psn",label:"État des services PlayStation",level:"unknown",
   detail:"Un scanner local ne peut pas connaître l'historique conservé par Sony ni prédire une sanction."}
 ];
 const unknown=findings.filter(x=>x.level==="unknown").length+inaccessible;
 return {findings,reviewCount:findings.filter(x=>x.level==="review").length,
  unknownCount:unknown,partial,canGuaranteeSafe:false};
}
function officialPage(root){
 root.append(setTitle("Return to Live","Préparer un retour au système officiel, sans suppression automatique ni promesse de protection contre les suspensions PlayStation."));
 const banner=node("section","data-panel official-warning");
 add(banner,panelHeader("Diagnostic de retour","Préparation, pas certification"),
  notice("Aucune garantie de sécurité PSN","Des éléments non détectés, des modifications persistantes ou des informations déjà détenues à distance peuvent subsister. Une mise à jour ou réinitialisation ne garantit pas l'absence de suspension."));
 root.append(banner);
 const actions=node("section","data-panel");
 add(actions,panelHeader("Contrôles locaux en lecture seule",state.officialCheckedAt?"Exécutés pendant cette session":"À effectuer"));
 if(state.backend){
  const run=button(state.officialBusy?"Contrôles en cours…":"Examiner avant retour officiel","primary",scanOfficial);
  run.disabled=state.officialBusy;actions.append(run);
  if(state.officialBusy)actions.append(node("p","muted",state.officialStage));
 }else actions.append(node("p","muted","Le moteur natif n'est pas connecté. Le guide de retour reste disponible, mais aucun diagnostic local ne peut être attesté."));
 const findings=assessOfficial(state);
 if(state.officialCheckedAt){
  actions.append(node("p","filter-warning",findings.reviewCount+" point(s) à examiner, "+findings.unknownCount+" contrôle(s) ou signal(s) non vérifiables. Aucun état « prêt pour PSN » ne peut être délivré."));
  const list=node("div","official-findings");
  for(const item of findings.findings){
   const row=node("div","official-finding");
   const head=node("div","official-finding-head");
   add(head,node("strong","",item.label),node("span","official-level "+item.level,
    item.level==="review"?"À examiner":item.level==="limited"?"Contrôle limité":"Non vérifiable"));
   add(row,head,node("p","",item.detail));list.append(row);
  }
  actions.append(list);
  const affected=Array.isArray(state.diagnostics?.entries)?state.diagnostics.entries.filter(x=>
    x.status==="etaHEN-reference"||x.status==="other-payload-reference"||x.status==="enabled"):[];
  if(affected.length){
   const box=node("div","official-configs");
   box.append(node("strong","","Configurations connues à revoir dans leurs gestionnaires"));
   for(const entry of affected){
    const row=node("div","official-config");
    add(row,node("span","",entry.path),
      node("small","",entry.status==="enabled"?"Démarrage Toolbox configuré":"Référence de payload"));
    box.append(row);
   }
   const open=button("Examiner les installations  ›","secondary",()=>go("residuals"));
   box.append(open);actions.append(box);
  }
 }
 root.append(actions);
 const pathsPanel=node("section","data-panel");
 add(pathsPanel,panelHeader("Quelle méthode officielle envisager ?","Information seulement"));
 const pickers=node("div","official-route-pickers");
 for(const route of OFFICIAL_ROUTES){
  const b=button(route.title,"secondary"+(state.officialRoute===route.id?" chosen":""),()=>{
   state.officialRoute=route.id;render();
   document.querySelector('[data-official-route="'+route.id+'"]')?.focus({preventScroll:true});
  });
  b.dataset.officialRoute=route.id;
  b.setAttribute("aria-pressed",String(state.officialRoute===route.id));
  pickers.append(b);
 }
 pathsPanel.append(pickers);
 const choice=OFFICIAL_ROUTES.find(r=>r.id===state.officialRoute)||OFFICIAL_ROUTES[0];
 pathsPanel.append(notice(choice.title,choice.impact));
 pathsPanel.append(node("p","filter-warning","Aucune option ne garantit l'absence de suspension. Pour toute mise à jour ou réinstallation, consulter la procédure officielle et sauvegarder les données avant de commencer."));
 root.append(pathsPanel);
 const guide=node("section","data-panel");
 add(guide,panelHeader("Liste de préparation","Validation manuelle uniquement"));
 const checkList=node("div","official-checklist");
 for(const [i,step] of RETURN_STEPS.entries()){
  const line=node("label","official-step");
  const check=node("input","focusable");
  check.type="checkbox";check.checked=state.officialManual.has(step.id);
  check.setAttribute("aria-label","Étape "+(i+1)+" : "+step.title);
  const copy=add(node("div","official-step-copy"),
   node("strong","",(i+1)+". "+step.title),node("p","",step.detail));
  add(line,check,copy);
  check.addEventListener("change",()=>{
   if(check.checked)state.officialManual.add(step.id);
   else state.officialManual.delete(step.id);
   const p=document.querySelector("#official-progress");
   if(p)p.textContent=state.officialManual.size+" / "+RETURN_STEPS.length+" étapes déclarées vérifiées manuellement";
  });
  checkList.append(line);
 }
 guide.append(checkList);
 const progress=node("p","filter-warning",state.officialManual.size+" / "+RETURN_STEPS.length+" étapes déclarées vérifiées manuellement");
 progress.id="official-progress";guide.append(progress);
 const buttons=node("div","hero-buttons");
 add(buttons,button("Exporter mon plan de retour","secondary",exportOfficialPlan),
  button("Effacer mes coches","secondary",()=>{
   state.officialManual.clear();render();
  }));
 guide.append(buttons);
 guide.append(notice("Attention aux mises à jour","Une mise à jour officielle peut fermer la possibilité d'utiliser ton firmware actuel. Une réinstallation du logiciel système supprime les données de la console. Aucun bouton de cette page n'exécute ces actions."));
 root.append(guide);
 const policy=node("section","data-panel");
 add(policy,panelHeader("Limites de l'outil","Information essentielle"),
  node("p","muted","Return to Live ne modifie ni les journaux système, ni les registres Sony, ni les identifiants console. Il n'essaie pas d'effacer une activité déjà enregistrée à distance et ne se substitue pas aux procédures officielles."));
 root.append(policy);
}
async function scanOfficial(){
 if(!state.backend||state.officialBusy)return;
 state.officialBusy=true;state.officialCheckedAt=null;
 state.diagnostics=null;state.homebrew=null;state.preflight=null;
 try{
  const revision=state.reportRevision;
 const steps=[["Autoloaders et Toolbox",diagnoseNow],
   ["Homebrews et plugins",getHomebrew],
   ["Répertoires candidats",runPreflight]];
  for(const [label,fn] of steps){
   if(revision!==state.reportRevision)throw Error("Le rapport affiché a changé ; refaire le contrôle.");
   state.officialStage=label;render();
   await fn();
  }
  if(revision!==state.reportRevision)throw Error("Le rapport affiché a changé ; refaire le contrôle.");
  state.officialCheckedAt=new Date().toISOString();
  const summary=assessOfficial(state);
  announce(summary.reviewCount+" point(s) à examiner. Cette vérification ne certifie pas l'accès PSN.");
 }catch(e){announce("Contrôle interrompu : "+e.message);}
 finally{state.officialBusy=false;state.officialStage="";render();}
}
function officialPlan(snapshot,date=new Date().toISOString()){
 const assessment=assessOfficial(snapshot);
 return {
  schemaVersion:1,kind:"official-return-review-only",
  date,checked:!!snapshot.officialCheckedAt,
  manualSteps:RETURN_STEPS.map(step=>({id:step.id,checked:snapshot.officialManual instanceof Set&&snapshot.officialManual.has(step.id)})),
  chosenRoute:OFFICIAL_ROUTES.some(x=>x.id===snapshot.officialRoute)?snapshot.officialRoute:"undecided",
  findings:assessment.findings,
  reviewCount:assessment.reviewCount,unknownCount:assessment.unknownCount,
  guaranteesAccess:false,filesDeleted:0,
  note:"Rapport local partiel, sans historique PSN. Aucune opération effectuée sur les fichiers."
 };
}
function exportOfficialPlan(){
 if(typeof Blob==="undefined"||typeof URL==="undefined"||typeof URL.createObjectURL!=="function"){
  announce("L'export n'est pas pris en charge par ce navigateur.");return;
 }
 const payload=officialPlan(state);
 const url=URL.createObjectURL(new Blob([JSON.stringify(payload,null,2)],{type:"application/json"}));
 const anchor=document.createElement("a");
 anchor.href=url;anchor.download="cleanmyps5-retour-officiel.json";
 document.body.append(anchor);anchor.click();anchor.remove();
 setTimeout(()=>URL.revokeObjectURL(url),1500);
 announce("Plan de vérification exporté ; aucun fichier supprimé.");
}

function toolsPage(root){
 root.append(setTitle("Outils de stockage","Des contrôles réunis dans une seule application, sans service externe ni collecte distante."));
 const grid=node("div","tools-grid");
 const choices=[
  ["Inventaire général","Captures, système et apps inconnues · métadonnées",()=>scanNow()],
  ["Gros fichiers","Toutes les apps, ressources et autres fichiers ≥ 50 Mio",()=>{go("residuals");setMediaMode("over50");}],
  ["Fichiers anciens","Dernière modification ≥ 90 jours · ne prouve pas l’inutilité",()=>{go("residuals");setMediaMode("old");}],
  ["Captures et vidéos","Photos, clips, recherche, tri et revue",()=>go("media")],
  ["Doublons exacts","Comparaison des images sur demande · lecture bornée du contenu",()=>{go("media");}],
  ["Journaux et caches système","Bases, WAL/SHM et caches protégés",()=>go("cache")],
  ["Apps et plugins","Catalogue générique, données et ressources protégées",()=>go("residuals")],
  ["Démarrage et autoload","Diagnostic explicite des références connues · lecture de configurations",()=>go("residuals")],
  ["Carte des captures","Occupation par jeu et format",()=>go("storage")],
  ["Volumes","Capacité disponible des montages visibles, sans double total",()=>go("storage")],
  ["Couverture","Chemins absents, inaccessibles et limites de parcours",()=>go("overview")],
  ["Retour au système officiel","Contrôles et checklist manuelle",()=>go("official")]
 ];
 for(const [title,detail,action] of choices){const b=button("","tool-card",action);add(b,node("strong","",title),node("span","",detail));if(title==="Inventaire général")b.disabled=!state.backend||state.scanning;grid.append(b);}
 root.append(grid);
 root.append(notice("Fonctions non activées","Désinstallation, mises à jour d’applications, purge des caches, suppression de captures, sauvegarde/restauration et nettoyage automatique nécessitent une validation PS5 spécifique. Aucun antivirus ni gestionnaire de processus n’est fourni. Les fonctions mail, cloud et instantanés du Mac n’ont pas d’équivalent intégré ici."));
}
function overview(root){
 root.append(setTitle("Prenez soin de votre PS5.","Explorez les captures, journaux, caches et applications. Cette version analyse le stockage en lecture seule."));
 const hero=node("section","hero");const c=node("div","hero-copy");
 add(c,node("span","eyebrow","SMART SCAN · READ ONLY"),node("h2","",state.items.length?"Votre analyse est prête.":"Une vision claire de votre stockage."),node("p","",state.items.length?"Parcourez les éléments identifiés et préparez votre revue. Aucun fichier n’est modifié.":state.backend?"Le scanner local est disponible : captures, journaux système, caches et applications, au-delà d’etaHEN. Seules les métadonnées sont inventoriées.":"Chargez un rapport du scanner pour découvrir l'espace occupé. Vous pouvez aussi explorer une démonstration."));
 const actions=node("div","hero-buttons");
 if(state.backend){
  const smart=button(state.smartBusy?"Bilan en cours…":"Analyse intelligente  ✧","primary",smartCareNow);
  smart.disabled=state.smartBusy||state.scanning;
  const quick=button(state.scanning?"Inventaire en cours…":"Inventaire seul","secondary",scanNow);
  quick.disabled=state.smartBusy||state.scanning;
  add(actions,smart,quick,button("Importer un rapport","secondary",()=>$("#report-input").click()));
 }else{
  add(actions,button("Importer un rapport  ↗","primary",()=>$("#report-input").click()),button(state.items.length?"Voir les éléments":"Explorer la démo","secondary",()=>state.items.length?go("media"):demo()));
 }
 if(state.items.length)actions.append(button("Exporter l’inventaire JSON","secondary",exportInventory));
 c.append(actions);add(hero,c,add(node("div","hero-orb"),node("span","orb-shape","✧")));root.append(hero);
 if(state.smartBusy){
  const progress=node("section","data-panel smart-care-panel");
  add(progress,panelHeader("Analyse intelligente","Lecture seule"),
    node("p","muted",state.smartStage+" · aucune suppression ni optimisation du système."));
  root.append(progress);
 }
 if(state.smartSummary)root.append(smartCarePanel(state.smartSummary));
 root.append(add(node("div","section-header"),node("h3","","Les espaces à explorer"),node("span","section-note",state.demo?"Données de démonstration":state.items.length?"Données du rapport":"Aucune mesure pour l'instant")));root.append(categories());
 root.append(quickToolsPanel());
 const panel=node("section","data-panel");add(panel,panelHeader("Sécurité avant tout","Suppression désactivée"),notice("Un rapport n'est pas une preuve qu'un fichier est inutile.","Captures personnelles, sauvegardes, dossiers d'applications et données système restent protégés tant qu'aucune règle de nettoyage n'est validée."));root.append(panel);
 if(state.backend)root.append(preflightPanel());
 if(state.scanStats){const summary=node("section","data-panel");
  add(summary,panelHeader("Couverture de l'analyse",state.scanStats.partial?"Analyse partielle":"Inventaire réalisé"),
      node("p","muted",state.scanStats.visited.toLocaleString("fr-FR")+" entrées parcourues · "+state.scanStats.items.toLocaleString("fr-FR")+" fichiers répertoriés. Une analyse partielle ne garantit pas l'absence d'autres fichiers."));
  root.append(summary);
 }
 if(state.coverage.length)root.append(coveragePanel());
 if(state.warnings.length){const w=node("section","data-panel");add(w,panelHeader("Messages de l'analyse","Informations"),...state.warnings.map(s=>node("p","muted",s)));root.append(w);}
}
function quickToolsPanel(){
 const panel=node("section","data-panel");
 add(panel,panelHeader("Accès rapides","Fonctions utilisables à la manette"));
 const tools=node("div","quick-tools");
 const choices=[
  ["Captures volumineuses",()=>{go("media");setMediaMode("large");}],
  ["Captures anciennes",()=>{go("media");setMediaMode("old");}],
  ["Fichiers de même taille",()=>{go("media");setMediaMode("size");}],
  ["Applications et plugins",()=>go("residuals")],
  ["Carte de stockage",()=>go("storage")],
  ["Retour au système officiel",()=>go("official")]
 ];
 for(const [label,action] of choices){
  const b=button(label+"  ›","secondary quick-tool",action);
  tools.append(b);
 }
 panel.append(tools);
 panel.append(node("p","filter-warning","Ces raccourcis n'effectuent aucune suppression ; ils ouvrent les modules d'inspection. Le contenu disponible dépend du rapport chargé."));
 return panel;
}
function smartCarePanel(summary){
 const panel=node("section","data-panel smart-care-panel");
 add(panel,panelHeader("Bilan intelligent",summary.partial?"Résultats partiels":"Contrôles exécutés"));
 const metrics=node("div","smart-metrics");
 const fields=[
  ["Captures répertoriées",summary.mediaFiles],
  ["Captures de 50 Mio et plus",summary.largeFiles],
  ["Éléments homebrew recensés",summary.homebrewEntries],
  ["Captures modifiées il y a ≥90 j",summary.oldFiles],
  ["Groupes identiques vérifiés",summary.checks.duplicates===null?"Sur demande":summary.exactGroups],
  ["Copies supplémentaires à examiner",summary.checks.duplicates===null?"Sur demande":bytes(summary.verifiedCopyBytes)],
  ["Références autoload à examiner",summary.checks.autoload===null?"Sur demande":summary.suspiciousAutoloads],
 ];
 for(const [label,value] of fields)add(metrics,add(node("div","smart-metric"),
  node("span","",label),node("strong","",String(value))));
 panel.append(metrics);
 const steps=node("div","smart-checks");
 for(const [key,label] of [
  ["inventory","Captures / journaux"],["duplicates","Doublons exacts"],
  ["preflight","Répertoires PS5"],["volumes","Montages visibles"],
  ["autoload","Autoload / etaHEN"],["homebrew","Installations homebrew"]
 ]){
  add(steps,add(node("div","smart-check"),
   node("span","",label),node("strong","",summary.checks[key]===null?"Sur demande":summary.checks[key]?"Examiné":"Indisponible")));
 }
 panel.append(steps);
 panel.append(node("p","filter-warning","Seuls les fichiers accessibles et autorisés sont examinés. Aucun de ces volumes n'est un calcul d'espace réellement libéré. Les vérifications partielles ne prouvent pas l'absence d'autres fichiers."));
 const buttons=node("div","hero-buttons");
 add(buttons,button("Examiner les captures","secondary",()=>go("media")),
  button("Diagnostics etaHEN","secondary",()=>go("residuals")),
  button("Stockage","secondary",()=>go("storage")));
 panel.append(buttons);return panel;
}
async function smartCareNow(){
 if(!state.backend||state.smartBusy||state.scanning)return;
 state.smartBusy=true;state.smartStage="Inventaire des fichiers autorisés";state.smartSummary=null;render();
 const before=state.reportRevision;
 try{
  await scanNow();
  if(state.reportRevision===before||!state.liveReport)
   throw Error("L'inventaire n'a pas pu être chargé.");
  const revision=state.reportRevision;
  const stages=[
   ["Montages du stockage",getVolumes],
   ["Chemins d'accès",runPreflight],
   ["Installations et plugins protégés",getHomebrew]
  ];
  for(const [stage,fn] of stages){
   if(revision!==state.reportRevision)throw Error("Un autre rapport a remplacé l'analyse.");
   state.smartStage=stage;render();
   await fn();
  }
  if(revision!==state.reportRevision)throw Error("Le rapport a changé.");
  state.smartSummary=buildSmartSummary(state);
  if(state.smartSummary.partial)announce("Bilan partiel : certains contrôles sont indisponibles.");
  else announce("Bilan de lecture seule terminé.");
 }catch(error){announce("Bilan interrompu : "+error.message);}
 finally{state.smartBusy=false;state.smartStage="";render();}
}
function preflightPanel(){
 const panel=node("section","data-panel");
 add(panel,panelHeader("Diagnostic de l'environnement","Sans accéder aux fichiers personnels"));
 const actions=node("div","hero-buttons");
 const check=button(state.preflightBusy?"Vérification…":state.preflight?"Relancer les contrôles":"Vérifier les répertoires PS5","primary",runPreflight);
 check.disabled=state.preflightBusy;
 actions.append(check);
 if(state.preflight)actions.append(button("Exporter le diagnostic JSON","secondary",exportPreflight));
 panel.append(actions);
 if(state.preflight){
  const list=node("div","preflight-list");
  const labels={
   accessible:"Accessible",absent:"Absent / non monté",denied:"Accès refusé",
   symlink:"Lien symbolique (exclu)","not-directory":"Type inattendu",
   "root-unavailable":"Racine inaccessible",unknown:"État non déterminé"
  };
  const counters=new Map();
  for(const item of state.preflight.entries){
   const line=node("div","preflight-line");
   const label=labels[item.status]||labels.unknown;
   const detail=item.status==="denied"?" — Permission refusée par le système":
    item.readOnlyMount?" — Volume en lecture seule":
    item.status==="accessible"?(item.writableHint?" — Écriture théoriquement permise":" — Écriture non autorisée ou indéterminée"):
    item.status==="symlink"?" — Lien non suivi par sécurité":"";
   add(line,node("strong","",item.path),node("span","",label+detail));
   list.append(line);counters.set(item.status,(counters.get(item.status)||0)+1);
  }
  panel.append(list);
  const denied=counters.get("denied")||0;
  if(denied)panel.append(notice("Accès limités",denied+" chemin(s) refusé(s) par le système. Vérifie les privilèges réellement accordés au lanceur et au contexte de l'application native."));
  panel.append(node("p","filter-warning","Ces tests ne modifient rien. Un droit apparent d'écriture ne signifie pas que l'effacement est sûr : la galerie, les bases de données et les applications peuvent dépendre des fichiers."));
 }
 return panel;
}
async function runPreflight(){
 if(!state.backend||state.preflightBusy)return;
 state.preflightBusy=true;render();
 try{
  const response=await fetch("/api/preflight",{cache:"no-store"});
  if(!response.ok)throw Error("HTTP "+response.status);
  const raw=await response.text();
  if(raw.length>16384)throw Error("Réponse trop volumineuse");
  const data=JSON.parse(raw);
  if(data.schemaVersion!==1||data.scope!=="known-paths-only"||data.mode!=="read-only"||
     !Array.isArray(data.entries)||data.entries.length>64)throw Error("Diagnostic non conforme");
  state.preflight={entries:data.entries.map(x=>({
   path:typeof x.path==="string"?x.path.slice(0,120):"(inconnu)",
   status:typeof x.status==="string"?x.status.slice(0,40):"unknown",
   reason:typeof x.reason==="string"?x.reason.slice(0,80):"",
   writableHint:x.writableHint===true,
   readOnlyMount:x.readOnlyMount===true
  }))};
 }catch(error){announce("Diagnostic impossible : "+error.message);}
 finally{state.preflightBusy=false;render();}
}
function exportPreflight(){
 if(!state.preflight)return;
 if(typeof Blob==="undefined"||!URL?.createObjectURL){
  announce("Export non pris en charge par ce navigateur.");return;
 }
 const report={
  schemaVersion:1,kind:"non-destructive-preflight",
  collectedAt:new Date().toISOString(),
  target:state.target,entries:state.preflight.entries,
  note:"Lecture seule. L'absence d'un chemin n'indique pas un problème de firmware."
 };
 const url=URL.createObjectURL(new Blob([JSON.stringify(report,null,2)],{type:"application/json"}));
 const a=document.createElement("a");a.href=url;a.download="cleanmyps5-preflight.json";
 document.body.append(a);a.click();a.remove();
 setTimeout(()=>URL.revokeObjectURL(url),1500);
 announce("Diagnostic exporté (aucun contenu de fichier).");
}
function mediaTitles(){const arr=[...new Set(state.items.filter(x=>x.category==="media"&&x.titleId).map(x=>x.titleId))];return arr.sort();}
function compareMedia(a,b,mode){
 if(mode==="date")return (b.modifiedAt-a.modifiedAt)||(b.bytes-a.bytes)||a.path.localeCompare(b.path);
 if(mode==="name")return a.sortName.localeCompare(b.sortName,"fr")||(b.bytes-a.bytes);
 return (b.bytes-a.bytes)||a.path.localeCompare(b.path);
}
function mediaMatches(item,options){
 if(options.kind==="videos"&&!/\.(mp4|webm)$/i.test(item.path))return false;
 if(options.kind==="images"&&!/\.(png|jpe?g|jxr)$/i.test(item.path))return false;
 const query=(options.query||"").trim().toLocaleLowerCase("fr");
 if(query&&!item.path.toLocaleLowerCase("fr").includes(query)&&
    !item.titleId.toLocaleLowerCase("fr").includes(query))return false;
 return true;
}
function categoryItems(items,tab,options={}){
 return items.filter(item=>{
  const matches=tab==="cache"?["cache","logs"].includes(item.category):
    tab==="residuals"?["residuals","other"].includes(item.category):
    ["overview","storage"].includes(tab)||item.category===tab;
  return matches&&(tab==="media"||genericMatches(item,options));
 });
}
function filtered(){
 const items=categoryItems(state.items,state.tab,{query:state.mediaQuery,mode:state.mediaMode}).filter(x=>{
  if(state.tab==="media"){
   if(!mediaMatches(x,{kind:state.mediaKind,query:state.mediaQuery}))return false;
   if(state.mediaTitle!=="all"&&x.titleId!==state.mediaTitle)return false;
   if(state.mediaMode==="large"&&x.bytes<250*1024*1024)return false;
   if(state.mediaMode==="over50"&&x.bytes<50*1024*1024)return false;
   if(state.mediaMode==="old"&&!mediaOld(x))return false;
   if(state.mediaMode==="size"&&!x.possibleTwin)return false;
  }
  return true;
 });
 items.sort((a,b)=>compareMedia(a,b,state.mediaSort));
 return items;
}
function cycleMediaSort(){
 const values=["size","date","name"];
 state.mediaSort=values[(values.indexOf(state.mediaSort)+1)%values.length];
 state.page=0;render();document.querySelector(".sort-cycle")?.focus({preventScroll:true});
}
function setMediaMode(mode){
 state.mediaMode=mode;state.page=0;render();
 document.querySelector('[data-media-mode="'+mode+'"]')?.focus({preventScroll:true});
}
function setMediaKind(kind){
 if(!["all","images","videos"].includes(kind))return;
 state.mediaKind=kind;state.page=0;render();
 document.querySelector('[data-media-kind="'+kind+'"]')?.focus({preventScroll:true});
}
function cycleMediaTitle(){
 const values=["all",...mediaTitles()];const old=values.indexOf(state.mediaTitle);
 state.mediaTitle=values[(old+1)%values.length];state.page=0;render();
 document.querySelector(".title-cycle")?.focus({preventScroll:true});
}
function mediaToolbar(){
 const root=node("div","media-tools");
 const filterWrap=node("div","mode-group");add(filterWrap,node("span","control-caption","AFFICHER"));
 for(const [key,label] of [["all","Toutes"],["over50","≥ 50 Mio"],["large","≥ 250 Mio"],["old","Modifiées il y a ≥ 90 j"],["size","Même taille"]]){
  const b=button(label,"secondary mode-filter"+(state.mediaMode===key?" chosen":""),()=>setMediaMode(key));
  b.dataset.mediaMode=key;b.setAttribute("aria-pressed",String(state.mediaMode===key));filterWrap.append(b);
 }
 const titles=mediaTitles();const titleButton=button("Jeu : "+(state.mediaTitle==="all"?"Tous":state.mediaTitle),"secondary title-cycle",cycleMediaTitle);
 titleButton.disabled=!titles.length;titleButton.title="Changer de jeu";
 const sortLabels={size:"Taille ↓",date:"Date ↓",name:"Nom A–Z"};
 const sortButton=button("Trier : "+sortLabels[state.mediaSort],"secondary sort-cycle",cycleMediaSort);
 const typeFilter=node("div","mode-group");
 for(const [kind,label] of [["all","Tous formats"],["images","Images"],["videos","Vidéos"]]){
  const b=button(label,"secondary mode-filter"+(state.mediaKind===kind?" chosen":""),()=>setMediaKind(kind));
  b.dataset.mediaKind=kind;b.setAttribute("aria-pressed",String(state.mediaKind===kind));typeFilter.append(b);
 }
 const search=node("input","focusable media-search");
 search.type="text";search.inputMode="search";search.autocomplete="off";search.spellcheck=false;
 search.setAttribute("aria-label","Rechercher un fichier ou un identifiant de jeu");
 search.placeholder="Rechercher capture ou jeu…";search.maxLength=100;search.value=state.mediaQuery;
 search.addEventListener("input",()=>{
  const position=search.selectionStart;
  state.mediaQuery=search.value.slice(0,100);state.page=0;render();
  const next=document.querySelector(".media-search");
  if(next){next.focus({preventScroll:true});if(position!==null)next.setSelectionRange(position,position);}
 });
 add(root,filterWrap,titleButton,sortButton,typeFilter,search);
 root.append(node("p","filter-warning","« Anciens » utilise la date de dernière modification, et non le dernier accès ni la date de création. « Même taille » ne confirme pas un doublon. Aucune suppression automatique."));
 return root;
}
function row(x){const selected=state.selected.has(x.id);const protectedItem=x.risk==="protected";const el=node("button","file-item focusable"+(selected?" chosen":"")+(protectedItem?" protected":""));el.type="button";el.disabled=protectedItem;el.setAttribute("aria-pressed",String(selected));
 const box=node("span","file-check",selected?"✓":protectedItem?"–":"");
 const stamp=x.modifiedAt?new Date(x.modifiedAt*1000).toLocaleDateString("fr-FR"):"";
 const detail=[x.titleId,stamp,x.possibleTwin?"Même taille qu'un autre fichier":"",x.reason].filter(Boolean).join(" · ");
 const info=add(node("span","file-meta"),node("span","file-name",x.path.split("/").pop()||x.path),node("span","file-desc",x.path+" · "+detail));
 const right=add(node("span",""),node("span","file-size",bytes(x.bytes)),node("span","risk"+(protectedItem?" protected":""),protectedItem?"PROTÉGÉ":x.risk==="review"?"À VÉRIFIER":"EXAMINÉ"));
 add(el,box,info,right);el.addEventListener("click",()=>{if(state.selected.has(x.id))state.selected.delete(x.id);else state.selected.add(x.id);render(true);});
 if(state.liveReport&&state.backend&&x.category==="media"&&(x.previewPath||x.videoPath)){
   const wrapper=node("div","file-entry");wrapper.append(el);
   const view=button(x.videoPath?"Lire la vidéo  ▶":"Voir la capture  ↗","secondary preview-action",()=>openPreview(x));
   view.setAttribute("aria-label","Prévisualiser "+(x.path.split("/").pop()||"la capture"));
   wrapper.append(view);return wrapper;
 }
 return el;}
function filesPage(root){
 const title=labels[state.tab];const explain={media:"Retrouvez les photos, captures automatiques et clips vidéo. Sélection manuelle avant tout nettoyage.",cache:"Journaux diagnostiques et caches accessibles. Les caches système non identifiés restent exclus.",residuals:"Programmes, ressources, configurations et fichiers inconnus. Découverte indépendante de votre liste d’apps ; aucun élément n’est présumé inutile."};
 root.append(setTitle(title,explain[state.tab]));if(state.tab==="media")root.append(mediaToolbar());else root.append(genericToolbar());const section=node("section","data-panel");
 const items=filtered();const pageCount=Math.max(1,Math.ceil(items.length/state.pageSize));state.page=Math.min(state.page,pageCount-1);
 const from=state.page*state.pageSize;const slice=items.slice(from,from+state.pageSize);
 add(section,panelHeader("Éléments répertoriés",items.length+" élément(s)"),items.length?add(node("div","item-list"),...slice.map(row)):add(node("div","empty"),node("div","empty-glyph","◇"),node("p","","Aucun élément analysé dans cette catégorie.")));
 if(pageCount>1){const pager=node("div","pager");
  const previous=button("‹  Précédent","secondary",()=>changePage(-1));previous.disabled=state.page===0;
  const next=button("Suivant  ›","secondary",()=>changePage(1));next.disabled=state.page===pageCount-1;
  add(pager,previous,node("span","section-note","Page "+(state.page+1)+" / "+pageCount),next);section.append(pager);
 }
 const summary=node("div","review-bar");const nums=add(node("div"),node("small","","Sélection actuelle"),node("strong","",bytes(selectedBytes())));
 add(summary,nums,button("Examiner ma sélection  →","primary",review));
 section.append(summary);root.append(section);
 if(state.tab==="media")root.append(duplicatesPanel());
 if(state.tab==="residuals"){root.append(diagnosticsPanel());root.append(homebrewPanel());}
 root.append(notice("Mode sécurisé","Toutes les sélections restent locales au navigateur. Aucune API d'effacement ni modification de fichiers n'est implémentée."));
}
function openPreview(item){
 if(!state.backend||!state.liveReport||!(item.previewPath||item.videoPath))return;
 state.previousFocus=document.activeElement;
 state.preview=item;
 const shade=node("div","preview-shade");
 shade.id="preview-overlay";
 shade.setAttribute("role","dialog");shade.setAttribute("aria-modal","true");
 shade.setAttribute("aria-label","Prévisualisation de capture");
 const panel=node("div","preview-modal");
 const header=node("div","preview-header");
 const detail=node("div");
 add(detail,node("strong","",item.path.split("/").pop()||"Capture"),node("span","",bytes(item.bytes)+" · "+(item.titleId||"Jeu inconnu")));
 const close=button("✕  Fermer","secondary preview-close",closePreview);
 close.id="preview-close";
 add(header,detail,close);panel.append(header);
 if(item.videoPath){
  const video=document.createElement("video");
  video.className="preview-video";video.id="preview-video";
  video.controls=true;video.preload="metadata";video.playsInline=true;
  video.src="/api/video?file="+encodeURIComponent(item.videoPath);
  video.addEventListener("error",()=>announce("Ce clip n'a pas pu être décodé sur ce navigateur."));
  panel.append(video);
  const controls=node("div","preview-video-controls");
  const play=button("▶ Lire / Pause","secondary",()=>{
    if(video.paused){const p=video.play();if(p?.catch)p.catch(()=>announce("La lecture vidéo est indisponible."));}
    else video.pause();
  });play.id="preview-play";
  const backward=button("− 10 s","secondary",()=>{if(Number.isFinite(video.duration))video.currentTime=Math.max(0,video.currentTime-10);});
  const forward=button("+ 10 s","secondary",()=>{if(Number.isFinite(video.duration))video.currentTime=Math.min(video.duration,video.currentTime+10);});
  add(controls,play,backward,forward);panel.append(controls);
 }else{
  const image=document.createElement("img");
  image.alt="Capture locale : "+(item.path.split("/").pop()||"image");
  image.decoding="async";image.className="preview-image";
  image.src="/api/media?file="+encodeURIComponent(item.previewPath);
  image.addEventListener("error",()=>{
   image.replaceWith(node("div","preview-error","Cette image ne peut pas être ouverte (format non pris en charge ou fichier inaccessible)."));
  });
  panel.append(image);
 }
 panel.append(node("p","filter-warning","Lecture locale en mémoire uniquement. Aucun fichier n'est modifié ni exporté. Le décodage vidéo dépend du navigateur PS5."));
 shade.append(panel);document.body.append(shade);
 close.focus({preventScroll:true});
}
function closePreview(){
 const modal=document.querySelector("#preview-overlay");if(!modal)return;
 const video=modal.querySelector("video");if(video){video.pause();video.removeAttribute("src");video.load();}
 modal.remove();state.preview=null;
 const former=state.previousFocus;state.previousFocus=null;
 if(former?.isConnected)former.focus({preventScroll:true});
}
function prepareDupGroup(group){
 if(!state.liveReport||!state.backend||!group||!state.duplicates||
    state.duplicates.revision!==state.reportRevision||
    !state.duplicates.groups.includes(group)||!Array.isArray(group.paths))return;
 const byPath=new Map(state.items.filter(x=>x.category==="media"&&x.risk!=="protected").map(x=>[x.path,x]));
 // Keep the first file as an anchor; copies are flagged for human inspection,
 // not for deletion. The retained anchor is arbitrary and not a quality choice.
 let added=0;
 for(const path of group.paths.slice(1)){
  const item=byPath.get(path);
  if(!item||item.bytes!==group.bytesEach)continue;
  if(!state.selected.has(item.id)){state.selected.add(item.id);added++;}
 }
 render();
 announce(added+" copie(s) ajoutée(s) à la revue. Rien n'a été effacé.");
}
function duplicatesPanel(){
 const panel=node("section","data-panel");
 add(panel,panelHeader("Doublons vérifiés","Comparaison octet par octet"));
 if(state.backend && state.liveReport){
  const b=button(state.checkingDuplicates?"Comparaison en cours…":state.duplicates?"Relancer la vérification":"Comparer le contenu des captures","primary",checkDuplicates);
  b.disabled=state.checkingDuplicates;panel.append(b);
 }else panel.append(node("p","muted",state.backend?
   "Pour comparer les captures réelles, lance d'abord « Analyser ma PS5 » ou « Analyser le dossier ». Un rapport importé reste en lecture seule sans accès aux fichiers.":
   "Pour vérifier des doublons, ouvre le tableau de bord avec le service natif. L'import JSON n'autorise pas la lecture des fichiers."));
 if(state.duplicates){
  const data=state.duplicates;
  const n=data.groups.length;const label=n?"Groupes identiques confirmés : "+n:"Aucune paire confirmée dans l'échantillon analysé.";
  panel.append(node("p","dup-head",label));
  const duplicateBytes=data.groups.reduce((total,g)=>total+Math.max(0,g.paths.length-1)*g.bytesEach,0);
  if(duplicateBytes>0){
   const impact=node("div","dup-impact");
   add(impact,node("span","","Volume des copies supplémentaires vérifiées"),
       node("strong","",bytes(duplicateBytes)),
       node("small","","Ce volume est à examiner ; aucun octet n'est encore libéré."));
   panel.append(impact);
  }
  for(const group of data.groups.slice(0,30)){
   const el=node("div","dup-group");
   add(el,node("strong","",group.paths.length+" fichiers identiques · "+bytes(group.bytesEach)+" chacun"));
   const list=node("div","dup-list");
   for(const path of group.paths)list.append(node("div","",path));
   el.append(list);
   if(state.liveReport){
    const prepare=button("Ajouter les copies à examiner","secondary",()=>prepareDupGroup(group));
    prepare.title="Prépare uniquement la sélection ; ne supprime aucune capture";
    el.append(prepare);
   }
   panel.append(el);
  }
  panel.append(node("p","filter-warning","La première copie de chaque groupe est conservée par convention, sans garantie qu'il s'agisse de la meilleure. Vérifiez votre sélection. "+data.filesConsidered+" images considérées, "+bytes(data.comparedBytes)+" lus en comparaison. "+(data.partial?"Analyse partielle : certains doublons n'ont pas été vérifiés.":"Limites appliquées : cette vérification n'est pas exhaustive pour toute la console.")+" Images de 32 Mio maximum, 128 images et 128 Mio de lectures au plus. Aucun fichier supprimé."));
 }
 return panel;
}
async function checkDuplicates(){
 if(!state.backend||state.checkingDuplicates||!state.liveReport)return;
 const revision=state.reportRevision;
 state.checkingDuplicates=true;render();
 try{
  const resp=await fetch("/api/duplicates",{cache:"no-store"});
  if(!resp.ok)throw Error("Comparaison indisponible : "+resp.status);
  const raw=await resp.text();if(raw.length>1024*1024)throw Error("Réponse trop grande");
  const data=JSON.parse(raw);
  if(data.schemaVersion!==1||data.mode!=="read-only"||data.algorithm!=="byte-compare"||
     !Array.isArray(data.groups)||data.groups.length>128)throw Error("Format non reconnu");
  const groups=data.groups.map(g=>({
   bytesEach:Number.isSafeInteger(g.bytesEach)&&g.bytesEach>=0?g.bytesEach:0,
   paths:Array.isArray(g.paths)?g.paths.slice(0,128).map(p=>String(p).slice(0,2048)):[]
  })).filter(g=>g.paths.length>=2);
  if(revision===state.reportRevision&&state.liveReport)
   state.duplicates={groups,filesConsidered:Number(data.filesConsidered)||0,comparedBytes:Number(data.comparedBytes)||0,partial:data.partial===true,revision};
 }catch(e){announce("Comparaison impossible : "+e.message);}
 finally{state.checkingDuplicates=false;render();}
}
function homebrewPanel(){
 const panel=node("section","data-panel");
 add(panel,panelHeader("Homebrews et plugins","Inventaire protégé"));
 if(state.backend){
  const b=button(state.homebrewBusy?"Inspection en cours…":state.homebrew?"Actualiser les chemins":"Recenser les installations","primary",getHomebrew);
  b.disabled=state.homebrewBusy;panel.append(b);
 }else panel.append(node("p","muted","Nécessite le moteur natif local, sans accès en écriture."));
 if(state.homebrew){
  const list=node("div","homebrew-list");
  for(const item of state.homebrew.items){
   const line=node("div","homebrew-item");
   add(line,node("strong","",item.path),node("span","",bytes(item.nominalBytes)+" nominaux · "+item.files+" fichiers"));
   line.append(node("small","",({plugin:"Plugin",homebrew:"Homebrew","app-data":"Données d’app",installed:"Installation"}[item.kind]||"Application")+" · protégé"));
   list.append(line);
  }
  if(!state.homebrew.items.length)list.append(node("p","muted","Aucun dossier ou fichier identifiable dans les chemins connus."));
  panel.append(list);
  panel.append(node("p","filter-warning",state.homebrew.partial?
   "Inventaire PARTIEL : chemins indisponibles ou limites de parcours atteintes.":
   "Analyse terminée dans les seuls chemins candidats. La présence d'un dossier ne prouve ni une installation valide ni un résidu inutile."));
 }
 panel.append(notice("Aucune désinstallation possible ici","Découverte générique dans /data, /user/app et les homebrews USB/étendus (slots 0 à 7). Les groupes peuvent être imbriqués : ne pas additionner leurs tailles. Les données personnelles connues sont exclues du parcours. Aucune taille n’est un gain récupérable."));
 return panel;
}
async function getHomebrew(){
 if(!state.backend||state.homebrewBusy)return;
 state.homebrewBusy=true;render();
 try{
  const resp=await fetch("/api/homebrew",{cache:"no-store"});
  if(!resp.ok)throw Error("Service indisponible ("+resp.status+")");
  const raw=await resp.text();if(raw.length>256*1024)throw Error("Réponse trop volumineuse");
  const data=JSON.parse(raw);
  if(data.schemaVersion!==1||data.scope!=="protected-homebrew-metadata"||data.mode!=="read-only"||
     !Array.isArray(data.items)||data.items.length>256)throw Error("Format non reconnu");
  state.homebrew={partial:data.partial===true,items:data.items.map(v=>({
    path:typeof v.path==="string"?v.path.slice(0,2048):"(inconnu)",
    files:Number.isSafeInteger(v.files)&&v.files>=0?v.files:0,
    nominalBytes:Number.isSafeInteger(v.nominalBytes)&&v.nominalBytes>=0?v.nominalBytes:0,
    kind:["plugin","homebrew","app-data","installed"].includes(v.kind)?v.kind:"app-data",
  }))};
 }catch(error){announce("Inventaire homebrew impossible : "+error.message);}
 finally{state.homebrewBusy=false;render();}
}
function diagnosticsPanel(){
 const panel=node("section","data-panel");
 add(panel,panelHeader("Pourquoi etaHEN est encore visible ?","Diagnostic non destructif"));
 if(state.backend){
  const b=button(state.diagnosing?"Inspection…":state.diagnostics?"Actualiser le diagnostic":"Examiner les autoloads et etaHEN","primary",diagnoseNow);
  b.disabled=state.diagnosing;panel.append(b);
 }else{
  panel.append(node("p","muted","Ce diagnostic nécessite le service natif local. L'import d'un rapport reste disponible hors ligne."));
 }
 if(state.diagnostics){
  const entries=state.diagnostics.entries;const list=node("div","diag-list");
  const statuses={
    "etaHEN-reference":"Référence etaHEN trouvée : chargement possible, pas nécessairement actif",
    "other-payload-reference":"Autre ELF/BIN référencé dans l'autoloader : vérifier le gestionnaire",
    "no-reference":"Aucune référence ELF/BIN identifiée dans ce fichier",
    "enabled":"Démarrage Toolbox activé dans la configuration",
    "disabled":"Démarrage Toolbox désactivé",
    "unknown":"Réglage non identifié",
    "too-large":"Fichier trop volumineux pour cette inspection",
    "unreadable":"Impossible d'analyser ce fichier"
  };
  for(const entry of entries){
    const line=node("div","diag-row");
    add(line,node("strong","",entry.path),node("span","",statuses[entry.status]||"État inconnu"));
    list.append(line);
  }
  if(!entries.length)list.append(node("p","muted","Aucun des fichiers de configuration connus n'a été trouvé. Cela n'exclut pas un autoloader propre à une application ou une injection en mémoire."));
  panel.append(list);
  const hasAuto=entries.some(x=>x.status==="etaHEN-reference"||x.status==="other-payload-reference");
  const hasToolbox=entries.some(x=>x.status==="enabled");
  let guidance;
  if(hasAuto)guidance="Une référence à un payload persiste dans un autoloader connu. Vérifiez sa configuration avec le gestionnaire correspondant, puis effectuez un arrêt complet sans relancer l'environnement modifié. Les deux mécanismes de chargement automatique peuvent être indépendants.";
  else if(hasToolbox)guidance="Le démarrage de la Toolbox est activé dans la configuration etaHEN, mais ce paramètre ne prouve pas qu'etaHEN soit encore chargé. Inspectez les réglages du payload réellement utilisé et vérifiez après redémarrage complet.";
  else guidance="Aucune référence active n'a été détectée dans les fichiers connus. L'ancienne Toolbox peut encore être injectée en mémoire, ou un autre autoloader peut être en cause. Vérifiez après extinction complète, avant de relancer le jailbreak.";
  panel.append(notice("Interprétation suggérée",guidance));
  panel.append(node("p","filter-warning","11 emplacements standard examinés. Certaines configurations propres aux applications ne sont pas couvertes ; aucune modification n'est effectuée."));
 }
 return panel;
}
async function diagnoseNow(){
 if(!state.backend||state.diagnosing)return;
 state.diagnosing=true;render();
 try{
  const resp=await fetch("/api/diagnostics",{cache:"no-store"});
  if(!resp.ok)throw Error("Service indisponible : "+resp.status);
  const raw=await resp.text();
  if(raw.length>65536)throw Error("Rapport trop volumineux");
  const data=JSON.parse(raw);
  if(data.schemaVersion!==1||!Array.isArray(data.entries)||data.entries.length>32)throw Error("Format non reconnu");
  state.diagnostics={entries:data.entries.map(x=>({
   path:typeof x.path==="string"?x.path.slice(0,180):"(inconnu)",
   status:typeof x.status==="string"?x.status.slice(0,40):"unknown"
  }))};
 }catch(e){announce("Diagnostic impossible : "+e.message);}
 finally{state.diagnosing=false;render();}
}
function captureLensGroups(items,limit=10){
 const groups=new Map();
 for(const item of items){
  if(item.category!=="media")continue;
  const key=item.titleId||"Jeu inconnu";
  const current=groups.get(key)||{titleId:key,count:0,bytes:0,photos:0,videos:0};
  current.count++;
  current.bytes+=item.bytes;
  if(/\.(mp4|webm)$/i.test(item.path))current.videos+=item.bytes;
  else current.photos+=item.bytes;
  groups.set(key,current);
 }
 const all=[...groups.values()].sort((a,b)=>b.bytes-a.bytes);
 const totalBytes=all.reduce((sum,g)=>sum+g.bytes,0);
 const top=all.slice(0,limit).map(g=>({...g,percent:totalBytes?100*g.bytes/totalBytes:0}));
 const other=all.slice(limit);
 if(other.length){
  const rest={titleId:"Autres jeux",count:0,bytes:0,photos:0,videos:0};
  for(const g of other){
   rest.count+=g.count;rest.bytes+=g.bytes;
   rest.photos+=g.photos;rest.videos+=g.videos;
  }
  top.push({...rest,percent:totalBytes?100*rest.bytes/totalBytes:0});
 }
 return {groups:top,totalBytes,allGames:all.length};
}
function storage(root){
 root.append(setTitle("L'espace, en toute transparence.","Répartition des seuls fichiers inventoriés par le scanner — ce n'est pas la capacité totale du SSD PS5."));
 const s=node("div","data-panel");add(s,node("div","eyebrow","TAILLE DES ÉLÉMENTS INVENTORIÉS"),node("div","large-number",bytes(total())),node("div","tiny-label",state.items.length+" élément(s) au total · aucun calcul de capacité système"));
 const track=node("div","storage-track");Object.entries(categoryColors).forEach(([cat,color])=>{const n=node("span");n.style.width=(total()?numberFor(cat)/total()*100:0)+"%";n.style.background=color;track.append(n);});s.append(track);
 const legend=node("div","legend");Object.entries(categoryLabels).forEach(([cat,label])=>{if(numberFor(cat)){const labelEl=node("span");const b=node("b");b.style.background=categoryColors[cat];add(labelEl,b,document.createTextNode(label+" · "+bytes(numberFor(cat))));legend.append(labelEl);}});s.append(legend);
 root.append(s);
 const mountPanel=node("section","data-panel");
 add(mountPanel,panelHeader("Volumes montés","Mesures indépendantes"));
 if(state.backend){
  const btn=button(state.volumeBusy?"Mesure en cours…":state.volumes?"Actualiser":"Mesurer l'espace disponible","primary",getVolumes);
  btn.disabled=state.volumeBusy;mountPanel.append(btn);
 }else mountPanel.append(node("p","muted","Le calcul de l'espace disponible nécessite le service natif local."));
 if(state.volumes){
  const list=node("div","volume-list");
  for(const v of state.volumes){
   const row=node("div","volume-row");
   const statusText=v.status==="ok"?"Disponible : "+bytes(v.availableBytes)+" / "+bytes(v.totalBytes)+(v.readOnlyMount?" · lecture seule":""):
    v.status==="denied"?"Accès refusé par le système":
    v.status==="absent"?"Volume non monté":
    v.status==="symlink"?"Lien symbolique exclu":
    "Mesure indisponible";
   add(row,node("strong","",v.path),node("span","",statusText));
   if(v.sharedWith)row.append(node("small","", "Même système de fichiers que "+v.sharedWith+" — ne pas additionner"));
   list.append(row);
  }
  mountPanel.append(list);
 }
 mountPanel.append(node("p","filter-warning","Mesures statvfs de montages visibles, pas nécessairement de tout le SSD PS5. Deux chemins peuvent partager un même volume."));
 root.append(mountPanel);
 const lens=captureLensGroups(state.items);
 if(lens.groups.length){
  const perGame=node("section","data-panel");
  add(perGame,panelHeader("Carte des captures","Répartition des seuls médias inventoriés"));
  perGame.append(node("p","muted",lens.allGames+" jeux ou groupes · "+bytes(lens.totalBytes)+" au total, pas une cartographie du SSD."));
  const list=node("div","capture-lens");
  for(const group of lens.groups){
   const clickable=group.titleId!=="Jeu inconnu"&&group.titleId!=="Autres jeux";
   const element=node(clickable?"button":"div","lens-item"+(clickable?" focusable":""));
   if(clickable){
    element.type="button";
    element.addEventListener("click",()=>{state.mediaMode="all";state.mediaTitle=group.titleId;go("media");});
   }
   const row=node("div","lens-top");
   add(row,node("strong","",group.titleId),node("span","",bytes(group.bytes)+" · "+group.count+" éléments"));
   element.append(row);
   const bar=node("div","lens-bar");
   const photo=node("span","lens-photo");photo.style.width=(lens.totalBytes?100*group.photos/lens.totalBytes:0)+"%";
   const video=node("span","lens-video");video.style.width=(lens.totalBytes?100*group.videos/lens.totalBytes:0)+"%";
   add(bar,photo,video);element.append(bar);
   element.append(node("span","lens-sub",bytes(group.photos)+" images · "+bytes(group.videos)+" vidéos"+(clickable?" · ✕ explorer":"")));
   list.append(element);
  }
  perGame.append(list);
  perGame.append(node("p","filter-warning","Largeur des barres relative au total des captures inventoriées ; les éléments cachés par des limites d'analyse ne sont pas comptabilisés. Aucun dossier système n'est parcouru."));
  root.append(perGame);
 }
 root.append(add(node("div","data-panel"),panelHeader("Interprétation","Important"),notice("Stockage PS5 ≠ vue FTP /data","Un volume de 64 Go exposé par un montage /data ne représente pas forcément le SSD système. CleanMyPS5 n'affiche aucune capacité globale non vérifiée.")));
}
async function getVolumes(){
 if(!state.backend||state.volumeBusy)return;
 state.volumeBusy=true;render();
 try{
  const resp=await fetch("/api/volumes",{cache:"no-store"});
  if(!resp.ok)throw Error("Service indisponible ("+resp.status+")");
  const raw=await resp.text();if(raw.length>16384)throw Error("Rapport invalide");
  const data=JSON.parse(raw);
  if(data.schemaVersion!==1||data.scope!=="mounts-not-total-ssd"||!Array.isArray(data.volumes)||data.volumes.length>8)throw Error("Format incompatible");
  state.volumes=data.volumes.map(v=>({
   path:typeof v.path==="string"?v.path.slice(0,100):"(inconnu)",
   status:["ok","denied","absent","symlink"].includes(v.status)?v.status:"unavailable",
   readOnlyMount:v.readOnlyMount===true,
   totalBytes:Number.isSafeInteger(v.totalBytes)?v.totalBytes:0,
   availableBytes:Number.isSafeInteger(v.availableBytes)?v.availableBytes:0,
   sharedWith:typeof v.sharedWith==="string"?v.sharedWith.slice(0,100):null
  }));
 }catch(error){announce("Mesure impossible : "+error.message);}
 finally{state.volumeBusy=false;render();}
}
function closeReview(){
 const overlay=document.querySelector("#review-overlay");if(!overlay)return;
 overlay.remove();state.reviewOpen=false;
 const previous=state.previousFocus;state.previousFocus=null;
 if(previous?.isConnected)previous.focus({preventScroll:true});
}
function exportReview(items){
 if(items.length>500){announce("Export limité à 500 éléments, réduis la sélection.");return;}
 if(typeof Blob==="undefined"||typeof URL.createObjectURL!=="function"){
  announce("L'export local n'est pas pris en charge sur ce navigateur.");return;
 }
 const plan={
  schemaVersion:1,kind:"review-only",createdAt:new Date().toISOString(),
  warning:"Ce plan ne prouve pas que les fichiers sont inutiles. Aucune suppression n'a ete effectuee.",
  items:items.map(x=>({path:x.path,bytes:x.bytes,category:x.category,risk:x.risk,reason:x.reason}))
 };
 const content=JSON.stringify(plan,null,2);
 const blob=new Blob([content],{type:"application/json"});
 const url=URL.createObjectURL(blob);
 const link=document.createElement("a");link.href=url;link.download="cleanmyps5-plan.json";
 document.body.append(link);link.click();link.remove();
 setTimeout(()=>URL.revokeObjectURL(url),1500);
 announce("Plan de revue exporté, sans aucune modification sur la PS5.");
}
function review(){
 const selected=state.items.filter(x=>state.selected.has(x.id));if(!selected.length){announce("Sélectionne d'abord un ou plusieurs éléments.");return;}
 state.reviewOpen=true;state.previousFocus=document.activeElement;
 const shade=node("div","preview-shade");shade.id="review-overlay";
 shade.setAttribute("role","dialog");shade.setAttribute("aria-modal","true");
 shade.setAttribute("aria-label","Revue avant nettoyage");
 const panel=node("div","preview-modal review-modal");
 const top=node("div","preview-header");
 const title=add(node("div"),node("strong","","Revue de la sélection"),node("span","",selected.length+" éléments · "+bytes(selectedBytes())+" inventoriés"));
 const close=button("✕  Fermer","secondary",closeReview);close.id="review-close";
 add(top,title,close);panel.append(top);
 panel.append(notice("Aucune suppression possible à ce stade.","Une sélection n'indique pas qu'un fichier est inutile. Le plan sert uniquement à examiner les candidats sur ordinateur."));
 const items=node("div","review-items");
 for(const item of selected.slice(0,100)){
  const line=node("div","review-item");
  add(line,node("span","",item.path),node("strong","",bytes(item.bytes)));
  items.append(line);
 }
 if(selected.length>100)items.append(node("p","muted","+"+(selected.length-100)+" éléments non affichés dans cette prévisualisation."));
 panel.append(items);
 const actions=node("div","review-actions");
 add(actions,button("Exporter le plan JSON","primary",()=>exportReview(selected)),button("Retour à l'analyse","secondary",closeReview));
 panel.append(actions);shade.append(panel);document.body.append(shade);
 close.focus({preventScroll:true});
}
function changePage(delta){
 const maxPage=Math.max(0,Math.ceil(filtered().length/state.pageSize)-1);
 const target=Math.max(0,Math.min(maxPage,state.page+delta));
 if(target===state.page){announce(target===0?"Première page":"Dernière page");return;}
 state.page=target;render();document.querySelector(".file-item:not(:disabled), .pager button:not(:disabled)")?.focus();
}
function go(tab){if(!(tab in labels))return;state.page=0;state.mediaMode="all";state.mediaQuery="";const fromCard=document.activeElement?.classList.contains("summary-card");state.tab=tab;render();const nextFocus=fromCard?document.querySelector(".file-item:not(:disabled), .review-bar .primary"):document.querySelector(`[data-tab="${tab}"]`);nextFocus?.focus({preventScroll:true});}
function render(preserveFocus=false){
 const before=preserveFocus&&document.activeElement?.classList.contains("file-item")?Array.from(document.querySelectorAll(".file-item")).indexOf(document.activeElement):-1;
 const content=$("#content");content.replaceChildren();content.className="main-wrap";
 document.querySelectorAll("[data-tab]").forEach(b=>{const active=b.dataset.tab===state.tab;b.classList.toggle("active",active);b.setAttribute("aria-current",active?"page":"false");});
 $("#breadcrumb").textContent=labels[state.tab];$("#source-label").textContent=state.source==="none"?(state.backend?"Scanner local connecté":"Aucun scan chargé"):state.demo?"Données fictives":"Rapport : "+state.source;
 if(state.tab==="overview")overview(content);else if(state.tab==="storage")storage(content);else if(state.tab==="official")officialPage(content);else if(state.tab==="tools")toolsPage(content);else filesPage(content);
 if(before>=0){const item=document.querySelectorAll(".file-item")[before];if(item)item.focus({preventScroll:true});}
}
if(typeof document!=="undefined")document.querySelectorAll("[data-tab]").forEach(b=>b.addEventListener("click",()=>go(b.dataset.tab)));
if(typeof document!=="undefined")$("#report-input").addEventListener("change",async e=>{const file=e.target.files?.[0];if(!file)return;try{if(file.size>8*1024*1024)throw Error("Rapport supérieur à 8 Mo");useReport(JSON.parse(await file.text()));}catch(err){announce("Import refusé : "+err.message);}e.target.value="";});
function back(){if(state.reviewOpen){closeReview();return;}if(state.preview){closePreview();return;}if(state.tab!=="overview")go("overview");else announce("Vous êtes sur l'accueil.");}
function switchTab(n){const keys=Object.keys(labels);go(keys[(keys.indexOf(state.tab)+n+keys.length)%keys.length]);}
function targets(){const modal=document.querySelector("#review-overlay, #preview-overlay");
 return [...(modal||document).querySelectorAll(".focusable:not(:disabled)")].filter(el=>el.getClientRects().length);}
function move(dir){
 const all=targets();if(!all.length)return;const active=document.activeElement;const cur=all.includes(active)?active:all[0];if(!all.includes(active)){cur.focus();return;}
 const r=cur.getBoundingClientRect();const cx=r.left+r.width/2,cy=r.top+r.height/2;
 let best=null,score=Infinity;
 for(const el of all){if(el===cur)continue;const b=el.getBoundingClientRect();const dx=b.left+b.width/2-cx,dy=b.top+b.height/2-cy;
 const primary=dir==="left"?-dx:dir==="right"?dx:dir==="up"?-dy:dy;const lateral=dir==="left"||dir==="right"?Math.abs(dy):Math.abs(dx);
 if(primary<=4)continue;const s=primary+2.5*lateral;if(s<score){score=s;best=el;}}
 if(best){best.focus({preventScroll:true});best.scrollIntoView({block:"nearest",inline:"nearest"});}
}
if(typeof document!=="undefined")document.addEventListener("keydown",e=>{
 if(state.reviewOpen){
  if(["Escape","Backspace"].includes(e.key)){e.preventDefault();closeReview();}
  else if(e.key==="Tab"){
   const controls=[...document.querySelectorAll("#review-overlay .focusable:not(:disabled)")];
   if(controls.length){
    const current=controls.indexOf(document.activeElement);
    if((e.shiftKey&&current===0)||(!e.shiftKey&&current===controls.length-1)){
      e.preventDefault();controls[e.shiftKey?controls.length-1:0].focus();
    }
   }
  }else if(["ArrowUp","ArrowDown","ArrowLeft","ArrowRight"].includes(e.key)){
   e.preventDefault();move(e.key.slice(5).toLowerCase());
  }
  return;
 }
 if(state.preview){
  if(["Escape","Backspace"].includes(e.key)){e.preventDefault();closePreview();}
  else if(["ArrowUp","ArrowDown","ArrowLeft","ArrowRight"].includes(e.key)){
   e.preventDefault();move(e.key.slice(5).toLowerCase());
  }else if(e.key==="Tab"){
   const controls=[...document.querySelectorAll("#preview-overlay .focusable:not(:disabled)")];
   if(controls.length){
    const current=controls.indexOf(document.activeElement);
    e.preventDefault();controls[(current+(e.shiftKey?-1:1)+controls.length)%controls.length].focus();
   }
  }
  return;
 }
 if(e.target.matches("input,textarea"))return;
 const map={ArrowUp:"up",ArrowDown:"down",ArrowLeft:"left",ArrowRight:"right"};if(map[e.key]){e.preventDefault();move(map[e.key]);}
 if(e.key==="Escape"||e.key==="Backspace"){e.preventDefault();back();}
 if(e.key==="["||e.key==="PageUp"){e.preventDefault();switchTab(-1);}
 if(e.key==="]"||e.key==="PageDown"){e.preventDefault();switchTab(1);}
 if(e.key===","&&["media","cache","residuals"].includes(state.tab)){e.preventDefault();changePage(-1);}
 if(e.key==="."&&["media","cache","residuals"].includes(state.tab)){e.preventDefault();changePage(1);}
});
let previousButtons=new Set(),lastAxes=[0,0];
function gamepadLoop(){
 const pads=navigator.getGamepads?.()||[];const p=Array.from(pads).find(Boolean);
 if(p){const pressed=new Set();p.buttons.forEach((b,i)=>{if(b.pressed)pressed.add(i);});
 const edge=i=>pressed.has(i)&&!previousButtons.has(i);
 if(state.reviewOpen){
  if(edge(1))closeReview();
  else if(edge(0))document.activeElement?.click();
  if(edge(12))move("up");if(edge(13))move("down");if(edge(14))move("left");if(edge(15))move("right");
  previousButtons=pressed;requestAnimationFrame(gamepadLoop);return;
 }
 if(state.preview){
  if(edge(1))closePreview();
  else if(edge(0)){
   const focused=document.activeElement;
   if(focused?.classList.contains("focusable"))focused.click();
  }
  if(edge(12))move("up");if(edge(13))move("down");
  if(edge(14))move("left");if(edge(15))move("right");
  previousButtons=pressed;requestAnimationFrame(gamepadLoop);return;
 }
 if(edge(0)){const el=document.activeElement?.matches(".focusable")?document.activeElement:targets()[0];el?.click();}
 if(edge(1))back();if(edge(4))switchTab(-1);if(edge(5))switchTab(1);if(edge(6)&&["media","cache","residuals"].includes(state.tab))changePage(-1);if(edge(7)&&["media","cache","residuals"].includes(state.tab))changePage(1);
 if(edge(12))move("up");if(edge(13))move("down");if(edge(14))move("left");if(edge(15))move("right");
 const axes=[p.axes[0]||0,p.axes[1]||0];
 const directions=[axes[0]<-.65?"left":axes[0]>.65?"right":null,axes[1]<-.65?"up":axes[1]>.65?"down":null];
 directions.forEach((dir,i)=>{if(dir&&lastAxes[i]===0)move(dir);});lastAxes=directions.map(Boolean).map(Number);
 previousButtons=pressed;
 }else{previousButtons.clear();lastAxes=[0,0];}
 requestAnimationFrame(gamepadLoop);
}
if(typeof window!=="undefined")window.addEventListener("gamepadconnected",()=>announce("Manette connectée · ✕ sélectionner · ◯ retour"));
async function findNativeServer(){
 try{
  const response=await fetch("/api/status",{cache:"no-store"});
  if(!response.ok)return;
  const status=await response.json();
  if(status.app==="CleanMyPS5"&&status.mode==="read-only"&&status.api===1){
   state.backend=true;state.target=status.target==="ps5"?"ps5":"host";
   if(typeof status.version==="string"&&/^[0-9A-Za-z.-]{1,32}$/.test(status.version)){
    const badge=document.querySelector(".build-tag");
    if(badge)badge.textContent="PREVIEW "+status.version;
    const footer=document.querySelector(".footer-end");
    if(footer)footer.textContent="CleanMyPS5 • "+status.version+" • lecture seule";
   }
   render();
  }
 }catch(_error){/* Static preview: importing JSON remains available. */}
}
async function scanNow(){
 if(!state.backend||state.scanning)return;
 const revision=state.reportRevision;
 state.scanning=true;render();
 try{
  const response=await fetch("/api/report",{cache:"no-store"});
  if(!response.ok)throw Error("Service d'analyse indisponible ("+response.status+")");
  const type=response.headers.get("content-type")||"";
  if(!type.includes("application/json"))throw Error("Réponse non JSON");
  const raw=await response.text();
  if(raw.length>8*1024*1024)throw Error("Rapport trop volumineux");
  if(revision===state.reportRevision)useReport(JSON.parse(raw),false,true);
 }catch(error){if(revision===state.reportRevision)announce("Analyse impossible : "+error.message);}
 finally{state.scanning=false;render();}
}
if(typeof document!=="undefined"){
 render();requestAnimationFrame(gamepadLoop);
 findNativeServer();
}
// Node-only exports for behavioral regression tests; never available through
// the embedded browser's ordinary script-tag execution.
if(typeof module!=="undefined"&&module.exports){
 module.exports={categoryItems,inventoryExport,safeCoverage,genericMatches,safeItems,compareMedia,mediaOld,buildSmartSummary,captureLensGroups,mediaMatches,assessOfficial,officialPlan,RETURN_STEPS,OFFICIAL_ROUTES};
}

