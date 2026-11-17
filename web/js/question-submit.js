/* ── 学生投稿页 ──
 *
 * 契约：docs/questions.md（feat/student-question-review）
 *   POST /api/v1/questions 任意已登录角色可调用：
 *     student 提交 → status=pending 进入待审核队列，不直接生效；
 *     teacher/admin 提交 → status=active 直接生效。
 *
 * 字段 id 使用 qs* 前缀，避免与对话页详情区（q*）及题目表单页（qf*）重复。
 */
const QuestionSubmitPage = (() => {
  const $ = (id) => document.getElementById(id);
  let currentType = 'single_choice';

  function role() {
    const user = API.getUser();
    return user ? user.role : null;
  }

  function show() {
    if (!API.getToken()) {
      navigate('#login');
      return;
    }
    $('page-login').classList.add('hidden');
    $('page-register').classList.add('hidden');
    $('navbar').classList.remove('hidden');
    $('page-questions').classList.add('hidden');
    $('page-chat').classList.add('hidden');
    $('page-question-submit').classList.remove('hidden');
    $('questionSubmitError').classList.add('hidden');
    $('questionSubmitMsg').classList.add('hidden');
    updateUserInfo();
    resetForm();
  }

  function updateUserInfo() {
    const user = API.getUser();
    $('userInfo').textContent = user ? (user.name || user.username) : '';
  }

  function resetForm() {
    $('qsContent').value = '';
    $('qsAnalysis').value = '';
    $('qsAnswer').value = '';
    $('qsKnowledge').value = '';
    $('qsOptionsContainer').innerHTML = '';
    currentType = 'single_choice';
    document.querySelectorAll('input[name="qsType"]').forEach((el) => {
      el.checked = el.value === 'single_choice';
    });
    setType('single_choice');
  }

  function setType(type) {
    currentType = type;
    document.querySelectorAll('input[name="qsType"]').forEach((el) => {
      el.checked = el.value === type;
    });
    if (type === 'single_choice') {
      $('qsOptionsSection').classList.remove('hidden');
      if ($('qsOptionsContainer').children.length === 0) addOption('', '');
    } else {
      $('qsOptionsSection').classList.add('hidden');
    }
  }

  function addOption(label, text) {
    const container = $('qsOptionsContainer');
    const div = document.createElement('div');
    div.className = 'q-option-row';
    div.innerHTML = `<input type="text" class="q-opt-label-input" placeholder="label" value="${escapeHtml(label)}" />
      <input type="text" class="q-opt-text-input" placeholder="选项文本" value="${escapeHtml(text)}" />
      <button type="button" class="btn btn-sm btn-danger" onclick="QuestionSubmitPage.removeOption(this)">×</button>`;
    container.appendChild(div);
  }

  function removeOption(btn) {
    const row = btn.closest('.q-option-row');
    if (row) row.remove();
  }

  function collectOptions() {
    const options = [];
    document.querySelectorAll('#qsOptionsContainer .q-option-row').forEach((row) => {
      const label = row.querySelector('.q-opt-label-input').value.trim();
      const text = row.querySelector('.q-opt-text-input').value.trim();
      if (label && text) options.push({ label, text });
    });
    return options;
  }

  function collectPayload() {
    const payload = {
      content: $('qsContent').value.trim(),
      analysis: $('qsAnalysis').value.trim(),
      answer: $('qsAnswer').value.trim(),
      type: currentType,
      knowledge_points: $('qsKnowledge').value.split(/[,，]/).map((s) => s.trim()).filter((s) => s)
    };
    payload.options = currentType === 'single_choice' ? collectOptions() : [];
    return payload;
  }

  function showError(text) {
    $('questionSubmitError').textContent = text;
    $('questionSubmitError').classList.remove('hidden');
  }

  async function submit() {
    const btn = $('questionSubmitBtn');
    btn.disabled = true;
    btn.textContent = '提交中...';
    $('questionSubmitError').classList.add('hidden');
    $('questionSubmitMsg').classList.add('hidden');
    try {
      const payload = collectPayload();
      const result = await API.createQuestion(payload);
      const q = (result && result.question) || {};
      if (q.status === 'pending') {
        $('questionSubmitMsg').textContent = '已提交，等待教师审核通过后题目才会出现在题库中。';
      } else {
        $('questionSubmitMsg').textContent = '题目已创建并生效。';
      }
      $('questionSubmitMsg').classList.remove('hidden');
      resetForm();
      /* 留在本页让用户看到结果提示；点「返回题库」去列表 */
    } catch (e) {
      showError('提交失败：' + (e.message || '未知错误'));
    } finally {
      btn.disabled = false;
      btn.textContent = '提 交';
    }
  }

  function cancel() {
    navigate('#questions');
  }

  function bindEvents() {
    document.querySelectorAll('input[name="qsType"]').forEach((el) => {
      el.addEventListener('change', (e) => setType(e.target.value));
    });
    $('questionSubmitBtn').addEventListener('click', submit);
    $('questionSubmitCancelBtn').addEventListener('click', cancel);
  }

  if (typeof document !== 'undefined') {
    if (document.readyState === 'loading') {
      document.addEventListener('DOMContentLoaded', bindEvents);
    } else {
      bindEvents();
    }
  }

  return { show, submit, addOption, removeOption };
})();
