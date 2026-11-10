/* ── API 客户端 ──
 *
 * 两种运行模式，自动切换，调用方无需关心：
 *
 *   1. 在线模式：页面由 C 后端托管（http://host:8000），正常请求 /api/v1/*
 *   2. 离线演示模式：直接双击 index.html（file:// 协议）或后端未启动时，
 *      浏览器会禁止 fetch 并抛 TypeError: Failed to fetch。此时自动回退到
 *      内置演示数据，保证登录 → 题目列表 → 对话页整条链路可完整演示。
 *
 * 离线数据严格对齐 docs/auth.md（三个 demo 账号）与 docs/questions.md（5 道固定题）。
 */
const API = (() => {
  const BASE = '/api/v1';

  /* file:// 协议下 fetch 必然失败，直接判定为离线，省去一次无谓请求 */
  const OFFLINE_PROTOCOL =
    (typeof window !== 'undefined') && window.location.protocol === 'file:';

  /* ── 存储：file:// 下 localStorage 可能受限，降级到内存 ── */
  const memoryStore = {};

  function storeGet(key) {
    try {
      return localStorage.getItem(key);
    } catch (e) {
      return Object.prototype.hasOwnProperty.call(memoryStore, key) ? memoryStore[key] : null;
    }
  }

  function storeSet(key, value) {
    try {
      localStorage.setItem(key, value);
    } catch (e) {
      memoryStore[key] = value;
    }
  }

  function storeDel(key) {
    try {
      localStorage.removeItem(key);
    } catch (e) {
      delete memoryStore[key];
    }
  }

  /** 获取存储的 token */
  function getToken() {
    return storeGet('token');
  }

  /** 获取当前用户信息 */
  function getUser() {
    const raw = storeGet('user');
    if (!raw) return null;
    try {
      return JSON.parse(raw);
    } catch (e) {
      return null;
    }
  }

  /** 保存登录状态 */
  function setAuth(token, user) {
    storeSet('token', token);
    storeSet('user', JSON.stringify(user));
  }

  /** 清除登录状态 */
  function clearAuth() {
    storeDel('token');
    storeDel('user');
  }

  /** 判断异常是否属于「后端不可达」——这类错误应回退到演示数据 */
  function isUnreachable(err) {
    if (!err) return false;
    if (err.networkError) return true;
    const msg = String(err.message || '');
    return msg.indexOf('Failed to fetch') !== -1 ||
           msg.indexOf('NetworkError') !== -1 ||
           msg.indexOf('Load failed') !== -1;   // Safari 的措辞
  }

  /** 判断异常是否属于「接口尚未实现」——例如 chat 接口还没上线时的 404 */
 function isNotImplemented(err) {
   if (!err) return false;
   /* 优先按 HTTP 状态码判断，比匹配错误文案可靠：
    *   404 接口不存在 / 405 方法不允许 / 501 服务端未实现该方法
    * 注意不含 500：那是后端真实故障，必须暴露出来，不能被演示数据掩盖。 */
   if (err.status === 404 || err.status === 405 || err.status === 501) {
     return true;
   }
   const msg = String(err.message || '');
   return msg.indexOf('api not found') !== -1 ||
          msg.indexOf('404') !== -1 ||
          msg.indexOf('405') !== -1 ||
          msg.indexOf('501') !== -1 ||
          msg.indexOf('Not Found') !== -1 ||
          msg.indexOf('method not allowed') !== -1 ||
          msg.indexOf('Unsupported method') !== -1;
 }

  /** 通用 fetch 封装 */
  async function request(method, path, body) {
    const url = BASE + path;
    const headers = { 'Content-Type': 'application/json' };
    const token = getToken();
    if (token) {
      headers['Authorization'] = 'Bearer ' + token;
    }
    const opts = { method, headers };
    if (body !== undefined) {
      opts.body = JSON.stringify(body);
    }

    let res;
    try {
      res = await fetch(url, opts);
    } catch (netErr) {
      /* 网络层失败：file:// 协议、后端没启动、连接被拒。
       * 打上标记，让上层决定是回退演示数据还是直接报错。 */
      const wrapped = new Error(netErr && netErr.message ? netErr.message : '网络请求失败');
      wrapped.networkError = true;
      throw wrapped;
    }

    /* 响应体可能不是 JSON（例如反向代理返回 HTML 错误页），单独兜住 */
    let data;
    try {
      data = await res.json();
    } catch (e) {
      data = {};
    }

    if (!res.ok) {
      /* 把状态码挂到异常上，供 isNotImplemented 精确判断 */
      const err = new Error(data.error || ('请求失败 (' + res.status + ')'));
      err.status = res.status;
      throw err;
    }
    return data;
  }

  /** 对话接口是否真的回退到了演示数据（http:// 下也会发生，因为后端
   *  /api/v1/chat 尚未实现会返回 404）。页面据此提示，避免把内置回复
   *  当成真实 AI 输出展示。 */
  let chatMocked = false;

  /** 统一回退包装：真实请求失败且属于可回退错误时，改用演示数据 */
  async function withFallback(realCall, mockCall, onFallback) {
    if (OFFLINE_PROTOCOL) {
      if (onFallback) onFallback();
      return mockCall();
    }
    try {
      return await realCall();
    } catch (e) {
      if (isUnreachable(e) || isNotImplemented(e)) {
        if (onFallback) onFallback();
        return mockCall();
      }
      throw e;
    }
  }

  /** 论文/业务型资源（如试卷）专用回退：只在「后端不可达 / file://」时用演示数据，
   *  404（资源不存在）、403、409（已归档）必须原样抛给页面，不能被演示数据掩盖。 */
  async function withOfflineFallback(realCall, mockCall) {
    if (OFFLINE_PROTOCOL) return mockCall();
    try {
      return await realCall();
    } catch (e) {
      if (isUnreachable(e)) return mockCall();
      throw e;
    }
  }

  /* ══════════════════════════════════════════════════════════
   *  离线演示数据
   *  账号来源：docs/auth.md 第 4 节
   *  题目来源：docs/questions.md 第 1 节（5 道固定题）
   * ══════════════════════════════════════════════════════════ */

  const MOCK_USERS = [
    { username: 'admin',   password: 'admin123',   id: 1, name: '系统管理员', role: 'admin' },
    { username: 'teacher', password: 'teacher123', id: 2, name: '张老师',     role: 'teacher' },
    { username: 'student', password: 'student123', id: 3, name: '李同学',     role: 'student' }
  ];

  const MOCK_QUESTIONS = [
    {
      id: 1,
      content: '解方程：2x + 3 = 11，x 的值是多少？',
      options: [
        { label: 'A', text: '2' },
        { label: 'B', text: '3' },
        { label: 'C', text: '4' },
        { label: 'D', text: '5' }
      ],
      answer: 'C',
      analysis: '两边减去 3 得到 2x = 8，再除以 2，得到 x = 4。',
      knowledge_points: ['一元一次方程', '等式性质'],
      type: 'single_choice'
    },
    {
      id: 2,
      content: '求函数 f(x) = x² 的导数 f\'(x)。',
      options: [
        { label: 'A', text: '2x' },
        { label: 'B', text: 'x' },
        { label: 'C', text: 'x²' },
        { label: 'D', text: '2x²' }
      ],
      answer: 'A',
      analysis: '由幂函数求导公式 (xⁿ)\' = n·xⁿ⁻¹，得 f\'(x) = 2x。',
      knowledge_points: ['导数', '幂函数求导'],
      type: 'single_choice'
    },
    {
      id: 3,
      content: '求极限 lim(x→0) sin(x)/x 的值。',
      options: [
        { label: 'A', text: '0' },
        { label: 'B', text: '1' },
        { label: 'C', text: '∞' },
        { label: 'D', text: '不存在' }
      ],
      answer: 'B',
      analysis: '这是第一个重要极限，弧度制下 x→0 时 sin(x)/x → 1。',
      knowledge_points: ['极限', '重要极限'],
      type: 'single_choice'
    },
    {
      id: 4,
      content: '质量为 2 kg 的物体产生 3 m/s² 的加速度，所受合力是多少？',
      options: [
        { label: 'A', text: '5 N' },
        { label: 'B', text: '1.5 N' },
        { label: 'C', text: '6 N' },
        { label: 'D', text: '9 N' }
      ],
      answer: 'C',
      analysis: '由牛顿第二定律 F = ma = 2 × 3 = 6 N。',
      knowledge_points: ['牛顿第二定律', '力学'],
      type: 'single_choice'
    },
    {
      id: 5,
      content: 'C 语言中 sizeof(char) 的值是多少？',
      options: [
        { label: 'A', text: '1' },
        { label: 'B', text: '2' },
        { label: 'C', text: '4' },
        { label: 'D', text: '8' }
      ],
      answer: 'A',
      analysis: 'C 标准规定 sizeof(char) 恒为 1，单位是字节而非位。',
      knowledge_points: ['C语言', 'sizeof'],
      type: 'single_choice'
    }
  ];

  const MOCK_REPLIES = {
    1: '根据一元一次方程的解法：\n\n步骤 1：将常数项移到等号右边\n2x = 11 - 3\n2x = 8\n\n步骤 2：两边同时除以系数\nx = 8 ÷ 2\nx = 4\n\n所以 x = 4，选择 C 选项。',
    2: '根据幂函数求导公式 (x^n)\' = n·x^(n-1)：\n\nf(x) = x^2\nf\'(x) = 2·x^(2-1) = 2x\n\n所以答案为 f\'(x) = 2x，选择 A 选项。',
    3: '这是微积分中非常重要的第一个重要极限。\n\n在弧度制下，当 x → 0 时，sin(x)/x → 1。\n\n直观理解：当角度很小时，正弦值约等于弧度值，比值趋近于 1。\n\n所以选择 B 选项。',
    4: '根据牛顿第二定律：\n\n公式：F = ma\n其中 m = 2 kg，a = 3 m/s²\n\nF = 2 × 3 = 6 N\n\n所以所受合力为 6 N，选择 C 选项。',
    5: '在 C 语言中：\n\n· C 标准明确规定 sizeof(char) 的值为 1\n· 这个 "1" 是指 1 个字节（byte）\n· 一个字节有多少位由 CHAR_BIT 宏定义（通常为 8）\n· 但 sizeof 返回的是字节数，不是位数\n\n所以答案为 A 选项。'
  };

  /** 演示数据也模拟网络延迟，让加载态可见，避免界面闪跳 */
  function delay(ms) {
    return new Promise((resolve) => setTimeout(resolve, ms));
  }

  function mockLogin(username, password) {
    return delay(400).then(() => {
      const hit = MOCK_USERS.filter((u) => u.username === username)[0];
      /* 对齐 docs/auth.md：用户名不存在与密码错误统一返回 401，防枚举 */
      if (!hit || hit.password !== password) {
        throw new Error('用户名或密码错误');
      }
      return {
        token: 'demo-offline-token-' + Date.now(),
        user: { id: hit.id, username: hit.username, name: hit.name, role: hit.role }
      };
    });
  }

  /* 离线注册：校验规则与 docs/auth.md 2.1 的 400/409 契约保持一致。
   * 注册成功写回 MOCK_USERS，保证离线模式下新账号可以立即登录。 */
  function mockRegister(payload) {
    return delay(400).then(() => {
      const username = String(payload.username || '').trim();
      const password = String(payload.password || '');
      const confirm = String(payload.confirm_password || '');
      const role = String(payload.role || 'student');

      if (!username) {
        throw new Error('用户名不能为空');
      }
      if (password !== confirm) {
        throw new Error('两次输入的密码不一致');
      }
      if (password.length < 8 || !/[A-Za-z]/.test(password) || !/[0-9]/.test(password)) {
        throw new Error('密码至少 8 位，且必须包含字母和数字');
      }
      if (['student', 'teacher', 'admin'].indexOf(role) === -1) {
        throw new Error('角色值无效，仅支持 student/teacher/admin');
      }
      if (MOCK_USERS.filter((u) => u.username === username).length > 0) {
        const err = new Error('用户名已存在');
        err.status = 409;
        throw err;
      }

      const user = {
        id: MOCK_USERS.length + 1,
        username: username,
        name: username,
        role: role,
        password: password
      };
      MOCK_USERS.push(user);
      return {
        token: 'demo-offline-token-' + Date.now(),
        user: { id: user.id, username: user.username, name: user.name, role: user.role }
      };
    });
  }

  function mockQuestions() {
    return delay(300).then(() => ({ questions: MOCK_QUESTIONS.slice() }));
  }

  function mockQuestionDetail(id) {
    return delay(300).then(() => {
      const num = parseInt(id, 10);
      const hit = MOCK_QUESTIONS.filter((q) => q.id === num)[0];
      if (!hit) {
        /* 对齐 docs/questions.md 的 404 契约 */
        throw new Error('question not found');
      }
      return { question: hit };
    });
  }

  /* ── 试卷演示数据（对齐 docs/exams.md 的试卷对象与错误码） ── */
  let examSeq = 3;
  const MOCK_EXAMS = [
    {
      id: 1,
      name: '期中数学测验',
      description: '覆盖一元一次方程与导数',
      teacher_id: 2,
      status: 'active',
      created_at: 1760000000,
      updated_at: 1760000000,
      questions: [{ question_id: 1, position: 1, score: 5 }, { question_id: 2, position: 2, score: 10 }]
    },
    {
      id: 2,
      name: '物理随堂小测',
      description: '牛顿第二定律',
      teacher_id: 2,
      status: 'archived',
      created_at: 1760001000,
      updated_at: 1760001000,
      questions: [{ question_id: 4, position: 1, score: 8 }]
    }
  ];

  function examError(status, code, message) {
    const err = new Error(message);
    err.status = status;
    err.code = code;
    return err;
  }

  function currentRole() {
    const user = getUser();
    return user ? user.role : null;
  }

  function findExam(id) {
    const num = parseInt(id, 10);
    return MOCK_EXAMS.filter((item) => item.id === num)[0] || null;
  }

  function visibleExam(id) {
    const exam = findExam(id);
    /* archived 对 student 一律 404，不泄露存在性 */
    if (!exam || (currentRole() === 'student' && exam.status !== 'active')) {
      throw examError(404, 'EXAM_NOT_FOUND', '试卷不存在');
    }
    return exam;
  }

  function writableExam(id) {
    if (currentRole() === 'student') throw examError(403, 'EXAM_FORBIDDEN', '学生无权修改试卷');
    const exam = findExam(id);
    if (!exam) throw examError(404, 'EXAM_NOT_FOUND', '试卷不存在');
    if (exam.status === 'archived') throw examError(409, 'EXAM_ARCHIVED', '试卷已归档，不能编辑');
    return exam;
  }

  function inlineQuestions(exam) {
    return exam.questions.map((entry) => {
      const q = MOCK_QUESTIONS.filter((item) => item.id === entry.question_id)[0] || {};
      return {
        question_id: entry.question_id,
        position: entry.position,
        score: entry.score,
        content: q.content || '',
        type: q.type || 'single_choice',
        options: Array.isArray(q.options) ? q.options.slice() : [],
        knowledge_points: Array.isArray(q.knowledge_points) ? q.knowledge_points.slice() : [],
        answer: q.answer || '',
        analysis: q.analysis || ''
      };
    });
  }

  function examDetailObject(exam) {
    return {
      id: exam.id,
      name: exam.name,
      description: exam.description,
      teacher_id: exam.teacher_id,
      status: exam.status,
      created_at: exam.created_at,
      updated_at: exam.updated_at,
      questions: inlineQuestions(exam)
    };
  }

  function examListItem(exam) {
    return {
      id: exam.id,
      name: exam.name,
      description: exam.description,
      teacher_id: exam.teacher_id,
      status: exam.status,
      created_at: exam.created_at,
      updated_at: exam.updated_at,
      question_count: exam.questions.length
    };
  }

  function validateExamMeta(patch, base) {
    const name = patch.name !== undefined ? patch.name : (base ? base.name : '');
    if (typeof name !== 'string' || !name.trim() || Array.from(name).length > 100) {
      throw examError(400, 'EXAM_INVALID_REQUEST', '试卷名称需为 1–100 个字符');
    }
    const description = patch.description !== undefined ? patch.description : (base ? base.description : '');
    if (typeof description !== 'string' || Array.from(description).length > 1000) {
      throw examError(400, 'EXAM_INVALID_REQUEST', '描述不能超过 1000 个字符');
    }
    return { name: name.trim(), description: description };
  }

  function validateScore(score) {
    const value = Number(score);
    if (!isFinite(value) || value <= 0 || value > 1000) {
      throw examError(400, 'EXAM_INVALID_REQUEST', '分值需大于 0 且不超过 1000');
    }
    return value;
  }

  function touchExam(exam) {
    exam.updated_at = Math.floor(Date.now() / 1000);
  }

  function mockExamList() {
    return delay(300).then(() => {
      const role = currentRole();
      const visible = MOCK_EXAMS.filter((exam) => role !== 'student' || exam.status === 'active');
      return { exams: visible.slice().sort((a, b) => a.id - b.id).map(examListItem) };
    });
  }

  function mockExamDetail(id) {
    return delay(250).then(() => ({ exam: examDetailObject(visibleExam(id)) }));
  }

  function mockExamCreate(payload) {
    return delay(250).then(() => {
      if (currentRole() === 'student') throw examError(403, 'EXAM_FORBIDDEN', '学生无权创建试卷');
      const meta = validateExamMeta(payload || {}, null);
      const now = Math.floor(Date.now() / 1000);
      const exam = {
        id: examSeq++,
        name: meta.name,
        description: meta.description,
        teacher_id: 2,
        status: 'active',
        created_at: now,
        updated_at: now,
        questions: []
      };
      MOCK_EXAMS.push(exam);
      return { exam: examDetailObject(exam) };
    });
  }

  function mockExamUpdate(id, payload) {
    return delay(250).then(() => {
      const exam = writableExam(id);
      const meta = validateExamMeta(payload || {}, exam);
      exam.name = meta.name;
      exam.description = meta.description;
      touchExam(exam);
      return { exam: examDetailObject(exam) };
    });
  }

  function mockExamStatus(id, status) {
    return delay(200).then(() => {
      if (currentRole() === 'student') throw examError(403, 'EXAM_FORBIDDEN', '学生无权修改试卷');
      if (status !== 'active' && status !== 'archived') {
        throw examError(400, 'EXAM_INVALID_REQUEST', '状态只能为 active 或 archived');
      }
      const exam = findExam(id);
      if (!exam) throw examError(404, 'EXAM_NOT_FOUND', '试卷不存在');
      exam.status = status;
      touchExam(exam);
      return { exam: examDetailObject(exam) };
    });
  }

  function normalizeQuestions(list) {
    if (!Array.isArray(list)) throw examError(400, 'EXAM_INVALID_REQUEST', '题目列表格式不正确');
    if (list.length > 200) throw examError(400, 'EXAM_INVALID_REQUEST', '单卷题目不能超过 200 道');
    const seen = {};
    return list.map((item, index) => {
      const qid = parseInt(item.question_id, 10);
      if (!MOCK_QUESTIONS.some((q) => q.id === qid)) {
        throw examError(400, 'EXAM_INVALID_QUESTION', '引用了不存在的题目');
      }
      if (seen[qid]) throw examError(400, 'EXAM_INVALID_LAYOUT', '同一试卷内题目不能重复');
      seen[qid] = true;
      return { question_id: qid, position: index + 1, score: validateScore(item.score) };
    });
  }

  function mockExamReplaceQuestions(id, questions) {
    return delay(250).then(() => {
      const exam = writableExam(id);
      exam.questions = normalizeQuestions(questions);
      touchExam(exam);
      return { exam: examDetailObject(exam) };
    });
  }

  function mockExamAddQuestion(id, payload) {
    return delay(250).then(() => {
      const exam = writableExam(id);
      const qid = parseInt((payload || {}).question_id, 10);
      if (!MOCK_QUESTIONS.some((q) => q.id === qid)) {
        throw examError(400, 'EXAM_INVALID_QUESTION', '引用了不存在的题目');
      }
      if (exam.questions.some((item) => item.question_id === qid)) {
        throw examError(400, 'EXAM_INVALID_LAYOUT', '该题已在试卷中');
      }
      if (exam.questions.length >= 200) {
        throw examError(400, 'EXAM_INVALID_REQUEST', '单卷题目不能超过 200 道');
      }
      exam.questions.push({
        question_id: qid,
        position: exam.questions.length + 1,
        score: validateScore(payload.score)
      });
      touchExam(exam);
      return { exam: examDetailObject(exam) };
    });
  }

  function mockExamRemoveQuestion(id, questionId) {
    return delay(250).then(() => {
      const exam = writableExam(id);
      const qid = parseInt(questionId, 10);
      const index = exam.questions.findIndex((item) => item.question_id === qid);
      if (index < 0) throw examError(404, 'EXAM_NOT_FOUND', '该题不在试卷中');
      exam.questions.splice(index, 1);
      /* 移除后自动重排为 1..n */
      exam.questions.forEach((item, i) => { item.position = i + 1; });
      touchExam(exam);
      return { exam: examDetailObject(exam) };
    });
  }

  function mockExamDelete(id) {
    return delay(250).then(() => {
      if (currentRole() === 'student') throw examError(403, 'EXAM_FORBIDDEN', '学生无权删除试卷');
      const exam = findExam(id);
      if (!exam) throw examError(404, 'EXAM_NOT_FOUND', '试卷不存在');
      MOCK_EXAMS.splice(MOCK_EXAMS.indexOf(exam), 1);
      return { status: 'ok' };
    });
  }

  function mockChat(questionId, message, { mode, onStatus } = {}) {
    const modeLabel = { solve: '完整讲解', hint: '分层提示', review: '错因分析' }[mode] || '完整讲解';
    return delay(1200).then(() => {
      const base = MOCK_REPLIES[questionId] ||
        `这是题目 ${questionId} 的解答（演示模式）。\n\n你的问题：${message}\n\n后端对话接口尚未就绪，当前为前端内置的模拟回复。`;
      const created = {
        job_id: '00000000-0000-0000-0000-000000000000',
        conversation_id: '00000000-0000-0000-0000-000000000001',
        question_id: questionId,
        mode: mode || 'solve',
        status: 'succeeded',
        reply: base + '\n\n【演示模式】教学模式：' + modeLabel,
        model: 'mock'
      };
      if (typeof onStatus === 'function') onStatus(created);
      return created;
    });
  }

  function mockMe() {
    return delay(150).then(() => {
      const user = getUser();
      if (!user) throw new Error('未登录或 token 已过期');
      return { user: user };
    });
  }

  /* ── 对外接口（签名与在线模式完全一致） ── */
  return {
    getToken,
    getUser,
    setAuth,
    clearAuth,

    /** 当前是否处于离线演示模式，供页面提示使用 */
    isOfflineDemo() {
      return OFFLINE_PROTOCOL;
    },

    /** 对话回复是否来自内置演示数据（离线或后端 chat 接口未就绪时为 true） */
    isChatMocked() {
      return chatMocked;
    },

    /** 登录 POST /auth/login */
    login(username, password) {
      return withFallback(
        () => request('POST', '/auth/login', { username: username, password: password }),
        () => mockLogin(username, password)
      );
    },

    /** 注册 POST /auth/register（成功即自动登录，返回 token + user） */
    register(username, password, confirmPassword, role) {
      const payload = {
        username: username,
        password: password,
        confirm_password: confirmPassword
      };
      if (role) {
        payload.role = role;
      }
      return withFallback(
        () => request('POST', '/auth/register', payload),
        () => mockRegister(payload)
      );
    },

    /** 退出 POST /auth/logout（离线模式直接成功，本地状态由调用方清除） */
    logout() {
      return withFallback(
        () => request('POST', '/auth/logout'),
        () => delay(100).then(() => ({ status: 'ok' }))
      );
    },

    /** 获取当前用户 GET /auth/me */
    me() {
      return withFallback(() => request('GET', '/auth/me'), mockMe);
    },

    /** 获取题目列表 GET /questions */
    questions() {
      return withFallback(() => request('GET', '/questions'), mockQuestions);
    },

    /** 获取题目详情 GET /questions/{id} */
    questionDetail(id) {
      return withFallback(
        () => request('GET', '/questions/' + id),
        () => mockQuestionDetail(id)
      );
    },

    /** 查询 AI 使用权限 GET /ai/access（v1.2）
     * 后端返回 { ai_access: {...} }，这里提取内部对象 */
    aiAccess() {
      return withFallback(
        () => request('GET', '/ai/access').then((body) => body.ai_access || {}),
        () => delay(150).then(() => ({ status: 'active', allowed: true, expires_at: null }))
      );
    },

    /* ── 试卷（F03）GET/POST/PUT/PATCH/DELETE /exams... 契约见 docs/exams.md ── */

    /** 试卷列表 GET /exams（admin 全部 / teacher 本人 / student 仅 active） */
    exams() {
      return withOfflineFallback(
        () => request('GET', '/exams').then((body) => body.exams || []),
        () => mockExamList().then((body) => body.exams)
      );
    },

    /** 试卷详情 GET /exams/{id}（含内联题目，按 position 升序） */
    examDetail(id) {
      return withOfflineFallback(
        () => request('GET', '/exams/' + id).then((body) => body.exam),
        () => mockExamDetail(id).then((body) => body.exam)
      );
    },

    /** 新建试卷 POST /exams（仅 name / description） */
    createExam(payload) {
      return withOfflineFallback(
        () => request('POST', '/exams', payload).then((body) => body.exam),
        () => mockExamCreate(payload).then((body) => body.exam)
      );
    },

    /** 修改元数据 PUT /exams/{id}（部分更新） */
    updateExam(id, payload) {
      return withOfflineFallback(
        () => request('PUT', '/exams/' + id, payload).then((body) => body.exam),
        () => mockExamUpdate(id, payload).then((body) => body.exam)
      );
    },

    /** 状态切换 PATCH /exams/{id}（active / archived） */
    setExamStatus(id, status) {
      return withOfflineFallback(
        () => request('PATCH', '/exams/' + id, { status: status }).then((body) => body.exam),
        () => mockExamStatus(id, status).then((body) => body.exam)
      );
    },

    /** 整体替换题目编排 PUT /exams/{id}/questions（数组顺序即 position） */
    replaceExamQuestions(id, questions) {
      return withOfflineFallback(
        () => request('PUT', '/exams/' + id + '/questions', { questions: questions }).then((body) => body.exam),
        () => mockExamReplaceQuestions(id, questions).then((body) => body.exam)
      );
    },

    /** 追加单题 POST /exams/{id}/questions */
    addExamQuestion(id, payload) {
      return withOfflineFallback(
        () => request('POST', '/exams/' + id + '/questions', payload).then((body) => body.exam),
        () => mockExamAddQuestion(id, payload).then((body) => body.exam)
      );
    },

    /** 移除单题 DELETE /exams/{id}/questions/{qid}（后端自动重排 position） */
    removeExamQuestion(id, questionId) {
      return withOfflineFallback(
        () => request('DELETE', '/exams/' + id + '/questions/' + questionId).then((body) => body.exam),
        () => mockExamRemoveQuestion(id, questionId).then((body) => body.exam)
      );
    },

    /** 删除试卷 DELETE /exams/{id}（不受 archived 限制） */
    deleteExam(id) {
      return withOfflineFallback(
        () => request('DELETE', '/exams/' + id),
        () => mockExamDelete(id)
      );
    },

    /** 发送对话 POST /chat（v1.2 异步任务，自动轮询）
     * opts: { questionId, message, conversationId, mode, studentAnswer, onStatus, signal } */
    chat(opts) {
      const { questionId, message, conversationId, mode, studentAnswer, onStatus, signal } = opts || {};
      return withFallback(
        () => PiChatClient.sendPiChat({
          questionId, message, conversationId, token: getToken(),
          mode, studentAnswer, onStatus,
          signal
        }),
        () => mockChat(questionId, message, { mode, onStatus }),
        () => { chatMocked = true; }
      );
    }
  };
})();
