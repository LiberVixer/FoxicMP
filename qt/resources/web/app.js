'use strict';
const $ = id => document.getElementById(id);
const texts = {
  ru: {token:'Код доступа из окна плеера',connect:'Подключиться',previous:'Предыдущий',play:'Играть',pause:'Пауза',stop:'Стоп',next:'Следующий',position:'Позиция',volume:'Громкость',mute:'Звук вкл/выкл',rate:'Скорость',queue:'Очередь воспроизведения',footer:'Управление текущим окном. После перезапуска сервера подключитесь заново.',connected:'Подключено',auth:'Неверный код. Откройте актуальную ссылку в плеере.',offline:'Нет соединения с плеером.',rejected:'Команда не выполнена. Обновите очередь и проверьте источник.',enter:'Откройте ссылку из окна «Веб-управление» плеера или введите код.',Playing:'Воспроизведение',Paused:'Пауза',Stopped:'Остановлено',empty:'Очередь пуста',source:'Путь к файлу на компьютере плеера или URL',sourceNote:'Открыть заменяет проигрываемую очередь. В очередь добавляет источник без смены воспроизведения. Файл с телефона не загружается; локальные плейлисты и папки открывайте в самом плеере.',open:'Открыть и заменить очередь',enqueue:'В очередь',remove:'Удалить',removeLabel:'Удалить из очереди',pagePrevious:'Предыдущая страница',pageNext:'Следующая страница',items:'Записи'},
  en: {token:'Access code from the player window',connect:'Connect',previous:'Previous',play:'Play',pause:'Pause',stop:'Stop',next:'Next',position:'Position',volume:'Volume',mute:'Mute / unmute',rate:'Speed',queue:'Playback queue',footer:'Controls this player window. Reconnect after restarting the server.',connected:'Connected',auth:'Invalid code. Open the current link from the player.',offline:'Cannot connect to the player.',rejected:'Command failed. Refresh the queue and check the source.',enter:'Open the link from the player’s Web control window, or enter its code.',Playing:'Playing',Paused:'Paused',Stopped:'Stopped',empty:'Queue is empty',source:'File path on the player computer or URL',sourceNote:'Open replaces the playing queue. Enqueue adds a source without changing playback. Files are not uploaded from your phone; open local playlists and folders in the player itself.',open:'Open and replace queue',enqueue:'Enqueue',remove:'Remove',removeLabel:'Remove from queue',pagePrevious:'Previous page',pageNext:'Next page',items:'Items'}
};
let language = navigator.language.startsWith('ru') ? 'ru' : 'en';
let token = location.hash.slice(1), connected = false, busy = false, requestBusy = false;
let snapshot = null, offset = 0, queueSignature = '', revision = 0, commandError = false, previewUrl = '';
history.replaceState(null, '', location.pathname);
const t = key => texts[language][key] || key;
function translate() {
  document.documentElement.lang = language;
  document.querySelectorAll('[data-i18n]').forEach(n => n.textContent = t(n.dataset.i18n));
  $('language').textContent = language === 'ru' ? 'English' : 'Русский';
  queueSignature = '';
  if (snapshot) render(snapshot); else $('notice').textContent = t('enter');
}
$('language').onclick = () => { language = language === 'ru' ? 'en' : 'ru'; translate(); };
function clock(n) {
  if (!Number.isFinite(n) || n < 0) return '—';
  const s = Math.floor(n);
  return [Math.floor(s / 3600), Math.floor(s / 60) % 60, s % 60].map(v => String(v).padStart(2, '0')).join(':');
}
function controls() {
  const disabled = !connected || requestBusy;
  document.querySelectorAll('[data-command],#volume,#rate,#source').forEach(n => n.disabled = disabled);
  $('seek').disabled = disabled || !snapshot?.canSeek;
  document.querySelector('[data-command=next]').disabled = disabled || !snapshot?.canNext;
  document.querySelector('[data-command=previous]').disabled = disabled || !snapshot?.canPrevious;
  document.querySelectorAll('[data-queue-command]').forEach(n => n.disabled = disabled || !snapshot?.canEditQueue);
  $('openSource').disabled = disabled || !$('source').value.trim();
  $('enqueueSource').disabled = disabled || !snapshot?.canEnqueue || !$('source').value.trim();
  $('queuePrevious').disabled = disabled || !snapshot || snapshot.queueOffset === 0;
  $('queueNext').disabled = disabled || !snapshot || snapshot.queueOffset + snapshot.queue.length >= snapshot.queueTotal;
}
function setConnected(on) { connected = on; $('login').hidden = on; controls(); }
async function api(path, body) {
  const options = {headers:{Authorization:'Bearer ' + token},cache:'no-store',signal:AbortSignal.timeout(4000)};
  if (body) { options.method = 'POST'; options.headers['Content-Type'] = 'application/json'; options.body = JSON.stringify(body); }
  const r = await fetch(path, options);
  if (r.status === 403) { setConnected(false); token = ''; $('notice').textContent = t('auth'); throw Error('auth'); }
  if (!r.ok) throw Error('rejected');
  return r.json();
}
function render(s) {
  snapshot = s;
  $('title').textContent = s.title || 'FoxicMP Qt';
  $('state').textContent = t(s.status);
  $('clock').textContent = clock(s.position) + ' / ' + (s.duration > 0 ? clock(s.duration) : '—');
  const seek = $('seek');
  if (document.activeElement !== seek) { seek.max = Math.max(1, s.duration); seek.value = s.position; }
  if (document.activeElement !== $('volume')) $('volume').value = s.volume;
  $('volumeValue').textContent = Math.round(s.volume) + '%';
  if (document.activeElement !== $('rate')) $('rate').value = String(s.rate);
  const signature = JSON.stringify([s.queueList, s.queueOffset, s.queue]);
  if (signature !== queueSignature) {
    const queue = $('queue');
    queue.replaceChildren(); queue.start = s.queueOffset + 1;
    for (const [row, item] of (s.queue || []).entries()) {
      const li = document.createElement('li');
      const target = {list:s.queueList, id:item.id};
      const play = document.createElement('button');
      play.textContent = `${s.queueOffset + row + 1}. ${item.title}`; play.className = 'queuePlay'; play.dataset.queueCommand = 'queuePlay';
      play.setAttribute('aria-label', t('play') + ': ' + item.title);
      play.onclick = () => command('queuePlay', target);
      const remove = document.createElement('button');
      remove.textContent = t('remove'); remove.dataset.queueCommand = 'queueRemove';
      remove.setAttribute('aria-label', t('removeLabel') + ': ' + item.title);
      remove.onclick = () => command('queueRemove', target);
      li.append(play, remove);
      if (item.current) { li.className = 'current'; play.setAttribute('aria-current', 'true'); }
      queue.append(li);
    }
    queueSignature = signature;
  }
  $('queueNote').textContent = s.queueTotal ? `${t('items')}: ${s.queueOffset + 1}–${s.queueOffset + s.queue.length} / ${s.queueTotal}` : t('empty');
  controls();
}
async function refreshPreview() {
  if (!token || !connected) return;
  try {
    const response = await fetch('/api/snapshot.jpg', {headers:{Authorization:'Bearer '+token},cache:'no-store',signal:AbortSignal.timeout(4000)});
    if (!response.ok) { $('preview').hidden=true; return; }
    const next = URL.createObjectURL(await response.blob());
    if (previewUrl) URL.revokeObjectURL(previewUrl);
    previewUrl = next; $('preview').src = next; $('preview').hidden = false;
  } catch (_) { $('preview').hidden = true; }
}
async function poll() {
  if (!token || busy || requestBusy) return;
  busy = true;
  const requestedOffset = offset, requestedRevision = revision;
  try {
    const s = await api('/api/state?offset=' + requestedOffset);
    if (requestedOffset !== offset || requestedRevision !== revision || requestBusy) return;
    offset = s.queueOffset;
    setConnected(true); render(s); refreshPreview(); if (!commandError) $('notice').textContent = t('connected');
  } catch (e) {
    if (e.message !== 'auth') { $('notice').textContent = t('offline'); setConnected(false); }
  } finally {
    busy = false;
    if ((requestedOffset !== offset && offset !== snapshot?.queueOffset) || requestedRevision !== revision) poll();
  }
}
async function command(name, value) {
  if (!connected || requestBusy) return;
  requestBusy = true; ++revision; controls();
  const body = {command:name};
  if (value !== undefined) body.value = value;
  try {
    await api('/api/command', body);
    commandError = false; if (name === 'open') offset = 0;
    $('notice').textContent = t('connected');
  } catch (e) {
    if (e.message !== 'auth') { commandError = true; $('notice').textContent = t('rejected'); }
  } finally { requestBusy = false; controls(); poll(); }
}
$('connect').onclick = () => { commandError = false; token = $('token').value.trim(); $('token').value = ''; poll(); };
document.querySelectorAll('[data-command]').forEach(button => button.onclick = () => command(button.dataset.command));
$('seek').onchange = () => command('seek', Number($('seek').value));
$('volume').oninput = () => { $('volumeValue').textContent = $('volume').value + '%'; };
$('volume').onchange = () => command('volume', Number($('volume').value));
$('rate').onchange = () => command('rate', Number($('rate').value));
$('source').oninput = controls;
$('sourceForm').onsubmit = e => { e.preventDefault(); command('open', $('source').value.trim()); };
$('enqueueSource').onclick = () => command('enqueue', $('source').value.trim());
$('queuePrevious').onclick = () => { offset = Math.max(0, snapshot.queueOffset - snapshot.queueLimit); poll(); };
$('queueNext').onclick = () => { offset = snapshot.queueOffset + snapshot.queueLimit; poll(); };
translate(); setConnected(false); poll(); setInterval(poll, 1000);
