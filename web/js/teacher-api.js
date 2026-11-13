/* ── 教师 AI API 配置页 ──
 *
 * 契约：docs/teacher-api.md
 *   GET   /api/v1/teacher/ai-api  查询自己的配置（未配置返回 {configured:false}）
 *   PUT   /api/v1/teacher/ai-api  {api_key, model?} 新增/替换并启用，version 递增
 *   PATCH /api/v1/teacher/ai-api  {enabled:true|false} 启停（未配置时 404）
 *
 * 权限：仅 teacher；student / admin 调用均为 403，因此入口与页面都只对 teacher 开放。
 * 安全：页面只展示 masked_key，输入框提交后立即清空，任何路径都不回显完整密钥。
 */
const TeacherApiPage = (() => {
  const $ = (id) => document.getElementById(id);

  let current = null;

  function isTeacher() {
    const user = API.getUser();
    return !!user && user.role === 'teacher';
  }

  function show() {
    ['page-login', 'page-register', 'page-questions', 'page-chat'].forEach((id) => {
      $(id).classList.add('hidden');
    });
    $('navbar').classList.remove('hidden');
    $('page-teacher-api').classList.remove('hidden');

    $('teacherApiError').classList.add('hidden');
    $('teacherApiMsg').classList.add('hidden');
    $('teacherApiKey').value = '';
    $('teacherApiModel').value = '';

    if (!isTeacher()) {
      $('teacherApiLoading').classList.add('hidden');
      $('teacherApiContent').classList.add('hidden');
      $('teacherApiErrorText').textContent = '仅教师可配置自己的 AI API';
      $('teacherApiError').classList.remove('hidden');
      return;
    }
    load();
  }

  async function load() {
    $('teacherApiError').classList.add('hidden');
    $('teacherApiMsg').classList.add('hidden');
    $('teacherApiLoading').classList.remove('hidden');
    $('teacherApiContent').classList.add('hidden');
    try {
      current = await API.teacherAiApi();
      render();
      $('teacherApiContent').classList.remove('hidden');
    } catch (e) {
      $('teacherApiErrorText').textContent = '加载配置失败：' + (e.message || '未知错误');
      $('teacherApiError').classList.remove('hidden');
    } finally {
      $('teacherApiLoading').classList.add('hidden');
    }
  }

  /** 后端时间字段是 Unix 秒 */
  function formatTime(seconds) {
    if (!seconds) return '—';
    const date = new Date(seconds * 1000);
    return isNaN(date.getTime()) ? '—' : date.toLocaleString();
  }

  function render() {
    const el = $('teacherApiStatus');
    const btn = $('teacherApiToggleBtn');

    if (!current || !current.configured) {
      el.className = 'teacher-api-status status-none';
      el.textContent = '尚未配置 AI API：学生使用你的题目时将无法获得 AI 解答。';
      btn.disabled = true;
      btn.textContent = '停用';
      return;
    }

    el.className = 'teacher-api-status ' + (current.enabled ? 'status-active' : 'status-disabled');
    el.textContent = (current.enabled ? '已启用' : '已停用') +
      '；模型 ' + (current.model || '—') +
      '；密钥 ' + (current.masked_key || '—') +
      '；版本 v' + (current.version || 0) +
      '；更新于 ' + formatTime(current.updated_at);
    btn.disabled = false;
    btn.textContent = current.enabled ? '停用' : '启用';
  }

  function showMsg(text, isError) {
    const el = $('teacherApiMsg');
    el.textContent = text;
    el.className = 'form-hint ' + (isError ? 'is-error' : 'is-ok');
    el.classList.remove('hidden');
  }

  /** 400/403/404/415 的文案区分，避免把「未配置」误报成「无权限」 */
  function errorMessage(err) {
    if (err && err.code === 'INVALID_REQUEST') return '填写内容不合法，请检查 API Key 与模型名';
    if (err && err.status === 403) return '仅教师可配置自己的 AI API';
    if (err && err.status === 404) return '尚未配置 AI API，请先填写并保存';
    if (err && err.status === 415) return '请求格式不正确，请刷新页面重试';
    return (err && err.message) || '操作失败，请重试';
  }

  async function save(e) {
    e.preventDefault();
    if (!isTeacher()) return false;

    const key = $('teacherApiKey').value.trim();
    const model = $('teacherApiModel').value.trim();
    if (!key) {
      showMsg('请填写 API Key', true);
      return false;
    }

    const btn = $('teacherApiSaveBtn');
    btn.disabled = true;
    btn.textContent = '保存中...';
    try {
      const payload = { api_key: key };
      if (model) payload.model = model;
      current = await API.saveTeacherAiApi(payload);
      /* 提交后立刻清空，避免密钥停留在 DOM 里 */
      $('teacherApiKey').value = '';
      $('teacherApiModel').value = '';
      render();
      $('teacherApiContent').classList.remove('hidden');
      showMsg('已保存并启用', false);
    } catch (err) {
      showMsg('保存失败：' + errorMessage(err), true);
    } finally {
      btn.disabled = false;
      btn.textContent = '保存并启用';
    }
    return false;
  }

  async function toggle() {
    if (!isTeacher() || !current || !current.configured) return;
    const btn = $('teacherApiToggleBtn');
    const next = !current.enabled;
    btn.disabled = true;
    try {
      current = await API.setTeacherAiApiEnabled(next);
      render();
      showMsg(next ? '已启用' : '已停用', false);
    } catch (err) {
      showMsg('操作失败：' + errorMessage(err), true);
    } finally {
      btn.disabled = !current || !current.configured ? true : false;
    }
  }

  return { show, load, save, toggle };
})();
