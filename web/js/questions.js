/* ── 题目列表页 ── */
const QuestionsPage = (() => {
  const $ = (id) => document.getElementById(id);

  function isTeacherOrAdmin() {
    const user = API.getUser();
    return user && (user.role === 'teacher' || user.role === 'admin');
  }

  function show() {
    $('page-login').classList.add('hidden');
    $('navbar').classList.remove('hidden');
    $('page-questions').classList.remove('hidden');
    $('page-chat').classList.add('hidden');
    $('page-question-form').classList.add('hidden');
    $('questionsLoading').classList.remove('hidden');
    $('questionsError').classList.add('hidden');
    loadQuestions();
  }

  async function loadQuestions() {
    try {
      const data = await API.questions();    // { questions: [...] }
      renderQuestions(data.questions || []);
    } catch (e) {
      $('questionsLoading').classList.add('hidden');
      $('questionsError').textContent = '加载失败：' + (e.message || '未知错误');
      $('questionsError').classList.remove('hidden');
    }
  }

  function renderQuestions(questions) {
    $('questionsLoading').classList.add('hidden');
    const list = $('questionsList');
    let html = '';
    if (isTeacherOrAdmin()) {
      html += `<div class="q-manage-bar">
        <button class="btn btn-primary" onclick="QuestionsPage.create()">+ 新增题目</button>
      </div>`;
    }
    if (questions.length === 0) {
      list.innerHTML = html + '<p style="text-align:center;color:#999;">暂无题目</p>';
      return;
    }
    const items = questions.map(q => {
      const typeMap = { single_choice: '选择题', short_answer: '简答题' };
      const typeLabel = typeMap[q.type] || String(q.type || '');
      const kps = Array.isArray(q.knowledge_points) ? q.knowledge_points : [];
      const tags = kps.map(k => `<span class="q-tag">${escapeHtml(k)}</span>`).join('');
      /* id 会被拼进 onclick 属性，只接受正整数，杜绝属性注入 */
      const safeId = parseInt(q.id, 10);
      if (!isFinite(safeId) || safeId <= 0) return '';
      const adminButtons = isTeacherOrAdmin()
        ? `<div class="q-card-actions">
            <button class="btn btn-sm btn-secondary" onclick="event.stopPropagation();QuestionsPage.edit(${safeId})">编辑</button>
            <button class="btn btn-sm btn-danger" onclick="event.stopPropagation();QuestionsPage.remove(${safeId})">删除</button>
          </div>`
        : '';
      return `<div class="q-card" onclick="navigate('#chat/${safeId}')">
        <div class="q-card-title">${escapeHtml(q.content)}</div>
        <div class="q-card-meta">
          <span class="q-tag">${escapeHtml(typeLabel)}</span>
          ${tags}
        </div>
        ${adminButtons}
      </div>`;
    }).join('');
    list.innerHTML = html + items;
  }

  function create() {
    navigate('#question-form/new');
  }

  function edit(id) {
    navigate('#question-form/' + id);
  }

  async function remove(id) {
    if (!confirm('确定要删除这道题目吗？此操作不可恢复。')) return;
    try {
      await API.deleteQuestion(id);
      loadQuestions();
    } catch (e) {
      alert('删除失败：' + (e.message || '未知错误'));
    }
  }

  return { show, create, edit, remove };
})();
