// admin_handlers.cpp — HTML admin UI for the Epicollect5-compatible server.
//
// Routes:
//   GET /admin   — two-tab UI: Build Project | Entries Viewer

#include "admin_handlers.hpp"

#include "cors_middleware.hpp"

#include <nlohmann/json.hpp>

#include <iostream>
#include <string>

using namespace reactor;
using json = nlohmann::json;

namespace ec5
{

// ---------------------------------------------------------------------------
// The full admin HTML page (self-contained, no external assets)
// ---------------------------------------------------------------------------
static const std::string ADMIN_HTML = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>EC5 Admin</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,system-ui,sans-serif;background:#f7f8fa;color:#1f2328;font-size:14px;line-height:1.6}
header{background:#1f2328;color:#fff;padding:12px 24px;display:flex;align-items:center;gap:12px}
header h1{font-size:17px;font-weight:600}
header span{font-size:12px;color:#8b949e;margin-left:auto}

/* top-level tabs (full width) */
.page-tabs{display:flex;background:#fff;border-bottom:2px solid #e5e7eb;padding:0 24px}
.page-tab{padding:10px 18px;cursor:pointer;font-size:13px;font-weight:500;color:#57606a;border-bottom:2px solid transparent;margin-bottom:-2px;user-select:none}
.page-tab.active{color:#3b82d4;border-bottom-color:#3b82d4}
.page-panel{display:none;height:calc(100vh - 88px);overflow:hidden}
.page-panel.active{display:block}

/* two-column layout (used by Build + Entries) */
.layout{display:grid;grid-template-columns:1fr 1fr;height:100%;overflow:hidden}
.panel{padding:20px 24px;overflow-y:auto;border-right:1px solid #e5e7eb}
.panel:last-child{border-right:none}

/* typography */
h2{font-size:14px;font-weight:600;margin-bottom:14px;padding-bottom:7px;border-bottom:1px solid #e5e7eb}
label{display:block;font-size:12px;font-weight:500;color:#57606a;margin-bottom:3px;margin-top:10px}
input[type=text],input[type=password],textarea,select{width:100%;padding:6px 10px;border:1px solid #d0d7de;border-radius:6px;font-size:13px;font-family:inherit;background:#fff}
input:focus,textarea:focus,select:focus{outline:none;border-color:#3b82d4;box-shadow:0 0 0 3px rgba(59,130,212,.12)}
textarea{resize:vertical;min-height:56px}

/* buttons */
.btn{display:inline-flex;align-items:center;gap:5px;padding:6px 13px;border:none;border-radius:6px;font-size:13px;font-weight:500;cursor:pointer;transition:background .15s}
.btn-primary{background:#3b82d4;color:#fff}.btn-primary:hover{background:#2563b0}
.btn-danger{background:#ef4444;color:#fff}.btn-danger:hover{background:#dc2626}
.btn-secondary{background:#e5e7eb;color:#1f2328}.btn-secondary:hover{background:#d1d5db}
.btn-green{background:#16a34a;color:#fff}.btn-green:hover{background:#15803d}
.btn-sm{padding:3px 9px;font-size:12px}

/* form builder */
.input-row{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.form-block{border:1px solid #e5e7eb;border-radius:8px;padding:12px;margin-top:10px;background:#fff}
.form-block-header{display:flex;align-items:center;justify-content:space-between;margin-bottom:8px}
.input-block{border:1px solid #e5e7eb;border-radius:6px;padding:9px;margin-top:7px;background:#f7f8fa}
.input-block-header{display:flex;align-items:center;justify-content:space-between;margin-bottom:6px}
.remove-btn{color:#ef4444;background:none;border:none;cursor:pointer;font-size:17px;line-height:1;padding:0 3px}
.actions{display:flex;gap:8px;margin-top:18px;flex-wrap:wrap}
#build-result{margin-top:12px;padding:10px 12px;border-radius:6px;font-size:13px;display:none}
#build-result.ok{background:#dcfce7;color:#166534;border:1px solid #bbf7d0}
#build-result.err{background:#fee2e2;color:#991b1b;border:1px solid #fecaca}

/* project cards */
.project-card{background:#fff;border:1px solid #e5e7eb;border-radius:8px;padding:12px;margin-bottom:9px}
.project-card h3{font-size:13px;font-weight:600;margin-bottom:3px}
.project-card .slug{font-family:monospace;font-size:12px;color:#3b82d4}
.project-card .meta{font-size:11px;color:#57606a;margin-top:3px}
.badge{display:inline-block;font-size:11px;padding:1px 7px;border-radius:10px;font-weight:500}
.badge-green{background:#dcfce7;color:#166534}
.empty{color:#57606a;font-size:13px;text-align:center;padding:28px 0}

/* entries viewer */
.toolbar{display:flex;gap:8px;align-items:flex-end;flex-wrap:wrap;margin-bottom:14px}
.toolbar select{width:auto;min-width:160px}
.toolbar label{margin:0;font-size:12px;font-weight:500;color:#57606a}
.toolbar .field{display:flex;flex-direction:column;gap:3px}
#entries-status{font-size:12px;color:#57606a;margin-left:auto;align-self:center}
.entry-table-wrap{overflow-x:auto;border:1px solid #e5e7eb;border-radius:8px;background:#fff}
table{width:100%;border-collapse:collapse;font-size:12px}
thead th{background:#f7f8fa;padding:8px 10px;text-align:left;font-weight:600;border-bottom:1px solid #e5e7eb;white-space:nowrap}
tbody td{padding:7px 10px;border-bottom:1px solid #f0f0f0;vertical-align:top;max-width:220px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
tbody tr:last-child td{border-bottom:none}
tbody tr:hover{background:#f7f8fa}
.pagination{display:flex;gap:8px;align-items:center;margin-top:12px;font-size:13px}
.pagination button{padding:4px 10px}
#page-info{color:#57606a;font-size:12px}

/* header login widget */
header{background:#1f2328;color:#fff;padding:0 24px;display:flex;align-items:stretch;gap:16px;min-height:50px}
header h1{font-size:17px;font-weight:600;display:flex;align-items:center}
.hdr-login{display:flex;align-items:center;gap:8px;margin-left:auto}
.hdr-login input{padding:4px 8px;border:1px solid #444;border-radius:5px;background:#2d2d2d;color:#fff;font-size:12px;width:150px}
.hdr-login input::placeholder{color:#8b949e}
.hdr-login input:focus{outline:none;border-color:#3b82d4}
.hdr-field{display:flex;flex-direction:column;gap:1px}
.hdr-field label{font-size:10px;color:#8b949e;margin:0}
.btn-hdr{padding:4px 11px;font-size:12px;border:none;border-radius:5px;cursor:pointer;font-weight:500;white-space:nowrap}
.btn-hdr-login{background:#3b82d4;color:#fff}.btn-hdr-login:hover{background:#2563b0}
.btn-hdr-logout{background:#374151;color:#d1d5db}.btn-hdr-logout:hover{background:#4b5563}
#hdr-status{font-size:12px;color:#4ade80}
#hdr-err{font-size:11px;color:#f87171}

/* inner tabs (Build panel) */
.tabs{display:flex;border-bottom:1px solid #e5e7eb;margin-bottom:14px}
.tab{padding:6px 14px;cursor:pointer;font-size:12px;font-weight:500;color:#57606a;border-bottom:2px solid transparent;margin-bottom:-1px}
.tab.active{color:#3b82d4;border-bottom-color:#3b82d4}
.tab-panel{display:none}.tab-panel.active{display:block}
</style>
</head>
<body>
<header>
  <h1>&#9881; EC5 Admin</h1>

  <!-- logged-out state: inline login form -->
  <div id="hdr-loggedout" class="hdr-login">
    <div class="hdr-field">
      <label>Email</label>
      <input type="text" id="hdr-email" placeholder="user@example.com" autocomplete="username">
    </div>
    <div class="hdr-field">
      <label>Password</label>
      <input type="password" id="hdr-pass" placeholder="password" autocomplete="current-password"
             onkeydown="if(event.key==='Enter') doLogin()">
    </div>
    <button class="btn-hdr btn-hdr-login" onclick="doLogin()">Log in</button>
    <span id="hdr-err"></span>
  </div>

  <!-- logged-in state: user badge + logout -->
  <div id="hdr-loggedin" class="hdr-login" style="display:none">
    <span id="hdr-status" class="logged-in"></span>
    <button class="btn-hdr btn-hdr-logout" onclick="doLogout()">Log out</button>
  </div>
</header>

<!-- top-level page tabs -->
<div class="page-tabs">
  <div class="page-tab active" onclick="showPage('build')">&#128196; Build Project</div>
  <div class="page-tab"        onclick="showPage('entries')">&#128202; Entries Viewer</div>
  <div class="page-tab"        onclick="showPage('users')">&#128100; Users</div>
</div>

<!-- ═══════════════ PAGE: BUILD ═══════════════ -->
<div id="page-build" class="page-panel active">
<div class="layout">

<!-- left: builder -->
<div class="panel">
  <div class="tabs">
    <div class="tab active" onclick="switchBuildTab('form')">Form Builder</div>
    <div class="tab"        onclick="switchBuildTab('raw')">Raw JSON</div>
  </div>

  <div id="btab-form" class="tab-panel active">
    <h2>Project Details</h2>
    <label>Project Name *</label>
    <input type="text" id="p-name" placeholder="My Survey" oninput="onNameChange()">
    <label>Slug (auto-generated, editable) *</label>
    <input type="text" id="p-slug" placeholder="my-survey">
    <label>Description</label>
    <textarea id="p-desc" rows="2" placeholder="Short description"></textarea>
    <div class="input-row">
      <div><label>Access</label>
        <select id="p-access"><option value="public">Public</option><option value="private">Private</option></select>
      </div>
      <div><label>Status</label>
        <select id="p-status"><option value="active">Active</option><option value="locked">Locked</option></select>
      </div>
    </div>
    <h2 style="margin-top:18px">Forms</h2>
    <div id="forms-container"></div>
    <button class="btn btn-secondary btn-sm" style="margin-top:10px" onclick="addForm()">+ Add Form</button>
    <div class="actions">
      <button class="btn btn-primary" onclick="submitProject()">&#10003; Save Project</button>
      <button class="btn btn-secondary" onclick="resetBuilder()">&#8635; Reset</button>
    </div>
    <div id="build-result"></div>
  </div>

  <div id="btab-raw" class="tab-panel">
    <h2>Raw definition_json Preview</h2>
    <p style="font-size:12px;color:#57606a;margin-bottom:8px">Read-only. Switch to Form Builder to edit.</p>
    <textarea id="raw-preview" rows="32" readonly style="font-family:monospace;font-size:11px"></textarea>
  </div>
</div>

<!-- right: project list -->
<div class="panel">
  <div style="display:flex;align-items:center;justify-content:space-between;margin-bottom:14px">
    <h2 style="margin:0;border:none;padding:0">Existing Projects</h2>
    <button class="btn btn-secondary btn-sm" onclick="loadProjectList()">&#8635; Refresh</button>
  </div>
  <div id="project-list"><div class="empty">Loading&#8230;</div></div>
</div>

</div><!-- /layout -->
</div><!-- /page-build -->

<!-- ═══════════════ PAGE: ENTRIES ═══════════════ -->
<div id="page-entries" class="page-panel">
<div class="layout">

<!-- left: controls + table -->
<div class="panel" style="grid-column:1/-1;border:none">

  <!-- auth notice (shown when no JWT and user tries to load) -->
  <div id="login-banner" style="display:none;background:#fff3cd;border:1px solid #ffc107;border-radius:6px;padding:8px 14px;margin-bottom:14px;font-size:13px;color:#856404">
    &#9888; Log in using the form in the header to fetch entries.
  </div>

  <!-- toolbar -->
  <div class="toolbar">
    <div class="field">
      <label>Project</label>
      <select id="e-project" onchange="onProjectChange()"><option value="">— select —</option></select>
    </div>
    <div class="field">
      <label>Form</label>
      <select id="e-form" onchange="onFormChange()"><option value="">— select —</option></select>
    </div>
    <div class="field">
      <label>Per page</label>
      <select id="e-perpage">
        <option value="25">25</option>
        <option value="50" selected>50</option>
        <option value="100">100</option>
        <option value="250">250</option>
      </select>
    </div>
    <button class="btn btn-primary btn-sm" onclick="fetchPage(1)">&#8635; Load</button>
    <button class="btn btn-green btn-sm"   onclick="downloadCSV()">&#8659; CSV</button>
    <span id="entries-status"></span>
  </div>

  <!-- table -->
  <div class="entry-table-wrap">
    <table id="entries-table">
      <thead id="entries-thead"><tr><th colspan="5" style="text-align:center;color:#57606a">Select a project and form, then click Load.</th></tr></thead>
      <tbody id="entries-tbody"></tbody>
    </table>
  </div>

  <!-- pagination -->
  <div class="pagination">
    <button class="btn btn-secondary btn-sm" id="btn-prev" onclick="fetchPage(currentPage-1)" disabled>&#8592; Prev</button>
    <button class="btn btn-secondary btn-sm" id="btn-next" onclick="fetchPage(currentPage+1)" disabled>Next &#8594;</button>
    <span id="page-info"></span>
  </div>

</div>
</div><!-- /layout -->
</div><!-- /page-entries -->

<!-- ═══════════════ PAGE: USERS ═══════════════ -->
<div id="page-users" class="page-panel">
<div class="layout">

<div class="panel">
  <h2>Add User</h2>
  <label>Display Name</label>
  <input type="text" id="u-name" placeholder="Alice">
  <label>Email *</label>
  <input type="text" id="u-email" placeholder="alice@example.com">
  <label>Password *</label>
  <input type="password" id="u-pass" placeholder="min 8 characters">
  <label>Confirm Password *</label>
  <input type="password" id="u-pass2" placeholder="repeat password">
  <div class="actions">
    <button class="btn btn-primary" onclick="submitUser()">&#10003; Create User</button>
    <button class="btn btn-secondary" onclick="resetUserForm()">&#8635; Reset</button>
  </div>
  <div id="user-result" style="margin-top:12px;padding:10px 12px;border-radius:6px;font-size:13px;display:none"></div>
</div>

<div class="panel">
  <h2>Quick Reference</h2>
  <p style="font-size:13px;color:#57606a;line-height:1.7">
    Users created here can log in to the mobile app using <strong>local</strong> authentication.<br><br>
    The server also supports:
  </p>
  <ul style="font-size:13px;color:#57606a;margin:10px 0 0 18px;line-height:2">
    <li><strong>Passwordless</strong> — email + one-time code (code printed to server stdout)</li>
    <li><strong>Google OAuth</strong> — stub, auto-creates user from OAuth code</li>
    <li><strong>Apple Sign-In</strong> — stub, auto-creates user from identity token</li>
  </ul>
  <div style="margin-top:20px;padding:12px;background:#f7f8fa;border-radius:6px;font-size:12px;font-family:monospace;color:#57606a">
    POST /api/admin/register<br>
    {"email":"…","password":"…","name":"…"}
  </div>
</div>

</div><!-- /layout -->
</div><!-- /page-users -->

<script>
// ═══════════════════════════════════════════════════════════════════════════
// SHARED STATE
// ═══════════════════════════════════════════════════════════════════════════
let jwt = localStorage.getItem('ec5_admin_jwt') || '';
let allProjects = [];
let currentPage = 1;
let currentNextUrl = null;
let currentFormRef = '';
let currentSlug = '';
let allEntryRows = []; // for CSV
let allEntryColumns = [];

// ═══════════════════════════════════════════════════════════════════════════
// PAGE SWITCHING
// ═══════════════════════════════════════════════════════════════════════════
function showPage(id){
  document.querySelectorAll('.page-tab').forEach((t,i)=>t.classList.toggle('active',['build','entries','users'][i]===id));
  document.querySelectorAll('.page-panel').forEach(p=>p.classList.remove('active'));
  document.getElementById('page-'+id).classList.add('active');
  if(id==='entries') initEntriesPage();
}

// ═══════════════════════════════════════════════════════════════════════════
// BUILD TAB — inner tab switch
// ═══════════════════════════════════════════════════════════════════════════
function switchBuildTab(id){
  document.querySelectorAll('#page-build .tab').forEach((t,i)=>t.classList.toggle('active',['form','raw'][i]===id));
  document.querySelectorAll('#page-build .tab-panel').forEach(p=>p.classList.remove('active'));
  document.getElementById('btab-'+id).classList.add('active');
  if(id==='raw'){
    try{ document.getElementById('raw-preview').value=JSON.stringify(buildPayload().definition_json,null,2); }
    catch(e){ document.getElementById('raw-preview').value='// '+e.message; }
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// BUILD TAB — project builder
// ═══════════════════════════════════════════════════════════════════════════
let formCount=0, inputCounters={};
const INPUT_TYPES=['text','textarea','integer','decimal','phone','date','time','radio','checkbox','dropdown','location','photo','audio','video','barcode'];

function slugify(s){ return s.toLowerCase().trim().replace(/[^a-z0-9\s-]/g,'').replace(/\s+/g,'-').replace(/-+/g,'-'); }
function onNameChange(){
  const sl=document.getElementById('p-slug');
  if(!sl.dataset.manual) sl.value=slugify(document.getElementById('p-name').value);
}
document.getElementById('p-slug').addEventListener('input',function(){this.dataset.manual='1';});

function addForm(){
  formCount++;
  const fid=formCount;
  const el=document.createElement('div');
  el.className='form-block'; el.id='form-'+fid;
  el.innerHTML=`<div class="form-block-header"><strong>Form ${fid}</strong>
    <button class="btn btn-danger btn-sm" onclick="removeForm(${fid})">Remove</button></div>
    <label>Form Name *</label>
    <input type="text" id="fname-${fid}" placeholder="Registration Form">
    <div id="inputs-${fid}"></div>
    <button class="btn btn-secondary btn-sm" style="margin-top:7px" onclick="addInput(${fid})">+ Add Question</button>`;
  document.getElementById('forms-container').appendChild(el);
}
function removeForm(fid){ document.getElementById('form-'+fid).remove(); }

function addInput(fid){
  if(!inputCounters[fid]) inputCounters[fid]=0;
  inputCounters[fid]++;
  const iid=inputCounters[fid];
  const opts=INPUT_TYPES.map(t=>`<option value="${t}">${t}</option>`).join('');
  const el=document.createElement('div');
  el.className='input-block'; el.id=`input-${fid}-${iid}`;
  el.innerHTML=`<div class="input-block-header">
    <select id="itype-${fid}-${iid}" style="width:auto;border:1px solid #d0d7de;border-radius:4px;font-size:12px;padding:2px 6px">${opts}</select>
    <button class="remove-btn" onclick="removeInput(${fid},${iid})">&#215;</button></div>
    <label>Question *</label>
    <input type="text" id="iq-${fid}-${iid}" placeholder="Enter your question text">
    <div class="input-row" style="margin-top:5px">
      <label style="margin-top:0"><input type="checkbox" id="ititle-${fid}-${iid}"> Use as entry title</label>
      <label style="margin-top:0"><input type="checkbox" id="ireq-${fid}-${iid}"> Required</label>
    </div>`;
  document.getElementById('inputs-'+fid).appendChild(el);
}
function removeInput(fid,iid){ document.getElementById(`input-${fid}-${iid}`).remove(); }

function makeRef(){ return [...crypto.getRandomValues(new Uint8Array(16))].map(b=>b.toString(16).padStart(2,'0')).join(''); }

function buildPayload(){
  const name=document.getElementById('p-name').value.trim();
  const slug=document.getElementById('p-slug').value.trim();
  const desc=document.getElementById('p-desc').value.trim();
  const access=document.getElementById('p-access').value;
  const status=document.getElementById('p-status').value;
  if(!name||!slug) throw new Error('Project name and slug are required.');
  const projectRef=makeRef();
  const now=new Date().toISOString().replace('T',' ').substring(0,19);
  const rawForms=[],extraFormsMap={},extraInputsMap={},formRefsList=[];
  document.querySelectorAll('.form-block').forEach(fb=>{
    const fid=fb.id.replace('form-','');
    const fname=document.getElementById('fname-'+fid)?.value.trim()||'Form '+fid;
    const formRef=projectRef+'_form_'+fid;
    formRefsList.push(formRef);
    const rawInputs=[],inputRefsList=[];
    fb.querySelectorAll('.input-block').forEach(ib=>{
      const parts=ib.id.split('-'); const iid=parts[parts.length-1];
      const itype=document.getElementById(`itype-${fid}-${iid}`)?.value||'text';
      const iq=document.getElementById(`iq-${fid}-${iid}`)?.value.trim()||'Question';
      const ititle=document.getElementById(`ititle-${fid}-${iid}`)?.checked||false;
      const ireq=document.getElementById(`ireq-${fid}-${iid}`)?.checked||false;
      const inputRef=formRef+'_input_'+iid;
      inputRefsList.push(inputRef);
      const inputDef={ref:inputRef,type:itype,question:iq,is_title:ititle,is_required:ireq,
        uniqueness:'none',regex:null,default:null,min:null,max:null,possible_answers:[],jumps:[],branch:[],group:[]};
      rawInputs.push(inputDef);
      extraInputsMap[inputRef]={data:inputDef};
    });
    rawForms.push({ref:formRef,name:fname,type:'hierarchy',inputs:rawInputs});
    extraFormsMap[formRef]={details:{ref:formRef,name:fname,type:'hierarchy',has_location:false},
      inputs:inputRefsList,branch:{},group:{}};
  });
  const definitionJson={
    data:{project:{name,ref:projectRef,slug,access,status,forms:rawForms}},
    meta:{project_extra:{project:{details:{name,slug,ref:projectRef,access,status,
        small_description:desc,description:desc},forms:formRefsList},
      forms:extraFormsMap,inputs:extraInputsMap},
      project_mapping:[{name:'EC5_AUTO',is_default:true,forms:{}}],
      project_stats:{structure_last_updated:now}}
  };
  return {slug,name,ref:projectRef,structure_last_updated:now,definition_json:definitionJson};
}

async function submitProject(){
  document.getElementById('build-result').style.display='none';
  let payload;
  try{ payload=buildPayload(); }catch(e){ showBuildResult(false,e.message); return; }
  try{
    const r=await fetch('/api/admin/projects',{method:'POST',
      headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});
    const data=await r.json();
    if(r.ok){ showBuildResult(true,'Project "'+payload.name+'" saved (slug: '+payload.slug+')'); loadProjectList(); }
    else{ showBuildResult(false,JSON.stringify(data.errors)); }
  }catch(e){ showBuildResult(false,e.message); }
}
function showBuildResult(ok,msg){
  const el=document.getElementById('build-result');
  el.className=ok?'ok':'err'; el.textContent=msg; el.style.display='block';
}
function resetBuilder(){
  document.getElementById('p-name').value='';
  document.getElementById('p-slug').value='';
  delete document.getElementById('p-slug').dataset.manual;
  document.getElementById('p-desc').value='';
  document.getElementById('forms-container').innerHTML='';
  formCount=0; inputCounters={};
  document.getElementById('build-result').style.display='none';
}

async function loadProjectList(){
  const el=document.getElementById('project-list');
  el.innerHTML='<div class="empty">Loading&#8230;</div>';
  try{
    const r=await fetch('/api/admin/projects');
    const data=await r.json();
    allProjects=data.data||[];
    if(!allProjects.length){ el.innerHTML='<div class="empty">No projects yet.</div>'; return; }
    el.innerHTML=allProjects.map(p=>`
      <div class="project-card">
        <h3>${esc(p.name)}</h3>
        <div class="slug">${esc(p.slug)}</div>
        <div class="meta"><span class="badge badge-green">&#10003; active</span> &nbsp;${esc(p.structure_last_updated)}</div>
        <div class="meta" style="font-family:monospace;font-size:11px;color:#8b949e">ref: ${esc(p.ref)}</div>
        <div style="margin-top:7px">
          <a href="/project/${esc(p.slug)}/add-entry" target="_blank" style="font-size:12px;color:#3b82d4">&#8599; Open add-entry</a>
        </div>
      </div>`).join('');
  }catch(e){ el.innerHTML=`<div class="empty" style="color:#ef4444">Failed: ${esc(e.message)}</div>`; }
}

// ═══════════════════════════════════════════════════════════════════════════
// ENTRIES PAGE
// ═══════════════════════════════════════════════════════════════════════════
function updateJwtHeader(){
  const loggedout = document.getElementById('hdr-loggedout');
  const loggedin  = document.getElementById('hdr-loggedin');
  const status    = document.getElementById('hdr-status');
  if(jwt){
    loggedout.style.display = 'none';
    loggedin.style.display  = 'flex';
    // decode email from JWT payload (middle segment)
    try{
      const payload = JSON.parse(atob(jwt.split('.')[1].replace(/-/g,'+').replace(/_/g,'/')));
      status.textContent = '\u2713 ' + (payload.sub || 'logged in');
    } catch(_){ status.textContent = '\u2713 Logged in'; }
  } else {
    loggedout.style.display = 'flex';
    loggedin.style.display  = 'none';
  }
}

async function doLogin(){
  const email = document.getElementById('hdr-email').value.trim();
  const pass  = document.getElementById('hdr-pass').value;
  const errEl = document.getElementById('hdr-err');
  errEl.textContent = '';
  try{
    const r = await fetch('/api/login/local', {
      method: 'POST',
      headers: {'Content-Type':'application/json'},
      body: JSON.stringify({username: email, password: pass})
    });
    const data = await r.json();
    if(!r.ok) throw new Error(data.errors?.[0]?.title || 'Login failed');
    jwt = data.data?.jwt || data.data?.token || '';
    if(!jwt) throw new Error('No token in response');
    localStorage.setItem('ec5_admin_jwt', jwt);
    document.getElementById('hdr-pass').value = '';
    updateJwtHeader();
  } catch(e){
    errEl.textContent = e.message;
  }
}

function doLogout(){
  jwt = '';
  localStorage.removeItem('ec5_admin_jwt');
  updateJwtHeader();
}

async function initEntriesPage(){
  updateJwtHeader();
  // populate project dropdown
  if(!allProjects.length){
    const r=await fetch('/api/admin/projects');
    const data=await r.json();
    allProjects=data.data||[];
  }
  const sel=document.getElementById('e-project');
  sel.innerHTML='<option value="">— select —</option>'
    +allProjects.map(p=>`<option value="${esc(p.slug)}">${esc(p.name)}</option>`).join('');
}

async function onProjectChange(){
  const slug=document.getElementById('e-project').value;
  const fsel=document.getElementById('e-form');
  fsel.innerHTML='<option value="">— select —</option>';
  document.getElementById('entries-status').textContent='';
  clearTable();
  if(!slug) return;
  // /api/pwa/project-debug/{slug} returns definition_json verbatim.
  // definition_json.data.project.forms is an array of form objects
  // each with { ref, name, ... }.
  try{
    const r=await fetch('/api/pwa/project-debug/'+encodeURIComponent(slug));
    const data=await r.json();
    // data IS definition_json: { data: { project: { forms: [...] } }, meta: {...} }
    const forms=data.data?.project?.forms||[];
    fsel.innerHTML='<option value="">— select —</option>'
      +forms.map(f=>`<option value="${esc(f.ref)}">${esc(f.name)}</option>`).join('');
    if(!forms.length) document.getElementById('entries-status').textContent='No forms found in this project.';
  }catch(e){
    document.getElementById('entries-status').textContent='Could not load forms: '+e.message;
  }
}

function onFormChange(){
  clearTable();
  document.getElementById('entries-status').textContent='';
}

function clearTable(){
  document.getElementById('entries-thead').innerHTML='<tr><th colspan="5" style="text-align:center;color:#57606a">Select a project and form, then click Load.</th></tr>';
  document.getElementById('entries-tbody').innerHTML='';
  document.getElementById('page-info').textContent='';
  document.getElementById('btn-prev').disabled=true;
  document.getElementById('btn-next').disabled=true;
  allEntryRows=[]; allEntryColumns=[];
  currentPage=1; currentNextUrl=null;
}

async function fetchPage(page){
  currentSlug=document.getElementById('e-project').value;
  currentFormRef=document.getElementById('e-form').value;
  const perPage=parseInt(document.getElementById('e-perpage').value)||50;
  if(!currentSlug||!currentFormRef){
    document.getElementById('entries-status').textContent='Select a project and form first.';
    return;
  }
  document.getElementById('entries-status').textContent='Loading&#8230;';
  const headers={'Content-Type':'application/json'};
  if(jwt) headers['Authorization']='Bearer '+jwt;
  try{
    const url=`/api/entries/${encodeURIComponent(currentSlug)}`
      +`?form_ref=${encodeURIComponent(currentFormRef)}&per_page=${perPage}&page=${page}`;
    const r=await fetch(url,{headers});
    if(r.status===401){
      // JWT missing or expired — clear it and show the header login form
      jwt=''; localStorage.removeItem('ec5_admin_jwt');
      updateJwtHeader();
      document.getElementById('login-banner').style.display='block';
      document.getElementById('entries-status').textContent='Session expired. Please log in again.';
      return;
    }
    const data=await r.json();
    const entries=data.data?.entries||[];
    const total=data.meta?.total??entries.length;
    currentNextUrl=data.links?.next||null;
    currentPage=page;

    // build column list from all answers keys
    const answerCols=new Set();
    entries.forEach(e=>Object.keys(e.entry?.answers||{}).forEach(k=>answerCols.add(k)));
    allEntryColumns=['uuid','title','created_at','platform',...answerCols];
    allEntryRows=entries;

    renderTable(entries,allEntryColumns);

    const shown=(page-1)*perPage+entries.length;
    document.getElementById('entries-status').textContent=`${shown} / ${total} entries`;
    document.getElementById('page-info').textContent=`Page ${page}`;
    document.getElementById('btn-prev').disabled=(page<=1);
    document.getElementById('btn-next').disabled=!currentNextUrl;
  }catch(e){
    document.getElementById('entries-status').textContent='Error: '+e.message;
  }
}

function renderTable(entries,cols){
  const thead=document.getElementById('entries-thead');
  const tbody=document.getElementById('entries-tbody');
  thead.innerHTML='<tr>'+cols.map(c=>`<th title="${esc(c)}">${esc(shortCol(c))}</th>`).join('')+'</tr>';
  if(!entries.length){ tbody.innerHTML=`<tr><td colspan="${cols.length}" style="text-align:center;color:#57606a;padding:20px">No entries found.</td></tr>`; return; }
  tbody.innerHTML=entries.map(e=>{
    const en=e.entry||{};
    const answers=en.answers||{};
    return '<tr>'+cols.map(c=>{
      let v='';
      if(c==='uuid')      v=en.entry_uuid||e.id||'';
      else if(c==='title')     v=en.title||'';
      else if(c==='created_at')v=en.created_at||'';
      else if(c==='platform')  v=en.platform||'';
      else{ const a=answers[c]; v=a!=null?(typeof a==='object'?JSON.stringify(a):String(a)):''; }
      return `<td title="${esc(v)}">${esc(v)}</td>`;
    }).join('')+'</tr>';
  }).join('');
}

function shortCol(c){
  // shorten long ref strings like "aabb...._form_1_input_3" → "input_3"
  const m=c.match(/input_\d+$/);
  return m?m[0]:c;
}

function downloadCSV(){
  if(!allEntryRows.length){ alert('Load entries first.'); return; }
  const cols=allEntryColumns;
  const rows=allEntryRows.map(e=>{
    const en=e.entry||{};
    const answers=en.answers||{};
    return cols.map(c=>{
      let v='';
      if(c==='uuid')       v=en.entry_uuid||e.id||'';
      else if(c==='title')      v=en.title||'';
      else if(c==='created_at') v=en.created_at||'';
      else if(c==='platform')   v=en.platform||'';
      else{ const a=answers[c]; v=a!=null?(typeof a==='object'?JSON.stringify(a):String(a)):''; }
      return '"'+String(v).replace(/"/g,'""')+'"';
    }).join(',');
  });
  const csv=[cols.map(c=>'"'+c+'"').join(','),...rows].join('\r\n');
  const blob=new Blob([csv],{type:'text/csv'});
  const a=document.createElement('a');
  a.href=URL.createObjectURL(blob);
  a.download=`${currentSlug}_${currentFormRef}_p${currentPage}.csv`;
  a.click();
}

// ═══════════════════════════════════════════════════════════════════════════
// USERS PAGE
// ═══════════════════════════════════════════════════════════════════════════
async function submitUser(){
  const name  = document.getElementById('u-name').value.trim();
  const email = document.getElementById('u-email').value.trim();
  const pass  = document.getElementById('u-pass').value;
  const pass2 = document.getElementById('u-pass2').value;
  const res   = document.getElementById('user-result');
  res.style.display = 'none';

  if(!email){ showUserResult(false,'Email is required.'); return; }
  if(!pass)  { showUserResult(false,'Password is required.'); return; }
  if(pass.length < 8){ showUserResult(false,'Password must be at least 8 characters.'); return; }
  if(pass !== pass2) { showUserResult(false,'Passwords do not match.'); return; }

  try{
    const r = await fetch('/api/admin/register', {
      method: 'POST',
      headers: {'Content-Type':'application/json'},
      body: JSON.stringify({email, password: pass, name: name || email})
    });
    const data = await r.json();
    if(!r.ok) throw new Error(data.errors?.[0]?.title || 'Registration failed');
    showUserResult(true, `User created — id: ${data.data.id}, email: ${data.data.email}`);
    resetUserForm();
  } catch(e){
    showUserResult(false, e.message);
  }
}

function showUserResult(ok, msg){
  const el = document.getElementById('user-result');
  el.style.cssText += ';display:block';
  el.className = '';
  el.style.background  = ok ? '#dcfce7' : '#fee2e2';
  el.style.color       = ok ? '#166534' : '#991b1b';
  el.style.border      = ok ? '1px solid #bbf7d0' : '1px solid #fecaca';
  el.textContent = msg;
}

function resetUserForm(){
  ['u-name','u-email','u-pass','u-pass2'].forEach(id => document.getElementById(id).value='');
}

// ═══════════════════════════════════════════════════════════════════════════
// UTILS
// ═══════════════════════════════════════════════════════════════════════════
function esc(s){ const d=document.createElement('div'); d.textContent=String(s??''); return d.innerHTML; }

// ── init ─────────────────────────────────────────────────────────────────────
addForm();
loadProjectList();
updateJwtHeader();
</script>
</body>
</html>)HTML";

// ---------------------------------------------------------------------------
// Handler helpers
// ---------------------------------------------------------------------------

namespace
{

Response htmlResponse(int version, const std::string& body)
{
    Response res{http::status::ok, version};
    res.set(http::field::content_type, "text/html; charset=utf-8");
    ec5::addCorsHeaders(res);
    res.keep_alive(false);
    res.body() = body;
    res.prepare_payload();
    return res;
}

} // namespace

// ---------------------------------------------------------------------------
// Handler: GET /admin
// ---------------------------------------------------------------------------
static net::awaitable<Response> adminUiHandler(Request& req,
                                               const http_function& /*params*/)
{
    co_return htmlResponse(req.version(), ADMIN_HTML);
}

// ---------------------------------------------------------------------------
// Route registration
// ---------------------------------------------------------------------------

void register_admin_routes(reactor::HttpRouter& router, ec5::Database& /*db*/)
{
    router.add_get_handler(
        "/admin",
        [](Request& req, const http_function& params) -> net::awaitable<Response> {
            return adminUiHandler(req, params);
        });

    std::cout << "[epicollect5_server] Admin UI registered at /admin\n";
}

} // namespace ec5
