/* ── 试卷列表与浏览 ──
 *
 * 契约：docs/exams.md
 *   GET /api/v1/exams        列表（admin 全部 / teacher 本人 / student 仅 active）
 *   GET /api/v1/exams/{id}   详情（含内联题目，按 position 升序）
 *
 * 浏览页对 student 只读；teacher/admin 可进入编辑页。
 */
const ExamsPage = (() => {
  const $ = (id) => document.getElementById(id);

  const STATUS_LABEL = { active: '进行中', archived: '已归档' };

  function canEdit() {
    const user = API.getUser();
    return !!user && (user.role === 'teacher' || user.role === 'admin');
  }

  function show() {
    ['page-login', 'page-register', 'page-questions', 'page-chat', 'page-teacher-api',
      'page-exam', 'page-exam-form'].forEach((id) => {
      const el = $(id);
      if (el) el.classList.add('hidden');
    });
    $('navbar').classList.remove('hidden');
    $('page-exams').classList.remove('hidden');
    $('examsNewBtn').classList.toggle('hidden', !canEdit());
    load();
  }

  async function load() {
    $('examsError').classList.add('hidden');
    $('examsList').innerHTML = '';
    $('examsLoading').classList.remove('hidden');
    try {
      const exams = await API.exams();
      render(exams);
    } catch (e) {
      $('examsErrorText').textContent = '加载试卷失败：' + (e.message || '未知错误');
      $('examsError').classList.remove('hidden');
    } finally {
      $('examsLoading').classList.add('hidden');
    }
  }

  function formatTime(seconds) {
    if (!seconds) return '—';
    const date = new Date(seconds * 1000);
    return isNaN(date.getTime()) ? '—' : date.toLocaleString();
  }

  function render(exams) {
    const list = $('examsList');
    if (!exams || exams.length === 0) {
      list.innerHTML = '<p class="empty-state">暂无试卷</p>';
      return;
    }
    list.innerHTML = exams.map((exam) => {
      const safeId = parseInt(exam.id, 10);
      if (!isFinite(safeId) || safeId <= 0) return '';
      const status = exam.status === 'archived' ? 'archived' : 'active';
      return `
      <div class="q-card" onclick="app.openExam(${safeId})">
        <div class="q-card-title">${escapeHtml(exam.name || '')}</div>
        <div class="exam-sub">${escapeHtml(exam.description || '（无描述）')}</div>
        <div class="q-card-meta">
          <span class="q-tag status-${status}">${STATUS_LABEL[status]}</span>
          <span class="q-tag">${Number(exam.question_count) || 0} 道题</span>
          <span class="q-tag">更新于 ${escapeHtml(formatTime(exam.updated_at))}</span>
        </div>
      </div>`;
    }).join('');
  }

  return { show, load };
})();

/* ── 试卷浏览页（按 position 顺序展示内联题目） ── */
const ExamViewPage = (() => {
  const $ = (id) => document.getElementById(id);
  let currentId = null;

  function canEdit() {
    const user = API.getUser();
    return !!user && (user.role === 'teacher' || user.role === 'admin');
  }

  function show(examId) {
    currentId = parseInt(examId, 10);
    ['page-login', 'page-register', 'page-questions', 'page-chat', 'page-teacher-api',
      'page-exams', 'page-exam-form'].forEach((id) => {
      const el = $(id);
      if (el) el.classList.add('hidden');
    });
    $('navbar').classList.remove('hidden');
    $('page-exam').classList.remove('hidden');
    $('examQuestions').innerHTML = '';
    $('examEditBtn').classList.toggle('hidden', !canEdit());
    load();
  }

  async function load() {
    $('examError').classList.add('hidden');
    $('examLoading').classList.remove('hidden');
    try {
      const exam = await API.examDetail(currentId);
      render(exam);
    } catch (e) {
      $('examErrorText').textContent = '加载试卷失败：' + (e.message || '未知错误');
      $('examError').classList.remove('hidden');
    } finally {
      $('examLoading').classList.add('hidden');
    }
  }

  function render(exam) {
    $('examTitle').textContent = exam.name || '试卷';
    const total = (exam.questions || []).reduce((sum, q) => sum + (Number(q.score) || 0), 0);
    $('examMeta').textContent = (exam.description || '（无描述）') +
      '；状态 ' + (exam.status === 'archived' ? '已归档' : '进行中') +
      '；共 ' + (exam.questions || []).length + ' 道题，合计 ' + total + ' 分';

    $('examQuestions').innerHTML = (exam.questions || []).map((q) => {
      const options = Array.isArray(q.options) && q.options.length > 0
        ? '<div class="q-options">' + q.options.map((opt) =>
          '<div class="q-opt-item"><span class="q-opt-label">' + escapeHtml(opt.label || '') + '</span>' +
          escapeHtml(opt.text || '') + '</div>').join('') + '</div>'
        : '<p class="q-opt-empty">（本题无选项）</p>';
      return `
      <div class="exam-question">
        <div class="exam-question-head">
          <span class="exam-pos">第 ${Number(q.position) || 0} 题</span>
          <span class="q-tag">${escapeHtml(String(q.score))} 分</span>
        </div>
        <p class="q-content">${escapeHtml(q.content || '')}</p>
        ${options}
        <details class="q-answer">
          <summary>查看答案与解析</summary>
          <p><strong>答案：</strong>${escapeHtml(q.answer || '（暂无）')}</p>
          <p><strong>解析：</strong>${escapeHtml(q.analysis || '（暂无）')}</p>
        </details>
      </div>`;
    }).join('') || '<p class="empty-state">该试卷还没有题目</p>';
  }

  return { show, load };
})();
