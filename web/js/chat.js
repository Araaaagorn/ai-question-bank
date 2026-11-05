/* ── 对话页（题目详情 + AI 对话） ──
 *
 * 接入 Pi Agent v1.2 异步任务：
 *   - 进入页面时查询 AI 授权状态（GET /api/v1/ai/access）
 *   - 发送时 POST /api/v1/chat 得到 job_id，轮询 GET /api/v1/chat/{job_id}
 *   - 支持取消（DELETE /api/v1/chat/{job_id}）
 *   - 支持追问：同一题目同一模式下复用 conversation_id
 *   - 支持三种模式：solve / hint / review
 */
const ChatPage = (() => {
  const $ = (id) => document.getElementById(id);
  let currentQuestionId = null;
  let currentMode = 'solve';
  let chatMockNoticeShown = false;

  /* conversation_id 按 "题目 + 模式" 隔离保存，支持同题追问 */
  const conversations = new Map();

  /* 当前正在进行的任务控制器 */
  let activeController = null;
  let activeJobId = null;

  /* AI 权限状态 */
  let aiAccess = null;

  /* ── UI 辅助 ── */
  function getModeLabel(mode) {
    return { solve: '完整讲解', hint: '分层提示', review: '错因分析' }[mode] || mode;
  }

  function setSendEnabled(enabled) {
    $('chatSendBtn').disabled = !enabled;
  }

  function setCancelVisible(visible) {
    const btn = $('chatCancelBtn');
    if (visible) btn.classList.remove('hidden');
    else btn.classList.add('hidden');
  }

  function setInputsEnabled(enabled) {
    $('chatInput').disabled = !enabled;
    $('chatMode').disabled = !enabled;
    $('studentAnswerInput').disabled = !enabled;
  }

  function renderAiAccess() {
    const el = $('aiAccessStatus');
    if (!aiAccess) {
      el.classList.add('hidden');
      return;
    }
    el.classList.remove('hidden');
    const statusMap = {
      active: { cls: 'status-active', text: 'AI 权限已启用' },
      not_granted: { cls: 'status-denied', text: '尚未获得 AI 使用权限，请联系平台负责人' },
      revoked: { cls: 'status-denied', text: 'AI 使用权限已撤销，请联系平台负责人' },
      expired: { cls: 'status-denied', text: 'AI 使用权限已到期，请联系平台负责人' },
    };
    const info = statusMap[aiAccess.status] || { cls: 'status-denied', text: 'AI 权限状态未知' };
    el.className = 'ai-access-status ' + info.cls;
    let text = info.text;
    if (aiAccess.expires_at) {
      text += '（到期：' + aiAccess.expires_at + '）';
    }
    el.textContent = text;
  }

  async function refreshAiAccess(silent) {
    try {
      aiAccess = await API.aiAccess();
    } catch (e) {
      if (!silent) {
        addMessage('system', 'AI 权限查询失败：' + (e.message || '未知错误'));
      }
      aiAccess = { status: 'not_granted', allowed: false, expires_at: null };
    }
    renderAiAccess();
    setSendEnabled(aiAccess.allowed || API.isOfflineDemo());
    return aiAccess;
  }

  /* ── 页面生命周期 ── */
  function show(questionId) {
    currentQuestionId = parseInt(questionId, 10);
    $('page-login').classList.add('hidden');
    $('navbar').classList.remove('hidden');
    $('page-questions').classList.add('hidden');
    $('page-chat').classList.remove('hidden');

    // 重置
    $('chatMessages').innerHTML = '';
    chatMockNoticeShown = false;
    $('chatInput').value = '';
    $('studentAnswerInput').value = '';
    $('detailLoading').classList.remove('hidden');
    $('detailError').classList.add('hidden');
    $('detailContent').classList.add('hidden');
    $('aiAccessStatus').classList.add('hidden');
    setCancelVisible(false);

    // 默认模式
    currentMode = 'solve';
    $('chatMode').value = currentMode;
    syncReviewInput();

    updateUserInfo();
    loadQuestionDetail(currentQuestionId);

    // 欢迎消息
    addMessage('system', '请在下方输入你的问题，AI 将为你解答这道题。');

    /* 离线演示时明确告知 */
    if (typeof API !== 'undefined' && API.isOfflineDemo && API.isOfflineDemo()) {
      addMessage('system', '⚠ 离线演示模式：数据来自前端内置演示集，未连接后端，回复不是真实 AI 生成。');
    }

    // 查询 AI 权限（非阻塞）
    refreshAiAccess(true);
  }

  function updateUserInfo() {
    const user = API.getUser();
    $('userInfo').textContent = user ? (user.name || user.username) + ' (' + (user.role === 'admin' ? '管理员' : user.role === 'teacher' ? '老师' : '学生') + ')' : '';
  }

  async function loadQuestionDetail(id) {
    try {
      const data = await API.questionDetail(id);
      const q = data.question;
      renderDetail(q);
      $('chatQuestionTitle').textContent = '题目 #' + q.id;
    } catch (e) {
      $('detailLoading').classList.add('hidden');
      $('detailError').textContent = '加载题目失败：' + (e.message || '未知错误');
      $('detailError').classList.remove('hidden');
    }
  }

  function renderDetail(q) {
    $('detailLoading').classList.add('hidden');
    $('detailContent').classList.remove('hidden');

    $('qContent').textContent = q.content;

    // 选项
    const optsEl = $('qOptions');
    if (Array.isArray(q.options) && q.options.length > 0) {
      optsEl.innerHTML = q.options.map(opt =>
        `<div class="q-opt-item"><span class="q-opt-label">${escapeHtml(opt.label)}.</span>${escapeHtml(opt.text)}</div>`
      ).join('');
    } else {
      optsEl.innerHTML = '<p style="color:#999;font-size:13px;">（本题为简答/填空类型，无选项）</p>';
    }

    // 类型标签
    const typeMap = { 'single_choice': '选择题', 'short_answer': '简答题' };
    $('qType').textContent = typeMap[q.type] || q.type;

    // 知识点
    const kps = Array.isArray(q.knowledge_points) ? q.knowledge_points : [];
    $('qKnowledge').textContent = kps.length > 0 ? kps.join('、') : '';

    // 答案 & 解析
    $('qAnswer').textContent = q.answer || '（暂无）';
    $('qAnalysis').textContent = q.analysis || '（暂无）';
  }

  /* ── 模式切换 ── */
  function onModeChange() {
    const newMode = $('chatMode').value;
    if (newMode === currentMode) return;
    currentMode = newMode;
    syncReviewInput();
    // 切换模式建立新对话
    addMessage('system', '已切换为「' + getModeLabel(currentMode) + '」模式，将开启新对话。');
  }

  function syncReviewInput() {
    const input = $('studentAnswerInput');
    if (currentMode === 'review') {
      input.classList.remove('hidden');
    } else {
      input.classList.add('hidden');
    }
  }

  /* ── 对话管理 ── */
  function getConversationKey() {
    return currentQuestionId + ':' + currentMode;
  }

  function getConversationId() {
    return conversations.get(getConversationKey()) || null;
  }

  function setConversationId(id) {
    conversations.set(getConversationKey(), id);
  }

  /* ── 发送消息 ── */
  async function sendMessage() {
    const input = $('chatInput');
    const text = input.value.trim();
    if (!text || !currentQuestionId) return;

    if (!aiAccess || !aiAccess.allowed) {
      await refreshAiAccess(false);
      if (!aiAccess || !aiAccess.allowed) {
        addMessage('system', '当前无法使用 AI 解答，请先申请 AI 使用权限。');
        return;
      }
    }

    const studentAnswer = currentMode === 'review' ? $('studentAnswerInput').value.trim() : undefined;
    if (currentMode === 'review' && !studentAnswer) {
      addMessage('system', '错因分析模式需要填写你的作答或过程。');
      return;
    }

    input.value = '';
    if (currentMode === 'review') $('studentAnswerInput').value = '';
    addMessage('user', text + (studentAnswer ? '\n（我的作答：' + studentAnswer + '）' : ''));

    const loadingId = addMessage('loading', 'AI 正在排队...');
    setSendEnabled(false);
    setInputsEnabled(false);
    setCancelVisible(true);

    activeController = new AbortController();
    const conversationId = getConversationId();

    try {
      const result = await API.chat({
        questionId: currentQuestionId,
        message: text,
        conversationId: conversationId,
        mode: currentMode,
        studentAnswer: studentAnswer,
        signal: activeController.signal,
        onStatus: (job) => {
          updateLoadingText(loadingId, job);
        }
      });

      removeMessage(loadingId);
      addMessage('ai', result.reply || '（无回复）');

      if (result.conversation_id) {
        setConversationId(result.conversation_id);
      }

      if (!chatMockNoticeShown && API.isChatMocked && API.isChatMocked()) {
        chatMockNoticeShown = true;
        addMessage('system', '⚠ 上面这条回复来自前端内置演示数据：后端 /api/v1/chat 尚未实现，不是真实 AI 生成。');
      }
    } catch (e) {
      removeMessage(loadingId);
      handleChatError(e, text, studentAnswer);
    } finally {
      activeController = null;
      activeJobId = null;
      setSendEnabled(true);
      setInputsEnabled(true);
      setCancelVisible(false);
    }
  }

  function updateLoadingText(loadingId, job) {
    const el = document.getElementById(loadingId);
    if (!el) return;
    const statusMap = {
      queued: 'AI 正在排队...',
      running: 'AI 正在解答...',
      succeeded: '解答完成',
      failed: '解答失败',
      cancelled: '已取消'
    };
    el.textContent = statusMap[job.status] || 'AI 处理中...';
    activeJobId = job.job_id || activeJobId;
  }

  function handleChatError(e, text, studentAnswer) {
    let message = e.message || '未知错误';
    if (e.code === 'AI_ACCESS_REQUIRED' || e.code === 'AI_ACCESS_REVOKED' || e.code === 'AI_ACCESS_EXPIRED') {
      message = 'AI 权限不足：' + message + '。请刷新权限或联系平台负责人。';
      refreshAiAccess(true);
    } else if (e.code === 'CLIENT_CANCELLED') {
      message = '已取消解答。';
    } else if (e.code === 'CLIENT_TIMEOUT') {
      message = '等待解答超时，请稍后重试。';
    } else if (e.status === 401 || e.code === 'UNAUTHORIZED') {
      message = '登录已过期，请重新登录。';
    }
    addMessage('system', '对话失败：' + message);
  }

  function cancel() {
    if (activeController) {
      activeController.abort();
    }
  }

  /* ── 消息管理 ── */
  let msgCounter = 0;
  function addMessage(type, text) {
    const id = 'msg-' + (++msgCounter);
    const el = document.createElement('div');
    el.id = id;
    el.className = 'msg msg-' + type;
    el.textContent = text;
    if (type === 'ai') {
      el.innerHTML = escapeHtml(text).replace(/\n/g, '<br>');
    }
    $('chatMessages').appendChild(el);
    el.scrollIntoView({ behavior: 'smooth', block: 'end' });
    return id;
  }

  function removeMessage(id) {
    const el = document.getElementById(id);
    if (el) el.remove();
  }

  return { show, sendMessage, cancel, onModeChange };
})();

/* 模式切换事件 */
document.addEventListener('DOMContentLoaded', () => {
  const modeSelect = document.getElementById('chatMode');
  if (modeSelect) {
    modeSelect.addEventListener('change', () => {
      ChatPage.onModeChange && ChatPage.onModeChange();
    });
  }
});
