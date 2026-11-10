/* ── 主应用：路由 + 页面切换 ── */
const $ = (id) => document.getElementById(id);

/* 简易 hash 路由 */
function navigate(hash) {
  window.location.hash = hash;
  route();
}

function route() {
  const hash = window.location.hash || '#login';

  /* 注册页只在 #register 下显示，其余路由统一隐藏 */
  $('page-register').classList.add('hidden');
  /* 试卷三页同理，只在对应路由下显示 */
  ['page-exams', 'page-exam', 'page-exam-form'].forEach((id) => {
    const el = $(id);
    if (el) el.classList.add('hidden');
  });
  if (hash === '#register') {
    RegisterPage.show();
    return;
  }

  // 检查是否已登录
  const token = API.getToken();

  if (hash === '#login' || hash === '' || hash === '#') {
    AuthPage.show();
    return;
  }

  // 以下页面需要登录
  if (!token) {
    navigate('#login');
    return;
  }

  // 更新导航栏用户信息
  const user = API.getUser();
  $('userInfo').textContent = user ? (user.name || user.username) : '';

  if (hash === '#questions') {
    QuestionsPage.show();
  } else if (hash.startsWith('#chat/')) {
    const id = hash.split('/')[1];
    ChatPage.show(id);
  } else if (hash === '#exams') {
    ExamsPage.show();
  } else if (hash.startsWith('#exam-form/')) {
    ExamFormPage.show(hash.split('/').slice(1).join('/'));
  } else if (hash.startsWith('#exam/')) {
    ExamViewPage.show(hash.split('/')[1]);
  } else {
    navigate('#questions');
  }
}

/* ── 全局函数（HTML onclick 调用） ── */
window.app = {
  login(e) {
    return AuthPage.handleLogin(e);
  },

  register(e) {
    return RegisterPage.handleRegister(e);
  },

  logout() {
    // 尝试调用后端退出 API，然后清除本地状态
    API.logout().catch((err) => {
      console.warn('[APP] 登出请求失败：', err);
    });
    API.clearAuth();
    /* 切换用户必须清空会话映射，避免复用上一个用户的 conversation_id */
    ChatPage.resetConversations();
    navigate('#login');
  },

  sendChat() {
    ChatPage.sendMessage();
  },

  cancelChat() {
    ChatPage.cancel();
  },

  reloadQuestions() {
    QuestionsPage.reload();
  },

  retryDetail() {
    ChatPage.retryDetail();
  },

  /* ── 试卷 ── */
  openExams() {
    navigate('#exams');
  },

  reloadExams() {
    ExamsPage.load();
  },

  openExam(id) {
    navigate('#exam/' + id);
  },

  editExam() {
    const hash = window.location.hash || '';
    const id = hash.split('/')[1];
    if (id) navigate('#exam-form/' + id);
  },

  reloadExam() {
    ExamViewPage.load();
  },

  newExam() {
    navigate('#exam-form/new');
  },

  reloadExamForm() {
    ExamFormPage.load();
  },

  saveExam(e) {
    return ExamFormPage.save(e);
  },

  toggleExamStatus() {
    return ExamFormPage.toggleStatus();
  },

  deleteExam() {
    return ExamFormPage.remove();
  },

  examAdd() {
    ExamFormPage.add();
  },

  examMove(index, delta) {
    ExamFormPage.move(index, delta);
  },

  examRemove(index) {
    ExamFormPage.removeItem(index);
  },

  examSetScore(index, value) {
    ExamFormPage.setScore(index, value);
  },

  saveExamArrange() {
    return ExamFormPage.saveArrange();
  },

  navigate
};

/* ── 支持回车发送 ── */
document.addEventListener('DOMContentLoaded', () => {
  $('chatInput').addEventListener('keydown', (e) => {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault();
      ChatPage.sendMessage();
    }
  });

  // 初始路由
  route();
});

/* hash 变化时重新路由 */
window.addEventListener('hashchange', route);
