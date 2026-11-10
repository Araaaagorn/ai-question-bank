/* ── 试卷编辑页（元数据 + 题目编排） ──
 *
 * 契约：docs/exams.md
 *   POST   /api/v1/exams                      创建（仅 name / description）
 *   PUT    /api/v1/exams/{id}                 改元数据（部分更新）
 *   PATCH  /api/v1/exams/{id}                 status=active|archived
 *   PUT    /api/v1/exams/{id}/questions       整体替换编排（数组顺序即 position，服务端归一化）
 *   DELETE /api/v1/exams/{id}                 删除（不受 archived 限制）
 *
 * 编排采用「本地编辑 + 一次整体替换」：上移/下移/改分值/移除都只动本地数组，
 * 点「保存编排」时一次性 PUT，避免多次追加/移除造成中间态。
 */
const ExamFormPage = (() => {
  const $ = (id) => document.getElementById(id);

  let examId = null;   // null 表示新建
  let exam = null;
  let items = [];      // [{ question_id, score }]
  let bank = [];       // 题库缓存（GET /questions）

  function isEditor() {
    const user = API.getUser();
    return !!user && (user.role === 'teacher' || user.role === 'admin');
  }

  function archived() {
    return !!exam && exam.status === 'archived';
  }

  function show(param) {
    ['page-login', 'page-register', 'page-questions', 'page-chat', 'page-teacher-api',
      'page-exams', 'page-exam'].forEach((id) => {
      const el = $(id);
      if (el) el.classList.add('hidden');
    });
    $('navbar').classList.remove('hidden');
    $('page-exam-form').classList.remove('hidden');

    examId = param === 'new' ? null : parseInt(param, 10);
    exam = null;
    items = [];
    $('examFormError').classList.add('hidden');
    $('examFormMsg').classList.add('hidden');
    $('examName').value = '';
    $('examDescription').value = '';
    $('examArrangeList').innerHTML = '';
    $('examAddSelect').innerHTML = '';

    if (!isEditor()) {
      $('examFormLoading').classList.add('hidden');
      $('examFormErrorText').textContent = '仅教师或管理员可以编辑试卷';
      $('examFormError').classList.remove('hidden');
      return;
    }
    load();
  }

  async function load() {
    $('examFormLoading').classList.remove('hidden');
    $('examFormError').classList.add('hidden');
    try {
      const [detail, questions] = await Promise.all([
        examId === null ? Promise.resolve(null) : API.examDetail(examId),
        API.questions().catch(() => ({ questions: [] }))
      ]);
      exam = detail;
      bank = Array.isArray(questions) ? questions : (questions.questions || []);
      if (exam) {
        $('examName').value = exam.name || '';
        $('examDescription').value = exam.description || '';
        items = (exam.questions || []).map((q) => ({ question_id: q.question_id, score: q.score }));
      }
      render();
    } catch (e) {
      $('examFormErrorText').textContent = '加载失败：' + (e.message || '未知错误');
      $('examFormError').classList.remove('hidden');
    } finally {
      $('examFormLoading').classList.add('hidden');
    }
  }

  function render() {
    const creating = examId === null;
    $('examFormTitle').textContent = creating ? '新建试卷' : '编辑试卷';
    $('examSaveBtn').textContent = creating ? '创建试卷' : '保存基本信息';

    $('examStatusBtn').classList.toggle('hidden', creating);
    $('examDeleteBtn').classList.toggle('hidden', creating);
    if (!creating) {
      $('examStatusBtn').textContent = archived() ? '恢复为进行中' : '归档试卷';
      $('examStatusBtn').disabled = false;
    }

    /* 归档后禁止编辑：输入框与编排按钮统一禁用 */
    const locked = archived();
    $('examName').disabled = locked;
    $('examDescription').disabled = locked;
    $('examSaveBtn').disabled = locked;
    $('examAddSelect').disabled = locked || bank.length === 0;
    $('examAddScore').disabled = locked;
    $('examAddBtn').disabled = locked;
    $('examArrangeSaveBtn').disabled = locked;
    $('examArchiveHint').classList.toggle('hidden', !locked);
    $('examArrangeHint').classList.toggle('hidden', creating);

    renderBank();
    renderItems();
  }

  function renderBank() {
    const select = $('examAddSelect');
    const selected = {};
    items.forEach((item) => { selected[item.question_id] = true; });
    select.innerHTML = bank.map((q) => {
      const id = parseInt(q.id, 10);
      if (!isFinite(id) || selected[id]) return '';
      return '<option value="' + id + '">#' + id + ' ' + escapeHtml(String(q.content || '').slice(0, 30)) + '</option>';
    }).join('');
    if (!select.innerHTML) {
      select.innerHTML = '<option value="">（题库中没有可添加的题目）</option>';
    }
  }

  function renderItems() {
    const list = $('examArrangeList');
    if (items.length === 0) {
      list.innerHTML = '<p class="empty-state">还没有题目，从下方选择后点「添加」</p>';
      return;
    }
    const locked = archived();
    const total = items.reduce((sum, item) => sum + (Number(item.score) || 0), 0);
    list.innerHTML = items.map((item, index) => {
      const q = bank.filter((one) => parseInt(one.id, 10) === item.question_id)[0];
      const text = q ? String(q.content || '').slice(0, 40) : '题目 #' + item.question_id;
      return `
      <div class="arrange-item">
        <span class="exam-pos">${index + 1}</span>
        <span class="arrange-text">${escapeHtml(text)}</span>
        <input type="number" class="arrange-score" id="examScore${index}" min="0.5" max="1000" step="0.5"
               value="${escapeHtml(String(item.score))}" ${locked ? 'disabled' : ''}
               onchange="app.examSetScore(${index}, this.value)" />
        <span class="arrange-actions">
          <button class="btn btn-sm btn-secondary" onclick="app.examMove(${index}, -1)" ${locked || index === 0 ? 'disabled' : ''}>↑</button>
          <button class="btn btn-sm btn-secondary" onclick="app.examMove(${index}, 1)" ${locked || index === items.length - 1 ? 'disabled' : ''}>↓</button>
          <button class="btn btn-sm btn-secondary" onclick="app.examRemove(${index})" ${locked ? 'disabled' : ''}>移除</button>
        </span>
      </div>`;
    }).join('') + '<p class="form-hint">共 ' + items.length + ' 道题，合计 ' + total + ' 分</p>';
  }

  function showMsg(text, isError) {
    const el = $('examFormMsg');
    el.textContent = text;
    el.className = 'form-hint ' + (isError ? 'is-error' : 'is-ok');
    el.classList.remove('hidden');
  }

  /** 后端错误码 → 可读文案（docs/exams.md §8） */
  function errorMessage(err) {
    const code = err && err.code;
    if (code === 'EXAM_ARCHIVED') return '试卷已归档，请先恢复为进行中再编辑';
    if (code === 'EXAM_FORBIDDEN') return '无权修改该试卷';
    if (code === 'EXAM_NOT_FOUND') return '试卷不存在或已被删除';
    if (code === 'EXAM_INVALID_LAYOUT') return '题目顺序或重复项不合法';
    if (code === 'EXAM_INVALID_QUESTION') return '引用了不存在或已停用的题目';
    if (code === 'EXAM_INVALID_REQUEST') return '填写内容不合法，请检查名称、描述与分值';
    return (err && err.message) || '操作失败，请重试';
  }

  async function save(e) {
    if (e) e.preventDefault();
    if (!isEditor() || archived()) return false;

    const name = $('examName').value.trim();
    const description = $('examDescription').value.trim();
    if (!name) { showMsg('请填写试卷名称', true); return false; }
    if (Array.from(name).length > 100) { showMsg('试卷名称不能超过 100 个字符', true); return false; }
    if (Array.from(description).length > 1000) { showMsg('描述不能超过 1000 个字符', true); return false; }

    const btn = $('examSaveBtn');
    btn.disabled = true;
    try {
      if (examId === null) {
        exam = await API.createExam({ name: name, description: description });
        examId = exam.id;
        /* 新建成功后切到编辑态：URL 同步，便于刷新与分享 */
        if (typeof window !== 'undefined' && window.location &&
            window.location.hash !== '#exam-form/' + examId && window.history &&
            typeof window.history.replaceState === 'function') {
          window.history.replaceState(null, '', '#exam-form/' + examId);
        }
        render();
        showMsg('试卷已创建，可以开始编排题目', false);
      } else {
        exam = await API.updateExam(examId, { name: name, description: description });
        render();
        showMsg('基本信息已保存', false);
      }
    } catch (err) {
      showMsg('保存失败：' + errorMessage(err), true);
      if (err && err.code === 'EXAM_ARCHIVED') load();
    } finally {
      btn.disabled = archived();
    }
    return false;
  }

  async function toggleStatus() {
    if (!exam || archived() === undefined) return;
    const btn = $('examStatusBtn');
    btn.disabled = true;
    const next = archived() ? 'active' : 'archived';
    try {
      exam = await API.setExamStatus(examId, next);
      render();
      showMsg(next === 'archived' ? '试卷已归档，编辑已锁定' : '试卷已恢复为进行中', false);
    } catch (err) {
      showMsg('操作失败：' + errorMessage(err), true);
    } finally {
      btn.disabled = false;
    }
  }

  async function remove() {
    if (examId === null) return;
    if (!window.confirm('删除后无法恢复，确定删除该试卷？')) return;
    try {
      await API.deleteExam(examId);
      app.navigate('#exams');
    } catch (err) {
      showMsg('删除失败：' + errorMessage(err), true);
    }
  }

  /* ── 本地编排操作 ── */
  function add() {
    if (archived()) return;
    const select = $('examAddSelect');
    const qid = parseInt(select.value, 10);
    if (!isFinite(qid) || qid <= 0) return;
    const score = Number($('examAddScore').value);
    if (!isFinite(score) || score <= 0 || score > 1000) {
      showMsg('分值需大于 0 且不超过 1000', true);
      return;
    }
    if (items.some((item) => item.question_id === qid)) {
      showMsg('该题已在试卷中', true);
      return;
    }
    if (items.length >= 200) {
      showMsg('单卷题目不能超过 200 道', true);
      return;
    }
    items.push({ question_id: qid, score: score });
    $('examAddScore').value = '5';
    render();
    showMsg('已添加到编排列表，点「保存编排」生效', false);
  }

  function move(index, delta) {
    if (archived()) return;
    const target = index + delta;
    if (index < 0 || index >= items.length || target < 0 || target >= items.length) return;
    const moved = items.splice(index, 1)[0];
    items.splice(target, 0, moved);
    render();
  }

  function removeItem(index) {
    if (archived()) return;
    if (index < 0 || index >= items.length) return;
    items.splice(index, 1);
    render();
  }

  function setScore(index, value) {
    if (archived()) return;
    const score = Number(value);
    if (!isFinite(score) || score <= 0 || score > 1000) {
      showMsg('分值需大于 0 且不超过 1000', true);
      renderItems();
      return;
    }
    items[index].score = score;
    renderItems();
  }

  async function saveArrange() {
    if (examId === null) { showMsg('请先创建试卷', true); return; }
    if (archived()) { showMsg('试卷已归档，不能修改编排', true); return; }
    const btn = $('examArrangeSaveBtn');
    btn.disabled = true;
    try {
      const payload = items.map((item, index) => ({
        question_id: item.question_id,
        position: index + 1,
        score: item.score
      }));
      exam = await API.replaceExamQuestions(examId, payload);
      items = (exam.questions || []).map((q) => ({ question_id: q.question_id, score: q.score }));
      render();
      showMsg('编排已保存', false);
    } catch (err) {
      showMsg('保存编排失败：' + errorMessage(err), true);
      if (err && err.code === 'EXAM_ARCHIVED') load();
    } finally {
      btn.disabled = archived();
    }
  }

  return { show, load, save, toggleStatus, remove, add, move, removeItem, setScore, saveArrange };
})();
