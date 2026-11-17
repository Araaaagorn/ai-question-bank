/* ── 教师审核队列页（#review，仅 teacher/admin） ──
 *
 * 契约：docs/questions.md（feat/student-question-review）
 *   GET  /api/v1/questions?status=pending|rejected   （student 403）
 *   POST /api/v1/questions/{id}/review  {"action":"approve"|"reject"}
 *     仅 pending 可审；approve → active 且 teacher_id 置为审核人（打通 AI 路由）；
 *     reject → rejected 终态；非 pending 返回 409。
 */
const ReviewPage = (() => {
  const $ = (id) => document.getElementById(id);

  let currentStatus = 'pending';

  function isReviewer() {
    const user = API.getUser();
    return !!user && (user.role === 'teacher' || user.role === 'admin');
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
    $('page-review').classList.remove('hidden');

    const user = API.getUser();
    $('userInfo').textContent = user ? (user.name || user.username) : '';

    $('reviewError').classList.add('hidden');
    $('reviewMsg').classList.add('hidden');
    currentStatus = 'pending';
    syncTabs();
    if (!isReviewer()) {
      $('reviewLoading').classList.add('hidden');
      $('reviewErrorText').textContent = '仅教师或管理员可以审核学生提交的题目';
      $('reviewError').classList.remove('hidden');
      $('reviewList').innerHTML = '';
      return;
    }
    load();
  }

  function syncTabs() {
    $('reviewTabPending').classList.toggle('active', currentStatus === 'pending');
    $('reviewTabRejected').classList.toggle('active', currentStatus === 'rejected');
  }

  function switchStatus(status) {
    if (status === currentStatus) return;
    currentStatus = status;
    syncTabs();
    if (isReviewer()) load();
  }

  async function load() {
    $('reviewError').classList.add('hidden');
    $('reviewMsg').classList.add('hidden');
    $('reviewList').innerHTML = '';
    $('reviewLoading').classList.remove('hidden');
    try {
      const data = await API.questions({ status: currentStatus });
      render(data.questions || []);
    } catch (e) {
      $('reviewErrorText').textContent = '加载审核队列失败：' + (e.message || '未知错误');
      $('reviewError').classList.remove('hidden');
    } finally {
      $('reviewLoading').classList.add('hidden');
    }
  }

  function render(questions) {
    const list = $('reviewList');
    if (questions.length === 0) {
      list.innerHTML = '<p class="empty-state">' +
        (currentStatus === 'pending' ? '暂无待审核题目' : '暂无被驳回的题目') + '</p>';
      return;
    }
    list.innerHTML = questions.map((q) => {
      const safeId = parseInt(q.id, 10);
      /* id 只接受正整数再拼进 onclick，杜绝属性注入 */
      if (!isFinite(safeId) || safeId <= 0) return '';
      const typeMap = { single_choice: '选择题', short_answer: '简答题' };
      const typeLabel = typeMap[q.type] || String(q.type || '');
      const kps = Array.isArray(q.knowledge_points) ? q.knowledge_points : [];
      const tags = kps.map((k) => `<span class="q-tag">${escapeHtml(k)}</span>`).join('');
      const options = Array.isArray(q.options) && q.options.length > 0
        ? q.options.map((opt) =>
            `<div class="q-opt-item"><span class="q-opt-label">${escapeHtml(opt.label)}.</span>${escapeHtml(opt.text)}</div>`
          ).join('')
        : '<p class="q-opt-empty">（简答题，无选项）</p>';
      const actions = currentStatus === 'pending'
        ? `<div class="q-card-actions">
            <button class="btn btn-sm btn-primary" onclick="ReviewPage.approve(${safeId})">通过</button>
            <button class="btn btn-sm btn-danger" onclick="ReviewPage.reject(${safeId})">驳回</button>
          </div>`
        : '<p class="form-hint">已驳回，终态。学生如需再次投稿需重新提交。</p>';
      return `<div class="q-card" style="cursor:default">
        <div class="q-card-title">${escapeHtml(q.content)}</div>
        <div class="q-card-meta">
          <span class="q-tag">${escapeHtml(typeLabel)}</span>
          ${tags}
        </div>
        <details class="q-answer">
          <summary>查看选项与答案</summary>
          ${options}
          <p><strong>答案：</strong><span>${escapeHtml(q.answer || '（暂无）')}</span></p>
          <p><strong>解析：</strong><span>${escapeHtml(q.analysis || '（暂无）')}</span></p>
        </details>
        ${actions}
      </div>`;
    }).join('');
  }

  function showMsg(text) {
    $('reviewMsg').textContent = text;
    $('reviewMsg').classList.remove('hidden');
  }

  async function review(id, action) {
    try {
      await API.reviewQuestion(id, action);
      showMsg(action === 'approve'
        ? '已通过：题目生效，归属当前审核教师。'
        : '已驳回。');
      load();
    } catch (e) {
      if (e.status === 409) {
        /* 题目已被他人审过：刷新列表即自愈 */
        showMsg('该题目已被处理（可能是其他教师已审核），列表已刷新。');
        load();
      } else {
        showMsg('操作失败：' + (e.message || '未知错误'));
      }
    }
  }

  function approve(id) { return review(id, 'approve'); }
  function reject(id) { return review(id, 'reject'); }

  return { show, load, switchStatus, approve, reject };
})();
