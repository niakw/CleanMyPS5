"use strict";
// The UI never sends a delete request. Reports are untrusted data, rendered via textContent.
const labels={overview:"Vue d'ensemble",media:"Captures & vidéos",cache:"Caches & journaux",residuals:"Installations",storage:"Stockage"};
const categoryLabels={media:"Captures & vidéos",cache:"Caches",logs:"Journaux",residuals:"Résidus",other:"Autres"};
const categoryColors={media:"#67baff",cache:"#b28dff",logs:"#68d9be",residuals:"#f1b86f",other:"#8192b8"};
const state={tab:"overview",items:[],selected:new Set(),source:"none",scannedAt:null,warnings:[],demo:false,backend:false,scanning:false,target:"host",page:0,pageSize:36};
const $=s=>document.querySelector(s);
function node(tag,cls,text){const el=document.createElement(tag);if(cls)el.className=cls;if(text!==undefined)el.textContent=text;return el;}
function add(parent,...children){children.forEach(x=>parent.append(x));return parent;}
function bytes(value){if(!Number.isFinite(value)||value<0)return "0 o";const u=["o","Ko","Mo","Go","To"];let i=0;while(value>=1024&&i<u.length-1){value/=1024;i++;}return (i?value.toLocaleString("fr-FR",{maximumFractionDigits:1}):Math.round(value).toLocaleString("fr-FR"))+" "+u[i];}
function setTitle(title,lead){const e=add(node("div",""),node("div","eyebrow","NETTOYAGE INTELLIGENT"),node("h1","page-heading",title),node("p","lead",lead));return e;}
function button(text,cls,fn){const b=node("button",cls+" focusable",text);b.type="button";b.addEventListener("click",fn);return b;}
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
    return {id,path:v.path,bytes:v.bytes,category:Object.prototype.hasOwnProperty.call(categoryLabels,v.category)?v.category:"other",risk:["review","safe","protected"].includes(v.risk)?v.risk:"protected",reason:String(v.reason||"Analyse locale, aucune suppression automatique.").slice(0,400)};
  });
}
function useReport(report,isDemo=false){
  const items=safeItems(report).sort((a,b)=>b.bytes-a.bytes);state.items=items;state.selected.clear();state.page=0;state.demo=isDemo;
  state.source=isDemo?"Démonstration (fictive)":String(report.source||"Rapport importé").slice(0,60);
  state.scannedAt=typeof report.scannedAt==="string"?report.scannedAt:null;
  state.warnings=Array.isArray(report.warnings)?report.warnings.slice(0,10).map(String):[];
  render();announce(isDemo?"Mode démonstration : valeurs fictives":"Rapport importé en lecture seule");
}
function demo(){const G=1024**3,M=1024**2;useReport({schemaVersion:1,source:"Démo",scannedAt:new Date().toISOString(),warnings:["Exemple fictif : aucun espace réel de la console n'a été mesuré."],items:[
 {id:"d1",category:"media",path:"/user/av_contents/video/NPXS40087/EXEMPLE/clip.mp4",bytes:Math.round(5.6*G),risk:"review",reason:"Vidéo personnelle : vérifier avant toute suppression."},
 {id:"d2",category:"media",path:"/user/av_contents/photo/NPXS40087/EXEMPLE/capture.png",bytes:85*M,risk:"review",reason:"Capture d'écran personnelle."},
 {id:"d3",category:"logs",path:"/data/etaHEN/debug.log",bytes:23*M,risk:"review",reason:"Journal diagnostic : archive recommandée."},
 {id:"d4",category:"residuals",path:"/data/etaHEN/config.ini",bytes:12*1024,risk:"protected",reason:"Configuration etaHEN : ce n'est pas un résidu avéré."},
 {id:"d5",category:"cache",path:"/data/unknown-cache",bytes:700*M,risk:"protected",reason:"Cache non qualifié : exclusion."}
 ]},true);}
function panelHeader(title,detail){const r=node("div","items-head");add(r,node("h3","",title),node("span","section-note",detail));return r;}
function categories(){
 const list=[["media","Captures & vidéos","▧","blue"],["cache","Caches & journaux","◎","purple"],["residuals","Installations","▤","teal"]];
 const cards=node("div","card-grid");
 list.forEach(([key,title,icon,color])=>{
  const b=node("button","summary-card focusable");b.type="button";
  add(b,node("span","card-icon "+color,icon),node("span","card-title",title),node("strong","card-metric",bytes(key==="cache"?numberFor("cache")+numberFor("logs"):numberFor(key))),node("span","card-caption",state.items.length?"Éléments détectés · inspecter":"En attente d'un rapport"));
  b.addEventListener("click",()=>go(key));cards.append(b);
 });return cards;
}
function notice(title,desc){const el=node("div","notice");add(el,node("span","","ⓘ"),add(node("div"),node("strong","",title),node("div","",desc)));return el;}
function overview(root){
 root.append(setTitle("Prenez soin de votre PS5.","Une vision claire de votre stockage, des captures volumineuses aux fichiers résiduels. Rien n'est supprimé sans vérification."));
 const hero=node("section","hero");const c=node("div","hero-copy");
 add(c,node("span","eyebrow","SMART SCAN · READ ONLY"),node("h2","",state.items.length?"Votre analyse est prête.":"Une console propre. L'esprit tranquille."),node("p","",state.items.length?"Parcourez les éléments identifiés et préparez votre sélection, sans risque pour la console.":state.backend?"Le scanner local est disponible. Lancez un inventaire en lecture seule de vos captures et fichiers etaHEN.":"Chargez un rapport du scanner pour découvrir l'espace occupé. Vous pouvez aussi explorer une démonstration."));
 const actions=node("div","hero-buttons");
 if(state.backend){
  add(actions,button(state.scanning?"Analyse en cours…":state.target==="ps5"?"Analyser ma PS5  ↗":"Analyser le dossier  ↗","primary",scanNow),button("Importer un rapport","secondary",()=>$("#report-input").click()));
  if(state.scanning)actions.firstChild.disabled=true;
 }else{
  add(actions,button("Importer un rapport  ↗","primary",()=>$("#report-input").click()),button(state.items.length?"Voir les éléments":"Explorer la démo","secondary",()=>state.items.length?go("media"):demo()));
 }
 c.append(actions);add(hero,c,add(node("div","hero-orb"),node("span","orb-shape","✧")));root.append(hero);
 root.append(add(node("div","section-header"),node("h3","","Les espaces à explorer"),node("span","section-note",state.demo?"Données de démonstration":state.items.length?"Données du rapport":"Aucune mesure pour l'instant")));root.append(categories());
 const panel=node("section","data-panel");add(panel,panelHeader("Sécurité avant tout","Suppression désactivée"),notice("Un rapport n'est pas une preuve qu'un fichier est inutile.","Captures personnelles, sauvegardes, dossiers d'applications et données système restent protégés tant qu'aucune règle de nettoyage n'est validée."));root.append(panel);
 if(state.warnings.length){const w=node("section","data-panel");add(w,panelHeader("Messages de l'analyse","Informations"),...state.warnings.map(s=>node("p","muted",s)));root.append(w);}
}
function filtered(){return state.items.filter(x=>state.tab==="cache"?["cache","logs"].includes(x.category):state.tab==="storage"||state.tab==="overview"?true:x.category===state.tab);}
function row(x){const selected=state.selected.has(x.id);const protectedItem=x.risk==="protected";const el=node("button","file-item focusable"+(selected?" chosen":"")+(protectedItem?" protected":""));el.type="button";el.disabled=protectedItem;el.setAttribute("aria-pressed",String(selected));
 const box=node("span","file-check",selected?"✓":protectedItem?"–":"");
 const info=add(node("span","file-meta"),node("span","file-name",x.path.split("/").pop()||x.path),node("span","file-desc",x.path+" · "+x.reason));
 const right=add(node("span",""),node("span","file-size",bytes(x.bytes)),node("span","risk"+(protectedItem?" protected":""),protectedItem?"PROTÉGÉ":x.risk==="review"?"À VÉRIFIER":"EXAMINÉ"));
 add(el,box,info,right);el.addEventListener("click",()=>{if(state.selected.has(x.id))state.selected.delete(x.id);else state.selected.add(x.id);render(true);});return el;}
function filesPage(root){
 const title=labels[state.tab];const explain={media:"Retrouvez les photos, captures automatiques et clips vidéo. Sélection manuelle avant tout nettoyage.",cache:"Journaux diagnostiques et caches accessibles. Les caches système non identifiés restent exclus.",residuals:"Inspection des anciennes installations et configurations résiduelles : aucun élément n'est présumé inutile."};
 root.append(setTitle(title,explain[state.tab]));const section=node("section","data-panel");
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
 section.append(summary);root.append(section);root.append(notice("Mode sécurisé","Toutes les sélections restent locales au navigateur. Aucune API d'effacement ni modification de fichiers n'est implémentée."));
}
function storage(root){
 root.append(setTitle("L'espace, en toute transparence.","Répartition des seuls fichiers inventoriés par le scanner — ce n'est pas la capacité totale du SSD PS5."));
 const s=node("div","data-panel");add(s,node("div","eyebrow","TAILLE DES ÉLÉMENTS INVENTORIÉS"),node("div","large-number",bytes(total())),node("div","tiny-label",state.items.length+" élément(s) au total · aucun calcul de capacité système"));
 const track=node("div","storage-track");Object.entries(categoryColors).forEach(([cat,color])=>{const n=node("span");n.style.width=(total()?numberFor(cat)/total()*100:0)+"%";n.style.background=color;track.append(n);});s.append(track);
 const legend=node("div","legend");Object.entries(categoryLabels).forEach(([cat,label])=>{if(numberFor(cat)){const labelEl=node("span");const b=node("b");b.style.background=categoryColors[cat];add(labelEl,b,document.createTextNode(label+" · "+bytes(numberFor(cat))));legend.append(labelEl);}});s.append(legend);
 root.append(s);root.append(add(node("div","data-panel"),panelHeader("Interprétation","Important"),notice("Stockage PS5 ≠ vue FTP /data","Un volume de 64 Go exposé par un montage /data ne représente pas forcément le SSD système. CleanMyPS5 n'affiche aucune capacité globale non vérifiée.")));
}
function review(){
 const selected=state.items.filter(x=>state.selected.has(x.id));if(!selected.length){announce("Sélectionne d'abord un ou plusieurs éléments.");return;}
 const names=selected.slice(0,3).map(x=>x.path.split("/").pop()).join(", ");
 announce(selected.length+" élément(s) · "+bytes(selectedBytes())+" · prévisualisation uniquement ("+names+")");
}
function changePage(delta){
 const maxPage=Math.max(0,Math.ceil(filtered().length/state.pageSize)-1);
 const target=Math.max(0,Math.min(maxPage,state.page+delta));
 if(target===state.page){announce(target===0?"Première page":"Dernière page");return;}
 state.page=target;render();document.querySelector(".file-item:not(:disabled), .pager button:not(:disabled)")?.focus();
}
function go(tab){if(!(tab in labels))return;state.page=0;const fromCard=document.activeElement?.classList.contains("summary-card");state.tab=tab;render();const nextFocus=fromCard?document.querySelector(".file-item:not(:disabled), .review-bar .primary"):document.querySelector(`[data-tab="${tab}"]`);nextFocus?.focus({preventScroll:true});}
function render(preserveFocus=false){
 const before=preserveFocus&&document.activeElement?.classList.contains("file-item")?Array.from(document.querySelectorAll(".file-item")).indexOf(document.activeElement):-1;
 const content=$("#content");content.replaceChildren();content.className="main-wrap";
 document.querySelectorAll("[data-tab]").forEach(b=>{const active=b.dataset.tab===state.tab;b.classList.toggle("active",active);b.setAttribute("aria-current",active?"page":"false");});
 $("#breadcrumb").textContent=labels[state.tab];$("#source-label").textContent=state.source==="none"?(state.backend?"Scanner local connecté":"Aucun scan chargé"):state.demo?"Données fictives":"Rapport : "+state.source;
 if(state.tab==="overview")overview(content);else if(state.tab==="storage")storage(content);else filesPage(content);
 if(before>=0){const item=document.querySelectorAll(".file-item")[before];if(item)item.focus({preventScroll:true});}
}
document.querySelectorAll("[data-tab]").forEach(b=>b.addEventListener("click",()=>go(b.dataset.tab)));
$("#report-input").addEventListener("change",async e=>{const file=e.target.files?.[0];if(!file)return;try{if(file.size>8*1024*1024)throw Error("Rapport supérieur à 8 Mo");useReport(JSON.parse(await file.text()));}catch(err){announce("Import refusé : "+err.message);}e.target.value="";});
function back(){if(state.tab!=="overview")go("overview");else announce("Vous êtes sur l'accueil.");}
function switchTab(n){const keys=Object.keys(labels);go(keys[(keys.indexOf(state.tab)+n+keys.length)%keys.length]);}
function targets(){return [...document.querySelectorAll(".focusable:not(:disabled)")].filter(el=>el.getClientRects().length);}
function move(dir){
 const all=targets();if(!all.length)return;const active=document.activeElement;const cur=all.includes(active)?active:all[0];if(!all.includes(active)){cur.focus();return;}
 const r=cur.getBoundingClientRect();const cx=r.left+r.width/2,cy=r.top+r.height/2;
 let best=null,score=Infinity;
 for(const el of all){if(el===cur)continue;const b=el.getBoundingClientRect();const dx=b.left+b.width/2-cx,dy=b.top+b.height/2-cy;
 const primary=dir==="left"?-dx:dir==="right"?dx:dir==="up"?-dy:dy;const lateral=dir==="left"||dir==="right"?Math.abs(dy):Math.abs(dx);
 if(primary<=4)continue;const s=primary+2.5*lateral;if(s<score){score=s;best=el;}}
 if(best){best.focus({preventScroll:true});best.scrollIntoView({block:"nearest",inline:"nearest"});}
}
document.addEventListener("keydown",e=>{if(e.target.matches("input,textarea"))return;
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
window.addEventListener("gamepadconnected",()=>announce("Manette connectée · ✕ sélectionner · ◯ retour"));
async function findNativeServer(){
 try{
  const response=await fetch("/api/status",{cache:"no-store"});
  if(!response.ok)return;
  const status=await response.json();
  if(status.app==="CleanMyPS5"&&status.mode==="read-only"&&status.api===1){state.backend=true;state.target=status.target==="ps5"?"ps5":"host";render();}
 }catch(_error){/* Static preview: importing JSON remains available. */}
}
async function scanNow(){
 if(!state.backend||state.scanning)return;
 state.scanning=true;render();
 try{
  const response=await fetch("/api/report",{cache:"no-store"});
  if(!response.ok)throw Error("Service d'analyse indisponible ("+response.status+")");
  const type=response.headers.get("content-type")||"";
  if(!type.includes("application/json"))throw Error("Réponse non JSON");
  const raw=await response.text();
  if(raw.length>8*1024*1024)throw Error("Rapport trop volumineux");
  useReport(JSON.parse(raw));
 }catch(error){announce("Analyse impossible : "+error.message);}
 finally{state.scanning=false;render();}
}
render();requestAnimationFrame(gamepadLoop);
findNativeServer();
