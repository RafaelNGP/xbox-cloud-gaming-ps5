// SPDX-License-Identifier: MIT
// Reddit Feedback Timeline Extractor - Standalone Bookmarklet
//
// HOW TO USE:
// 1. Create a new bookmark in your browser (Chrome, Brave, Edge, Firefox).
// 2. Name it: "Extract Reddit Timeline"
// 3. In the URL field, paste the minified code from the section below (starting with `javascript:`).
// 4. Open any Reddit post thread and click the bookmark!

javascript:(function(){
  if(window.__redditFeedbackExtractorLoaded && document.getElementById('reddit-feedback-trigger-btn')){
    document.getElementById('reddit-feedback-trigger-btn').click();
    return;
  }
  
  // Inject CSS
  const css = `
    #reddit-feedback-trigger-btn { position: fixed; bottom: 24px; right: 24px; z-index: 999999; background: linear-gradient(135deg, #107c10 0%, #0e600e 100%); color: #fff; border: 1px solid #1f9a1f; border-radius: 28px; padding: 10px 18px; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; font-size: 14px; font-weight: 600; box-shadow: 0 4px 16px rgba(0,0,0,0.45); cursor: pointer; display: flex; align-items: center; gap: 8px; }
    #reddit-feedback-trigger-btn:hover { transform: translateY(-2px); box-shadow: 0 6px 20px rgba(16,124,16,0.5); }
    #reddit-feedback-overlay { position: fixed; top: 0; left: 0; width: 100vw; height: 100vh; background: rgba(0,0,0,0.75); backdrop-filter: blur(6px); z-index: 9999999; display: flex; align-items: center; justify-content: center; opacity: 0; pointer-events: none; transition: opacity 0.2s ease; }
    #reddit-feedback-overlay.active { opacity: 1; pointer-events: auto; }
    #reddit-feedback-modal { width: 90vw; max-width: 960px; height: 85vh; background: #121316; border: 1px solid #2d3139; border-radius: 14px; box-shadow: 0 12px 48px rgba(0,0,0,0.8); display: flex; flex-direction: column; overflow: hidden; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; color: #e1e4ea; }
    .rf-header { padding: 16px 24px; border-bottom: 1px solid #242730; display: flex; justify-content: space-between; align-items: center; background: #181a1f; }
    .rf-title { font-size: 18px; font-weight: 700; color: #fff; display: flex; align-items: center; gap: 10px; }
    .rf-title-badge { background: #107c10; color: #fff; padding: 2px 8px; border-radius: 12px; font-size: 12px; }
    .rf-close-btn { background: transparent; border: none; color: #8c93a0; font-size: 22px; cursor: pointer; }
    .rf-stats-bar { display: flex; gap: 16px; padding: 12px 24px; background: #14161b; border-bottom: 1px solid #20232b; font-size: 13px; }
    .rf-stat-item { color: #9ba1b0; }
    .rf-stat-val { font-weight: 700; color: #fff; }
    .rf-stat-crash { color: #ff5252; }
    .rf-stat-working { color: #4caf50; }
    .rf-filters-bar { display: flex; gap: 8px; padding: 10px 24px; background: #121316; border-bottom: 1px solid #20232b; align-items: center; }
    .rf-filter-pill { background: #1c1f26; border: 1px solid #2c303a; color: #a4abb9; padding: 5px 12px; border-radius: 16px; font-size: 12px; cursor: pointer; }
    .rf-filter-pill.active { border-color: #107c10; background: #143516; color: #a7e0a7; }
    .rf-content-area { flex: 1; overflow-y: auto; padding: 20px 24px; display: flex; flex-direction: column; gap: 12px; }
    .rf-comment-card { background: #16181e; border: 1px solid #232731; border-radius: 10px; padding: 14px 16px; display: flex; flex-direction: column; gap: 8px; }
    .rf-comment-card.is-op { border-left: 4px solid #107c10; background: #131914; }
    .rf-comment-card.has-crash { border-left: 4px solid #ff5252; background: #1c1416; }
    .rf-comment-card.has-working { border-left: 4px solid #4caf50; }
    .rf-meta-row { display: flex; align-items: center; gap: 10px; font-size: 12px; flex-wrap: wrap; }
    .rf-author { font-weight: 700; color: #e6edf3; }
    .rf-author.is-op { color: #49c449; }
    .rf-badge-op { background: #107c10; color: white; padding: 1px 6px; border-radius: 8px; font-size: 10px; }
    .rf-delta { background: #20242e; color: #8fa0ba; padding: 2px 7px; border-radius: 10px; font-size: 11px; }
    .rf-time { color: #7d8590; }
    .rf-tag { padding: 2px 7px; border-radius: 10px; font-size: 11px; font-weight: 600; }
    .rf-tag-crash { background: rgba(255,82,82,0.2); color: #ff7575; border: 1px solid rgba(255,82,82,0.4); }
    .rf-tag-ps4 { background: rgba(0,112,209,0.2); color: #58a6ff; border: 1px solid rgba(0,112,209,0.4); }
    .rf-tag-ps5 { background: rgba(142,68,173,0.2); color: #d2a8ff; border: 1px solid rgba(142,68,173,0.4); }
    .rf-tag-working { background: rgba(76,175,80,0.2); color: #7ee787; border: 1px solid rgba(76,175,80,0.4); }
    .rf-tag-logs { background: rgba(255,193,7,0.2); color: #f2cc60; border: 1px solid rgba(255,193,7,0.4); }
    .rf-comment-body { font-size: 13.5px; line-height: 1.55; color: #c9d1d9; white-space: pre-wrap; word-break: break-word; }
    .rf-reply-info { font-size: 11px; color: #8b949e; font-style: italic; }
    .rf-actions-bar { padding: 14px 24px; border-top: 1px solid #20232b; background: #14161b; display: flex; justify-content: space-between; align-items: center; }
    .rf-btn-group { display: flex; gap: 10px; }
    .rf-btn { padding: 8px 16px; border-radius: 8px; font-size: 13px; font-weight: 600; cursor: pointer; border: 1px solid transparent; display: flex; align-items: center; gap: 6px; }
    .rf-btn-primary { background: #107c10; color: #fff; border-color: #1a9c1a; }
    .rf-btn-primary:hover { background: #139913; }
    .rf-btn-secondary { background: #21262d; color: #c9d1d9; border-color: #30363d; }
    .rf-toast { position: fixed; bottom: 80px; right: 24px; background: #107c10; color: white; padding: 10px 18px; border-radius: 8px; font-size: 13px; font-weight: 600; z-index: 10000000; opacity: 0; transition: all 0.2s; pointer-events: none; }
    .rf-toast.show { opacity: 1; }
  `;
  const styleEl = document.createElement('style');
  styleEl.textContent = css;
  document.head.appendChild(styleEl);

  let postData = null, commentsList = [], currentFilter = 'all', sortOrder = 'asc';

  function formatTime(sec){
    const d = new Date(sec * 1000);
    const p = n => String(n).padStart(2,'0');
    return `${d.getFullYear()}-${p(d.getMonth()+1)}-${p(d.getDate())} ${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}`;
  }

  function formatDelta(cSec, pSec){
    const diff = cSec - pSec;
    if(diff < 0) return 'before post';
    const m = Math.floor(diff/60);
    if(m < 60) return `+${m}m`;
    const h = Math.floor(m/60), remM = m%60;
    if(h < 24) return `+${h}h ${remM}m`;
    const days = Math.floor(h/24), remH = h%24;
    return `+${days}d ${remH}h`;
  }

  function detectTags(text){
    const tags = [], t = text.toLowerCase();
    if(/ce-34878-0|crash|freeze|error|panic|closing|fechou|travou|erro|falha|blue screen|tela azul/.test(t))
      tags.push({ id:'crash', label:'🚨 Crash / Error', cls:'rf-tag-crash' });
    if(/ps4\b|ps4 pro|ps4 fat|slim\b|orbis|cusa/.test(t))
      tags.push({ id:'ps4', label:'🕹️ PS4', cls:'rf-tag-ps4' });
    if(/ps5\b|prospero|ppsa|vulkan/.test(t))
      tags.push({ id:'ps5', label:'🎮 PS5', cls:'rf-tag-ps5' });
    if(/11\.00|9\.00|13\.02|12\.50|goldhen|pppwn|hen\b/.test(t))
      tags.push({ id:'fw', label:'⚙️ Firmware', cls:'rf-tag-ps4' });
    if(/\b(working|works|worked|tested|smooth|running|funcionou|rodou|perfeito|rodando)\b/.test(t) && !/not working|doesn'?t work|não funcionou/.test(t))
      tags.push({ id:'working', label:'✅ Working', cls:'rf-tag-working' });
    if(/ftp\b|boot\.log|xcloud\.log|crash\.marker|\/data\/psbox/.test(t))
      tags.push({ id:'logs', label:'❓ Logs / Diag', cls:'rf-tag-logs' });
    return tags;
  }

  function flatten(nodeList, parentAuthor=null, depth=0){
    const out = [];
    if(!nodeList || !Array.isArray(nodeList)) return out;
    for(const item of nodeList){
      if(item.kind !== 't1' || !item.data) continue;
      const c = item.data, body = c.body || '';
      out.push({
        id: c.id, author: c.author || '[deleted]', isOp: !!c.is_submitter,
        createdUtc: c.created_utc || 0, score: c.score || 0, body,
        permalink: c.permalink ? 'https://reddit.com' + c.permalink : '',
        parentAuthor, depth, tags: detectTags(body)
      });
      if(c.replies && c.replies.data && c.replies.data.children)
        out.push(...flatten(c.replies.data.children, c.author, depth + 1));
    }
    return out;
  }

  function generateMarkdown(){
    if(!postData) return '';
    const sorted = [...commentsList].sort((a,b)=>a.createdUtc-b.createdUtc);
    const crashCount = sorted.filter(c => c.tags.some(t => t.id === 'crash')).length;
    const workCount = sorted.filter(c => c.tags.some(t => t.id === 'working')).length;

    let md = `# 📢 Reddit Feedback Timeline: ${postData.title}\n\n`;
    md += `- **Post**: [${postData.title}](${postData.permalink})\n`;
    md += `- **Subreddit**: ${postData.subreddit} | **Author**: u/${postData.author}\n`;
    md += `- **Date**: \`${formatTime(postData.createdUtc)}\` | **Score**: ${postData.score}\n`;
    md += `- **Comments**: ${sorted.length} (🚨 ${crashCount} Crashes | ✅ ${workCount} Working)\n\n---\n\n`;
    md += `## 🕒 Chronological Event Timeline\n\n`;

    for(const c of sorted){
      const timeStr = formatTime(c.createdUtc);
      const delta = formatDelta(c.createdUtc, postData.createdUtc);
      const tagStr = c.tags.length ? c.tags.map(t=>`\`${t.label}\``).join(' ') : '`💬 Feedback`';
      const opBadge = c.isOp ? ' **[OP / Dev]**' : '';
      const reply = c.parentAuthor ? ` *(replying to u/${c.parentAuthor})*` : '';

      md += `### [${delta}] \`${timeStr}\` — **u/${c.author}**${opBadge}${reply}\n`;
      md += `**Tags**: ${tagStr} | **Score**: ${c.score} | [Link](${c.permalink})\n\n`;
      md += c.body.split('\n').map(l=>`> ${l}`).join('\n') + '\n\n';
    }
    return md;
  }

  function showToast(msg){
    let t = document.getElementById('rf-toast');
    if(!t){
      t = document.createElement('div');
      t.id = 'rf-toast';
      t.className = 'rf-toast';
      document.body.appendChild(t);
    }
    t.textContent = msg;
    t.classList.add('show');
    setTimeout(()=>t.classList.remove('show'), 3500);
  }

  function download(content, name, type){
    const b = new Blob([content], {type});
    const u = URL.createObjectURL(b);
    const a = document.createElement('a');
    a.href = u; a.download = name;
    document.body.appendChild(a); a.click();
    setTimeout(()=>{ document.body.removeChild(a); URL.revokeObjectURL(u); }, 100);
  }

  function render(overlay){
    let list = [...commentsList];
    list.sort((a,b) => sortOrder === 'asc' ? a.createdUtc - b.createdUtc : b.createdUtc - a.createdUtc);
    if(currentFilter !== 'all'){
      list = currentFilter === 'op' ? list.filter(c=>c.isOp) : list.filter(c=>c.tags.some(t=>t.id===currentFilter));
    }
    const crashes = commentsList.filter(c => c.tags.some(t => t.id === 'crash')).length;
    const working = commentsList.filter(c => c.tags.some(t => t.id === 'working')).length;

    overlay.innerHTML = `
      <div id="reddit-feedback-modal">
        <div class="rf-header">
          <div class="rf-title"><span>📊 Feedback Timeline</span><span class="rf-title-badge">${postData.subreddit}</span></div>
          <button class="rf-close-btn" id="rf-close">&times;</button>
        </div>
        <div class="rf-stats-bar">
          <div class="rf-stat-item">Post: <span class="rf-stat-val">${formatTime(postData.createdUtc)}</span></div>
          <div class="rf-stat-item">Total: <span class="rf-stat-val">${commentsList.length}</span></div>
          <div class="rf-stat-item rf-stat-crash">🚨 Crashes: <span class="rf-stat-val rf-stat-crash">${crashes}</span></div>
          <div class="rf-stat-item rf-stat-working">✅ Working: <span class="rf-stat-val rf-stat-working">${working}</span></div>
        </div>
        <div class="rf-filters-bar">
          <button class="rf-filter-pill ${currentFilter==='all'?'active':''}" data-f="all">All (${commentsList.length})</button>
          <button class="rf-filter-pill ${currentFilter==='crash'?'active':''}" data-f="crash">🚨 Crashes (${crashes})</button>
          <button class="rf-filter-pill ${currentFilter==='working'?'active':''}" data-f="working">✅ Working (${working})</button>
          <button class="rf-filter-pill ${currentFilter==='ps4'?'active':''}" data-f="ps4">🕹️ PS4</button>
          <button class="rf-filter-pill ${currentFilter==='ps5'?'active':''}" data-f="ps5">🎮 PS5</button>
          <button class="rf-filter-pill ${currentFilter==='op'?'active':''}" data-f="op">👑 OP</button>
          <div style="flex:1;"></div>
          <button class="rf-filter-pill" id="rf-sort">${sortOrder==='asc'?'⏱️ Oldest First':'⏱️ Newest First'}</button>
        </div>
        <div class="rf-content-area">
          ${list.map(c => `
            <div class="rf-comment-card ${c.isOp?'is-op':''} ${c.tags.some(t=>t.id==='crash')?'has-crash':''} ${c.tags.some(t=>t.id==='working')?'has-working':''}">
              <div class="rf-meta-row">
                <span class="rf-author ${c.isOp?'is-op':''}">u/${c.author}</span>
                ${c.isOp?'<span class="rf-badge-op">OP</span>':''}
                ${c.parentAuthor?`<span class="rf-reply-info">↳ replying to u/${c.parentAuthor}</span>`:''}
                <span class="rf-delta">${formatDelta(c.createdUtc, postData.createdUtc)}</span>
                <span class="rf-time">${formatTime(c.createdUtc)}</span>
                ${c.tags.map(t=>`<span class="rf-tag ${t.cls}">${t.label}</span>`).join('')}
              </div>
              <div class="rf-comment-body">${c.body.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;')}</div>
            </div>
          `).join('')}
        </div>
        <div class="rf-actions-bar">
          <div style="font-size:12px;color:#8c93a0;">Showing ${list.length} comments</div>
          <div class="rf-btn-group">
            <button class="rf-btn rf-btn-secondary" id="rf-dl-json">💾 Download JSON</button>
            <button class="rf-btn rf-btn-secondary" id="rf-dl-md">💾 Download MD</button>
            <button class="rf-btn rf-btn-primary" id="rf-copy">📋 Copy for AI</button>
          </div>
        </div>
      </div>
    `;

    document.getElementById('rf-close').onclick = () => overlay.classList.remove('active');
    document.querySelectorAll('.rf-filter-pill[data-f]').forEach(b => b.onclick = (e) => {
      currentFilter = e.currentTarget.getAttribute('data-f');
      render(overlay);
    });
    document.getElementById('rf-sort').onclick = () => {
      sortOrder = sortOrder === 'asc' ? 'desc' : 'asc';
      render(overlay);
    };
    document.getElementById('rf-copy').onclick = async () => {
      await navigator.clipboard.writeText(generateMarkdown());
      showToast('📋 Timeline copied to clipboard! Paste directly to Antigravity.');
    };
    document.getElementById('rf-dl-md').onclick = () => {
      download(generateMarkdown(), 'feedback_timeline.md', 'text/markdown');
      showToast('💾 Downloaded markdown!');
    };
    document.getElementById('rf-dl-json').onclick = () => {
      download(JSON.stringify({ post: postData, comments: commentsList }, null, 2), 'feedback_timeline.json', 'application/json');
      showToast('💾 Downloaded JSON!');
    };
  }

  async function start(){
    const path = window.location.pathname.replace(/\/+$/, '');
    if(!path.includes('/comments/')) { alert('Please open a Reddit post thread first.'); return; }
    
    let overlay = document.getElementById('reddit-feedback-overlay');
    if(!overlay){
      overlay = document.createElement('div');
      overlay.id = 'reddit-feedback-overlay';
      document.body.appendChild(overlay);
      overlay.onclick = (e) => { if(e.target===overlay) overlay.classList.remove('active'); };
      document.onkeydown = (e) => { if(e.key==='Escape') overlay.classList.remove('active'); };
    }

    const resp = await fetch(path + '.json?raw_json=1&limit=500', { credentials: 'include' });
    const json = await resp.json();
    const p = json[0].data.children[0].data;
    postData = {
      title: p.title, author: p.author, subreddit: p.subreddit_name_prefixed,
      createdUtc: p.created_utc, permalink: 'https://reddit.com' + p.permalink,
      score: p.score
    };
    commentsList = flatten(json[1].data.children, postData.author, 0);
    render(overlay);
    overlay.classList.add('active');
  }

  start();
})();
