import { listen } from '@tauri-apps/api/event';
import { api, esc, icon, refreshIcons, toast, errText } from '../util.js';
import { t } from '../i18n.js';
import { confirmUpscalerFiles } from '../upscalerCheck.js';
import { checkText, dumpText, statusText, stepDetail, stepTitle, toolText, toolHint } from '../rusttext.js';

// one listener for the session; the page being shown sets the handler
let nupHandler = null;
listen('nup-progress', (ev) => nupHandler && nupHandler(ev.payload));

const MAX_LOG = 600;
const hex = (c) => '0x' + (c >>> 0).toString(16).toUpperCase().padStart(8, '0');
const nupPct = (p) => (p && p.total ? Math.min(100, Math.floor((100 * p.done) / p.total)) : 0);
const nupMsg = (p) =>
  p && p.total
    ? t(p.file ? 'setup.unpackingNupFile' : 'setup.unpackingNup', { pct: nupPct(p), file: p.file || '' })
    : t('setup.readingNup');

function checkLabel(label) {
  const text = checkText(label);
  return text === label ? (label === 'exefs/main is decrypted' || /^supported version \(/.test(label) ? text : null) : text;
}
const dumpError = (s) => dumpText(s);
const progressMsg = (m) => dumpText(m);
const statusMsg = (s) => statusText(s);

function chosenLabel(c) {
  if (!c) return '';
  if (c.kind === 'update') return t('common.update', { version: c.version });
  if (c.kind === 'base') return t('common.baseGame');
  return t('common.folder');
}

function gameStatusChip(s) {
  if (s.state === 'running') return `<span class="chip warn">${icon('loader-circle', 'spin')}${esc(t('play.running'))}</span>`;
  if (s.state === 'exited') {
    const code = s.code === null || s.code === undefined ? t('play.noExitCode') : t('play.codeWithHex', { code: s.code, hex: hex(s.code) });
    return s.crashed
      ? `<span class="chip danger">${icon('circle-alert')}${esc(t('play.crashed', { code }))}</span>`
      : `<span class="chip ok">${icon('circle-check')}${esc(t('play.exited', { code }))}</span>`;
  }
  return '';
}

function reportHtml(s) {
  if (s.state !== 'exited') return '';
  const files = s.files.filter((f) => s.crashed || f.name === 'fefw.log');
  return `
    <div class="card">
      <h2>${icon(s.crashed ? 'circle-alert' : 'file-text')}${esc(t(s.crashed ? 'play.reportCrashed' : 'play.reportClean'))}</h2>
      <p class="dim">${esc(statusMsg(s))}</p>
      ${files.length ? `<div class="row wrap" style="margin-top: var(--space-4)">${files
        .map((f) => `<button class="btn" data-open="${esc(f.name)}">${icon('file-text')}${esc(f.name)}${f.fresh && f.name !== 'fefw.log' ? esc(t('play.fileNew')) : ''}</button>`)
        .join('')}</div>` : ''}
    </div>`;
}

function candidateHtml(c, chosen) {
  const title = chosenLabel(c);
  const used = chosen && chosen.path === c.path;
  const unneeded = chosen && !used && !c.ok;
  const chip = used
    ? `<span class="chip ok">${icon('circle-check')}${esc(t('setup.willBeUsed'))}</span>`
    : c.ok ? `<span class="chip">${esc(t('setup.valid'))}</span>`
    : unneeded ? `<span class="chip">${esc(t('setup.notNeeded'))}</span>`
    : `<span class="chip danger">${icon('circle-x')}${esc(t('setup.notUsable'))}</span>`;
  return `
    <div class="candidate ${used ? 'chosen' : ''}">
      <div class="row between"><h3>${esc(title)}</h3>${chip}</div>
      <div class="path">${esc(c.path)}</div>
      ${unneeded ? '' : `<ul class="checks">${c.checks.map((k) => `<li class="${k.ok ? 'ok' : 'bad'}">${icon(k.ok ? 'circle-check' : 'circle-x')}${esc(checkLabel(k.label) || k.label)}</li>`).join('')}</ul>`}
    </div>`;
}

function toolsSectionHtml(tools) {
  const missing = tools.filter((tool) => !tool.ok);
  if (!missing.length) return '';
  const tail = missing.length === 1
    ? t('setup.toolsMissingOne')
    : t('setup.toolsMissingMany', { count: missing.length });
  return `
    <div class="card-section" id="tools-section">
      <h3>${esc(t('setup.toolsTitle'))}</h3>
      <p class="dim">${esc(t('setup.toolsIntro') + tail)}</p>
      <div class="tools">
        ${tools.map((tool) => `<span class="${tool.ok ? 'ok' : 'bad'}">${icon(tool.ok ? 'circle-check' : 'circle-x')} ${esc(toolText(tool.name))}</span><span class="${tool.ok ? 'faint' : 'dim'}">${esc(tool.ok ? tool.found : toolHint(tool.hint))}</span>`).join('')}
      </div>
    </div>`;
}

function stepsSectionHtml(s, { canHide = false } = {}) {
  const p = s.progress;
  const running = p.state === 'running';
  const pct = p.total ? Math.floor((100 * p.done) / p.total) : 0;
  const label = s.complete ? t('setup.checkAgain') : s.steps.some((x) => x.state === 'done') ? t('setup.continueSetup') : t('setup.setUp');
  const stepIcon = (x) => (x.state === 'done' ? 'circle-check' : x.state === 'running' ? 'loader-circle' : 'circle');
  return `
    <div class="card-section" id="setup-steps">
      <ul class="steps">${s.steps.map((x) => `<li class="${x.state}">${icon(stepIcon(x), x.state === 'running' ? 'spin' : '')}${esc(stepTitle(x))}${stepDetail(x) ? `<span class="step-detail">${esc(stepDetail(x))}</span>` : ''}</li>`).join('')}</ul>
      ${running ? `<progress max="100" ${p.total ? `value="${pct}"` : ''}></progress>
        <p class="faint">${esc((p.total ? t('setup.progressDone', { done: p.done, total: p.total }) : '') + (progressMsg(p.message) ? `${progressMsg(p.message)} — ` : '') + t('setup.unlockHint'))}</p>` : ''}
      ${p.state === 'error' ? `<div class="banner danger">${icon('circle-x')}<div style="white-space: pre-wrap">${esc(progressMsg(p.message))}</div></div>` : ''}
      ${p.state === 'cancelled' || p.state === 'done' ? `<p class="dim">${esc(progressMsg(p.message))}</p>` : ''}
      ${!running && !s.complete && p.state !== 'error' ? `<p class="faint">${esc(t('setup.firstRunHint', { jobs: s.jobs }))}</p>` : ''}
      <div class="row wrap">
        ${running
          ? `<button class="btn" id="setup-cancel">${icon('circle-x')}${esc(t('setup.pause'))}</button>`
          : `<button class="btn ${s.complete ? '' : 'primary'}" id="setup-run" ${s.cpu_problem ? 'disabled' : ''}>${icon('hammer')}${esc(label)}</button>`}
        <button class="btn quiet" id="open-log">${icon('file-text')}${esc(t('setup.openFullLog'))}</button>
        ${canHide && !running ? `<button class="btn quiet" id="hide-setup">${esc(t('setup.hide'))}</button>` : ''}
      </div>
    </div>`;
}

export async function render(el, ctx) {
  const visit = el.dataset.visit; // changes when the page is left (main.js)
  const isLinux = (await api('platform').catch(() => '')) === 'linux';
  let [cfg, ov] = await Promise.all([api('get_config'), api('get_overview')]);
  let path = cfg.dump_dir || '';
  let saved = path;
  let sendReports = !!cfg.send_reports;
  let setup = ov.repo_ok ? await api('setup_status') : null;
  let report = path && ov.repo_ok ? await api('validate_dump', { path }) : null;
  let log = setup ? setup.log.slice() : [];
  let status = await api('game_status');
  // Windows: little memory left for the game to take (memory.rs)
  let memory = await api('memory_warning').catch(() => null);
  // Linux: fefw.exe as a non-Steam game (steam.rs)
  let steam = await api('steam_status').catch(() => ({ available: false }));
  let addingSteam = false;
  // Linux handhelds: the Decky plugin, when it is not installed
  let decky = await api('decky_offer').catch(() => null);
  let nupBusy = false;
  let nupProgress = null;
  let showSetup = false;
  let logOpen = false;
  let guideOpen = false;
  let closeBrowseMenu = null;
  let pathSeq = 0;
  let saveInFlight = 0;

  function shutBrowseMenu() {
    if (closeBrowseMenu) closeBrowseMenu();
  }

  function memoryHtml() {
    if (!memory || status.state === 'running') return '';
    const key = memory.kind === 'noPageFile' ? 'play.memoryNoPageFile' : 'play.memoryLow';
    return `<div class="banner warn">${icon('triangle-alert')}<div>${esc(t(key, { free: memory.free_gb.toFixed(1) }))}</div></div>`;
  }

  async function checkMemory() {
    const now = await api('memory_warning').catch(() => null);
    if (el.dataset.visit !== visit || JSON.stringify(now) === JSON.stringify(memory)) return;
    memory = now;
    const box = el.querySelector('#memory');
    if (box) box.innerHTML = memoryHtml();
    refreshIcons();
  }

  function drawLog() {
    const box = el.querySelector('#log');
    if (!box) return;
    const atEnd = box.scrollTop + box.clientHeight >= box.scrollHeight - 8;
    box.textContent = log.join('\n');
    if (atEnd) box.scrollTop = box.scrollHeight;
  }

  async function importNup(picked) {
    const seq = ++pathSeq;
    nupBusy = true;
    nupProgress = null;
    draw();
    try {
      const unpacked = await api('import_nup', { path: picked });
      if (seq !== pathSeq || el.dataset.visit !== visit) return;
      nupBusy = false;
      await applyPath(unpacked, { force: true });
      return;
    } catch (e) {
      if (seq !== pathSeq || el.dataset.visit !== visit) return;
      toast(errText(e), 'error');
    }
    nupBusy = false;
    draw();
  }

  async function applyPath(next, { force = false } = {}) {
    const nextPath = (next || '').trim();
    if (/\.nup$/i.test(nextPath)) {
      await importNup(nextPath);
      return;
    }
    const seq = ++pathSeq;
    const nextReport = nextPath && ov.repo_ok
      ? await api('validate_dump', { path: nextPath }).catch((e) => ({ errors: [errText(e)], candidates: [], chosen: null }))
      : null;
    if (seq !== pathSeq || el.dataset.visit !== visit) return;
    path = nextPath;
    report = nextReport;
    const running = !!(setup && setup.progress.state === 'running');
    if (nextReport && nextReport.chosen && !running && (force || nextPath !== saved || saveInFlight > 0)) {
      saveInFlight++;
      try {
        const savedReport = await api('save_dump', { path: nextPath });
        if (seq !== pathSeq || el.dataset.visit !== visit) return;
        const canonical = (savedReport && savedReport.path) || nextReport.path || nextPath;
        saved = canonical;
        path = canonical;
        report = savedReport || nextReport;
        const [nextSetup, nextOv] = await Promise.all([api('setup_status'), api('get_overview')]);
        if (seq !== pathSeq || el.dataset.visit !== visit) return;
        setup = nextSetup;
        ov = nextOv;
        if (setup) log = setup.log.slice();
        toast(t(setup && setup.complete ? 'play.filesReady' : 'setup.filesSaved'), 'ok');
      } catch (e) {
        if (seq === pathSeq && el.dataset.visit === visit) toast(errText(e), 'error');
      } finally {
        saveInFlight--;
      }
    }
    if (seq === pathSeq && el.dataset.visit === visit) draw();
  }

  function openBrowseMenu(button, input) {
    const menu = document.createElement('div');
    menu.className = 'pop-menu';
    menu.setAttribute('role', 'menu');
    menu.innerHTML = `
      <button type="button" class="nav-item" role="menuitem" data-pick="folder">${icon('folder-open')}<span>${esc(t('setup.browseFolder'))}</span></button>
      <button type="button" class="nav-item" role="menuitem" data-pick="nup">${icon('file-archive')}<span>${esc(t('setup.browseNup'))}</span></button>`;
    document.body.append(menu);
    const r = button.getBoundingClientRect();
    const mw = menu.offsetWidth;
    const mh = menu.offsetHeight;
    const left = Math.max(8, Math.min(r.right - mw, window.innerWidth - mw - 8));
    menu.style.left = `${left}px`;
    if (r.bottom + 4 + mh > window.innerHeight - 8) {
      menu.style.bottom = `${window.innerHeight - r.top + 4}px`;
    } else {
      menu.style.top = `${r.bottom + 4}px`;
    }
    button.setAttribute('aria-expanded', 'true');
    refreshIcons();

    const outside = (ev) => {
      if (!menu.contains(ev.target) && !button.contains(ev.target)) shutBrowseMenu();
    };
    const escape = (ev) => {
      if (ev.key !== 'Escape') return;
      shutBrowseMenu();
      button.focus();
    };
    closeBrowseMenu = () => {
      menu.remove();
      button.setAttribute('aria-expanded', 'false');
      document.removeEventListener('pointerdown', outside);
      document.removeEventListener('keydown', escape);
      closeBrowseMenu = null;
    };
    document.addEventListener('pointerdown', outside);
    document.addEventListener('keydown', escape);

    menu.querySelector('[data-pick="folder"]').addEventListener('click', async () => {
      shutBrowseMenu();
      const picked = await api('pick_folder', { title: t('setup.chooseDumpTitle'), start: input.value || null });
      if (picked) await applyPath(picked);
    });
    menu.querySelector('[data-pick="nup"]').addEventListener('click', async () => {
      shutBrowseMenu();
      const picked = await api('pick_nup', { start: null });
      if (picked) await importNup(picked);
    });
    menu.firstElementChild.focus();
  }

  function bindSteps() {
    el.querySelector('#setup-run')?.addEventListener('click', async () => {
      try {
        await api('start_setup');
        setup = await api('setup_status');
        draw();
      } catch (e) {
        toast(errText(e), 'error');
      }
    });
    el.querySelector('#setup-cancel')?.addEventListener('click', () => api('cancel_setup'));
    el.querySelector('#open-log')?.addEventListener('click', () => api('open_setup_log').catch((e) => toast(errText(e), 'error')));
    el.querySelector('#hide-setup')?.addEventListener('click', () => {
      showSetup = false;
      draw();
    });
  }

  function setupCardHtml({ chosen, missingTools, setupRunning, ready }) {
    const dumpHasErrors = !!(report && (!chosen || (report.errors && report.errors.length)));
    let chipHtml;
    if (!path) {
      chipHtml = `<span class="chip warn">${icon('folder-search')}${esc(t('setup.chipChooseFiles'))}</span>`;
    } else if (!chosen) {
      chipHtml = `<span class="chip danger">${icon('circle-x')}${esc(t('setup.chipDumpInvalid'))}</span>`;
    } else if (missingTools.length) {
      chipHtml = `<span class="chip danger">${icon('circle-x')}${esc(t(missingTools.length === 1 ? 'setup.chipToolsMissingOne' : 'setup.chipToolsMissingMany', { count: missingTools.length }))}</span>`;
    } else if (setup && !setup.package && setup.tools.length) {
      chipHtml = `<span class="chip ok">${icon('circle-check')}${esc(t('setup.chipDumpAndToolsReady', { count: setup.tools.length }))}</span>`;
    } else {
      chipHtml = `<span class="chip ok">${icon('circle-check')}${esc(t('setup.chipDumpReady'))}</span>`;
    }

    const dlcCount = report && report.dlc ? report.dlc.length : 0;
    const dlcNote = dlcCount ? ` · ${esc(t(dlcCount === 1 ? 'play.dlcShortOne' : 'play.dlcShortMany', { count: dlcCount }))}` : '';

    return `
      <section class="card stack" id="setup-card">
        <div class="card-head">
          <h2>${icon('hammer')}${esc(t('setup.cardTitle'))}</h2>
          ${chipHtml}
        </div>

        <div class="stack" style="gap: var(--space-2)">
          <div class="row">
            <input type="text" class="mono" id="dump-path" spellcheck="false" placeholder="${esc(t('setup.pathPlaceholder'))}" value="${esc(path)}" ${nupBusy || setupRunning ? 'disabled' : ''} />
            <button type="button" class="btn" id="browse" aria-haspopup="menu" aria-expanded="false" ${nupBusy || setupRunning ? 'disabled' : ''}>${icon('folder-open')}${esc(t('common.browse'))}</button>
          </div>
          ${nupBusy ? `<p class="dim" id="nup-msg">${esc(nupMsg(nupProgress))}</p><progress max="100" value="${nupPct(nupProgress)}"></progress>` : ''}
          ${!nupBusy && chosen && !dumpHasErrors ? `
            <p class="card-summary" style="margin-top: 0">
              ${esc(t('setup.usingPrefix'))} <strong>${esc(chosenLabel(chosen))}</strong> (<code class="path">${esc(chosen.path)}</code>)${dlcNote} · ${esc(t('setup.checksPassed'))}
            </p>` : ''}
          ${!nupBusy && !path ? `
            <p class="faint">${esc(t('setup.dumpHint'))}</p>
            <p class="faint">${esc(t('setup.nupHint'))}</p>
            ${setup && setup.package ? `<p class="faint">${esc(t('setup.packageComplete'))}</p>` : ''}` : ''}
        </div>

        ${dumpHasErrors ? `
          <div class="stack">
            ${report.errors.length ? `<div class="banner danger">${icon('circle-x')}<div>${report.errors.map((e) => esc(dumpError(e))).join('<br>')}</div></div>` : ''}
            ${report.candidates.length ? `<div class="stack">${report.candidates.map((c) => candidateHtml(c, report.chosen)).join('')}</div>` : ''}
          </div>` : ''}

        ${setup && setup.cpu_problem ? `<div class="banner danger" id="cpu-problem">${icon('circle-x')}<div><strong>${esc(t('setup.cpuProblem'))}</strong> ${esc(setup.cpu_problem)}</div></div>` : ''}
        ${setup && !setup.cpu_problem && setup.cpu_note ? `<div class="banner info" id="cpu-note">${icon('info')}<div><strong>${esc(t('setup.cpuCompat'))}</strong> ${esc(setup.cpu_note)}</div></div>` : ''}

        ${setup && !setup.package ? toolsSectionHtml(setup.tools) : ''}

        ${chosen && saved === path && setup ? stepsSectionHtml(setup, { canHide: ready }) : ''}

        ${(chosen && saved === path) || log.length ? `
          <details class="card-details flush" id="log-details" ${logOpen ? 'open' : ''}>
            <summary>${icon('terminal')}<span>${esc(t('setup.showLog'))}</span></summary>
            <div class="details-body"><pre class="logbox" id="log"></pre></div>
          </details>` : ''}
      </section>`;
  }

  function installCardHtml({ chosen, setupRunning, ready }) {
    const dlcCount = report && report.dlc ? report.dlc.length : 0;
    let chipHtml;
    if (setupRunning) {
      chipHtml = `<span class="chip warn">${icon('loader-circle', 'spin')}${esc(t('play.chipRebuilding'))}</span>`;
    } else if (ready) {
      chipHtml = `<span class="chip ok">${icon('circle-check')}${esc(t('play.chipReady', { version: chosenLabel(chosen) }))}</span>`;
    } else if (!chosen) {
      chipHtml = `<span class="chip">${esc(t('play.chipSetupNeeded'))}</span>`;
    } else {
      chipHtml = `<span class="chip warn">${esc(t('play.chipRebuildNeeded'))}</span>`;
    }

    const parts = [];
    if (dlcCount) parts.push(esc(t(dlcCount === 1 ? 'play.dlcShortOne' : 'play.dlcShortMany', { count: dlcCount })));
    if (ov.exe_exists) parts.push(`<code>fefw.exe</code> ${esc(t('common.built'))}`);
    parts.push(esc(t('play.runFolderSummary')));

    return `
      <section class="card" id="install-card">
        <div class="card-head">
          <h2>${icon('hard-drive')}${esc(t('play.install'))}</h2>
          ${chipHtml}
        </div>
        <p class="card-summary">${parts.join(' · ')}</p>
        <div class="row wrap" style="margin-top: var(--space-4)">
          <button type="button" class="btn" id="open-dir" ${ov.repo_ok ? '' : 'disabled'}>${icon('folder-open')}${esc(t('play.openRunFolder'))}</button>
          <button type="button" class="btn" id="open-mods" ${ov.repo_ok ? '' : 'disabled'}>${icon('package')}${esc(t('play.openModsFolder'))}</button>
          ${steam.available ? `<button type="button" class="btn" id="add-steam" title="${esc(t('play.addToSteamHint'))}" ${ov.exe_exists && !addingSteam ? '' : 'disabled'}>${icon(addingSteam ? 'loader-circle' : 'gamepad-2', addingSteam ? 'spin' : '')}${esc(t(steam.added ? 'play.updateSteam' : 'play.addToSteam'))}</button>` : ''}
          ${ready && ov.repo_ok ? `<button type="button" class="btn quiet" id="toggle-setup">${icon('hammer')}${esc(t(showSetup ? 'setup.hide' : 'play.rebuildOrChange'))}</button>` : ''}
        </div>
        <details class="card-details" id="guide-details" ${guideOpen ? 'open' : ''}>
          <summary>${icon('info')}<span>${esc(t('play.pathsAndMods'))}</span></summary>
          <div class="details-body">
            <dl class="kv">
              <dt>${esc(t('play.gameFiles'))}</dt>
              <dd>${chosen ? `${esc(chosenLabel(chosen))}<div class="path">${esc(chosen.path)}</div>` : `<span class="faint">${esc(t('common.notSet'))}</span>`}</dd>
              <dt>${esc(t('play.dlc'))}</dt>
              <dd>${dlcCount ? `${esc(t(dlcCount === 1 ? 'play.dlcFound' : 'play.dlcFoundMany', { count: dlcCount }))}<div class="path">${esc(report.dlc_dir)}</div>` : `<span class="faint">${esc(t('play.dlcNone'))}</span>`}</dd>
              <dt>${esc(t('play.exe'))}</dt>
              <dd>${ov.exe_exists ? esc(t('common.built')) : `<span class="faint">${esc(t('common.notBuiltYet'))}</span>`}<div class="path">${esc(ov.exe)}</div></dd>
              <dt>${esc(t('play.runFolder'))}</dt>
              <dd><span class="faint">${esc(t('play.runFolderContents'))}</span><div class="path">${esc(ov.game_dir)}</div></dd>
              <dt>${esc(t('play.mods'))}</dt>
              <dd><ul class="faint bullets">${['modsRomfs', 'modsExefs', 'modsOff', 'modsOrder', 'modsRebuild'].map((k) => `<li>${esc(t(`play.${k}`))}</li>`).join('')}</ul></dd>
            </dl>
          </div>
        </details>
      </section>`;
  }

  function draw() {
    shutBrowseMenu();
    const running = status.state === 'running';
    const chosen = report && report.chosen;
    const savedMatch = saved === path;
    const setupRunning = !!(setup && setup.progress.state === 'running');
    const missingTools = setup && !setup.package ? setup.tools.filter((x) => !x.ok) : [];
    const ready = !!(ov.repo_ok && chosen && savedMatch && setup && setup.complete && !setupRunning && !nupBusy && !setup.cpu_problem);
    const setupVisible = ov.repo_ok && (!ready || showSetup);
    const rebuildBanner = ov.repo_ok && chosen && savedMatch && setup && !setup.complete && ov.exe_exists && !setup.cpu_problem;

    el.innerHTML = `
      <div class="page">
        <div class="hero">
          <h1>${esc(t('play.title'))}</h1>
          <p class="dim">${esc(t('play.intro'))}</p>
        </div>

        ${!ov.repo_ok ? `<div class="banner danger">${icon('circle-x')}<div>${esc(t('setup.repoMissing'))}</div></div>` : ''}
        ${rebuildBanner ? `<div class="banner warn">${icon('hammer')}<div><strong>${esc(t('play.rebuildNeededTitle'))}</strong> ${esc(t(setupRunning ? 'play.rebuildRunningBody' : 'play.rebuildNeededBody'))}</div></div>` : ''}
        <div id="memory" style="display: contents">${memoryHtml()}</div>

        ${ready || running || status.state === 'exited' ? `
          <div class="row wrap">
            <button type="button" class="btn primary big" id="play" ${ready && !running ? '' : 'disabled'}>${icon('play')}${esc(t(running ? 'play.running' : 'play.title'))}</button>
            ${gameStatusChip(status)}
          </div>` : ''}

        ${reportHtml(status)}

        ${setupVisible ? setupCardHtml({ chosen, missingTools, setupRunning, ready }) : ''}

        ${decky ? `
          <section class="card" id="decky-offer">
            <h2>${icon('gamepad-2')}${esc(t('play.deckyTitle'))}</h2>
            <p class="dim">${esc(t('play.deckyText'))}</p>
            <p class="faint" style="margin-top: var(--space-2)">${esc(t('play.deckyHint'))}</p>
            ${decky.folder ? `<p class="faint" style="margin-top: var(--space-2)">${esc(t('play.deckyFolder'))}</p><div class="path">${esc(decky.folder)}</div>` : ''}
            <div class="row" style="margin-top: var(--space-4)">
              <button type="button" class="btn primary" id="decky-get">${icon('download')}${esc(t('play.deckyGet'))}</button>
              <button type="button" class="btn quiet" id="decky-dismiss">${esc(t('play.reportsDismiss'))}</button>
            </div>
          </section>` : ''}

        ${steam.available && steam.stale ? `<div class="banner warn">${icon('triangle-alert')}<div style="flex:1">${esc(t('play.steamStale'))}</div><button type="button" class="btn" id="update-steam" ${ov.exe_exists && !addingSteam ? '' : 'disabled'}>${icon('gamepad-2')}${esc(t('play.updateSteam'))}</button></div>` : ''}

        ${installCardHtml({ chosen, setupRunning, ready })}

        <section class="card" id="reports-card">
          <div class="card-head">
            <h2>${icon('activity')}${esc(t('play.reportsTitle'))}</h2>
            <label class="switch">
              <input type="checkbox" id="send-reports" ${sendReports ? 'checked' : ''} />
              <span class="switch-label">${esc(t('play.reportsToggle'))}</span>
            </label>
          </div>
          <p class="card-summary">${esc(t('setup.reportsHint'))}</p>
        </section>

        <p class="faint">${esc(t('play.footer'))}</p>
      </div>`;

    refreshIcons();
    drawLog();

    el.querySelector('#play')?.addEventListener('click', async (ev) => {
      const btn = ev.currentTarget;
      btn.disabled = true;
      const go = await confirmUpscalerFiles(isLinux);
      btn.disabled = false;
      if (!go) return;
      api('play').catch((e) => toast(errText(e), 'error'));
    });

    el.querySelector('#send-reports')?.addEventListener('change', async (ev) => {
      const next = !!ev.target.checked;
      try {
        await api('set_send_reports', { enabled: next });
        sendReports = next;
      } catch (e) {
        ev.target.checked = sendReports;
        toast(errText(e), 'error');
      }
    });

    el.querySelector('#decky-get')?.addEventListener('click', () =>
      api('open_decky_installer').then(() => toast(t('play.deckyOpened'), 'ok')).catch((e) => toast(errText(e), 'error')),
    );
    el.querySelector('#decky-dismiss')?.addEventListener('click', async () => {
      try {
        await api('decline_decky');
        decky = null;
        draw();
      } catch (e) {
        toast(errText(e), 'error');
      }
    });

    const input = el.querySelector('#dump-path');
    if (input) {
      input.addEventListener('change', () => applyPath(input.value));
      input.addEventListener('keydown', (e) => {
        if (e.key === 'Enter') applyPath(input.value);
      });
    }
    el.querySelector('#browse')?.addEventListener('click', (ev) => {
      if (closeBrowseMenu) shutBrowseMenu();
      else openBrowseMenu(ev.currentTarget, input);
    });

    el.querySelector('#log-details')?.addEventListener('toggle', (ev) => {
      logOpen = ev.currentTarget.open;
      if (logOpen) drawLog();
    });
    el.querySelector('#guide-details')?.addEventListener('toggle', (ev) => {
      guideOpen = ev.currentTarget.open;
    });

    el.querySelector('#toggle-setup')?.addEventListener('click', () => {
      showSetup = !showSetup;
      draw();
      if (showSetup) el.querySelector('#setup-card')?.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
    });

    el.querySelector('#open-dir')?.addEventListener('click', () => api('open_game_dir').catch((e) => toast(errText(e), 'error')));
    el.querySelector('#open-mods')?.addEventListener('click', () => api('open_mods_dir').catch((e) => toast(errText(e), 'error')));

    const addSteam = async () => {
      addingSteam = true;
      draw();
      try {
        toast(await api('add_to_steam'), 'ok');
      } catch (e) {
        toast(errText(e), 'error');
      }
      steam = await api('steam_status').catch(() => steam);
      addingSteam = false;
      draw();
    };
    el.querySelector('#add-steam')?.addEventListener('click', addSteam);
    el.querySelector('#update-steam')?.addEventListener('click', addSteam);
    el.querySelectorAll('[data-open]').forEach((b) =>
      b.addEventListener('click', () => api('open_game_file', { name: b.dataset.open }).catch((e) => toast(errText(e), 'error'))),
    );
    bindSteps();
  }

  nupHandler = (p) => {
    if (el.dataset.visit !== visit) return;
    nupProgress = p;
    const msg = el.querySelector('#nup-msg');
    if (msg) {
      msg.textContent = nupMsg(p);
      const bar = msg.nextElementSibling;
      if (bar) bar.value = nupPct(p);
    }
  };
  ctx.onLog((lines) => {
    log.push(...lines);
    if (log.length > MAX_LOG) log = log.slice(log.length - MAX_LOG);
    drawLog();
  });
  ctx.onSetup(async (p) => {
    if (p.state === 'running' && setup && setup.progress.state === 'running' && setup.progress.step === p.step) {
      setup = { ...setup, progress: p };
      const box = el.querySelector('#setup-steps');
      if (box) {
        box.outerHTML = stepsSectionHtml(setup, { canHide: false });
        refreshIcons();
        bindSteps();
        return;
      }
    } else {
      [setup, ov] = await Promise.all([api('setup_status'), api('get_overview')]);
      if (p.state === 'done' && setup && setup.complete) showSetup = false;
    }
    draw();
  });
  ctx.onStatus((s) => {
    status = s;
    draw();
    if (s.state === 'exited') checkMemory();
  });
  // programs closed or opened while the page is on show
  const memoryTimer = setInterval(() => {
    if (el.dataset.visit !== visit) {
      clearInterval(memoryTimer);
      shutBrowseMenu();
    } else if (status.state !== 'running') {
      checkMemory();
    }
  }, 5000);
  ctx.onLanguage(draw);
  draw();
}
