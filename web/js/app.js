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
  /* 教师 AI 配置页同理，只在 #teacher-api 下显示 */
  $('page-teacher-api').classList.add('hidden');
  /* 投稿页与审核页同理，只在对应 hash 下显示 */
  $('page-question-submit').classList.add('hidden');
  $('page-review').classList.add('hidden');
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
  /* 「AI 配置」入口只对教师显示：该接口对 student/admin 一律 403 */
  $('teacherApiNavBtn').classList.toggle('hidden', !(user && user.role === 'teacher'));
  /* 「投稿」对学生开放（teacher/admin 提交直接生效，也可用）；
   * 「审核」仅 teacher/admin：status 队列与 review 接口对 student 一律 403 */
  $('questionSubmitNavBtn').classList.toggle('hidden', !user);
  $('reviewNavBtn').classList.toggle('hidden', !(user && (user.role === 'teacher' || user.role === 'admin')));

  if (hash === '#questions') {
    QuestionsPage.show();
  } else if (hash.startsWith('#chat/')) {
    const id = hash.split('/')[1];
    ChatPage.show(id);
  } else if (hash === '#question-submit') {
    QuestionSubmitPage.show();
  } else if (hash === '#review') {
    ReviewPage.show();
  } else if (hash === '#teacher-api') {
    TeacherApiPage.show();
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

  openTeacherApi() {
    navigate('#teacher-api');
  },

  openQuestionSubmit() {
    navigate('#question-submit');
  },

  openReview() {
    navigate('#review');
  },

  reloadTeacherApi() {
    TeacherApiPage.load();
  },

  saveTeacherApi(e) {
    return TeacherApiPage.save(e);
  },

  toggleTeacherApi() {
    return TeacherApiPage.toggle();
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
