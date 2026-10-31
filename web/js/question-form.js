/* ── 题目表单页（新增 / 编辑） ── */
const QuestionFormPage = (() => {
  const $ = (id) => document.getElementById(id);
  let currentId = null;
  let currentType = 'single_choice';

  function isTeacherOrAdmin() {
    const user = API.getUser();
    return user && (user.role === 'teacher' || user.role === 'admin');
  }

  function show(id) {
    if (!isTeacherOrAdmin()) {
      navigate('#questions');
      return;
    }
    $('page-login').classList.add('hidden');
    $('navbar').classList.remove('hidden');
    $('page-questions').classList.add('hidden');
    $('page-chat').classList.add('hidden');
    $('page-question-form').classList.remove('hidden');
    $('questionFormError').classList.add('hidden');
    currentId = id;
    resetForm();
    if (id === 'new') {
      $('questionFormTitle').textContent = '新增题目';
      $('questionId').value = '';
      setType('single_choice');
      addOption('', '');
    } else {
      $('questionFormTitle').textContent = '编辑题目';
      $('questionId').value = id;
      loadQuestion(id);
    }
  }

  function resetForm() {
    $('qContent').value = '';
    $('qAnalysis').value = '';
    $('qAnswer').value = '';
    $('qKnowledge').value = '';
    $('qOptionsContainer').innerHTML = '';
    currentType = 'single_choice';
    document.querySelectorAll('input[name="qType"]').forEach((el) => {
      el.checked = el.value === 'single_choice';
    });
  }

  function setType(type) {
    currentType = type;
    document.querySelectorAll('input[name="qType"]').forEach((el) => {
      el.checked = el.value === type;
    });
    const container = $('qOptionsContainer');
    if (type === 'single_choice') {
      $('qOptionsSection').classList.remove('hidden');
      if (container.children.length === 0) addOption('', '');
    } else {
      $('qOptionsSection').classList.add('hidden');
    }
  }

  function addOption(label, text) {
    const container = $('qOptionsContainer');
    const idx = container.children.length + 1;
    const div = document.createElement('div');
    div.className = 'q-option-row';
    div.innerHTML = `<input type="text" class="q-opt-label-input" placeholder="label" value="${escapeHtml(label)}" />
      <input type="text" class="q-opt-text-input" placeholder="选项文本" value="${escapeHtml(text)}" />
      <button type="button" class="btn btn-sm btn-danger" onclick="QuestionFormPage.removeOption(this)">×</button>`;
    container.appendChild(div);
  }

  function removeOption(btn) {
    const row = btn.closest('.q-option-row');
    if (row) row.remove();
  }

  async function loadQuestion(id) {
    try {
      const data = await API.questionDetail(id);
      const q = data.question || {};
      $('qContent').value = q.content || '';
      $('qAnalysis').value = q.analysis || '';
      $('qAnswer').value = q.answer || '';
      $('qKnowledge').value = (q.knowledge_points || []).join(', ');
      const type = q.type === 'short_answer' ? 'short_answer' : 'single_choice';
      setType(type);
      const container = $('qOptionsContainer');
      container.innerHTML = '';
      if (type === 'single_choice' && Array.isArray(q.options)) {
        if (q.options.length === 0) addOption('', '');
        q.options.forEach((o) => addOption(o.label, o.text));
      }
    } catch (e) {
      $('questionFormError').textContent = '加载题目失败：' + (e.message || '未知错误');
      $('questionFormError').classList.remove('hidden');
    }
  }

  function collectOptions() {
    const rows = document.querySelectorAll('.q-option-row');
    const options = [];
    rows.forEach((row) => {
      const label = row.querySelector('.q-opt-label-input').value.trim();
      const text = row.querySelector('.q-opt-text-input').value.trim();
      if (label && text) options.push({ label, text });
    });
    return options;
  }

  function collectPayload() {
    const payload = {
      content: $('qContent').value.trim(),
      analysis: $('qAnalysis').value.trim(),
      answer: $('qAnswer').value.trim(),
      type: currentType,
      knowledge_points: $('qKnowledge').value.split(/[,，]/).map((s) => s.trim()).filter((s) => s)
    };
    if (currentType === 'single_choice') {
      payload.options = collectOptions();
    } else {
      payload.options = [];
    }
    return payload;
  }

  async function save() {
    const btn = $('questionSaveBtn');
    btn.disabled = true;
    btn.textContent = '保存中...';
    $('questionFormError').classList.add('hidden');
    try {
      const payload = collectPayload();
      if (currentId === 'new') {
        await API.createQuestion(payload);
      } else {
        await API.updateQuestion(currentId, payload);
      }
      navigate('#questions');
    } catch (e) {
      $('questionFormError').textContent = '保存失败：' + (e.message || '未知错误');
      $('questionFormError').classList.remove('hidden');
      btn.disabled = false;
      btn.textContent = '保 存';
    }
  }

  function cancel() {
    navigate('#questions');
  }

  function bindEvents() {
    document.querySelectorAll('input[name="qType"]').forEach((el) => {
      el.addEventListener('change', (e) => setType(e.target.value));
    });
    $('questionSaveBtn').addEventListener('click', save);
    $('questionCancelBtn').addEventListener('click', cancel);
  }

  if (typeof document !== 'undefined') {
    if (document.readyState === 'loading') {
      document.addEventListener('DOMContentLoaded', bindEvents);
    } else {
      bindEvents();
    }
  }

  return { show, addOption, removeOption };
})();
