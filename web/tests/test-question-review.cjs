/* #28 前端验证：投稿/审核契约、回退策略、页面行为、路由集成 */
const fs = require('fs');
const path = require('path');
const webJs = (f) => fs.readFileSync(path.join(__dirname, '..', 'js', f), 'utf8');

let pass = 0, fail = 0;
function check(name, cond, detail) {
  if (cond) { pass++; console.log('PASS', name); }
  else { fail++; console.log('FAIL', name, detail === undefined ? '' : detail); }
}
const tick = () => new Promise((r) => setTimeout(r, 0));

/* ════════ Part 0：index.html 静态检查 ════════ */
{
  const html = fs.readFileSync(path.join(__dirname, '..', 'index.html'), 'utf8');
  const ids = [...html.matchAll(/\bid="([^"]+)"/g)].map((m) => m[1]);
  const seen = new Set();
  const dup = [];
  for (const id of ids) { if (seen.has(id)) dup.push(id); seen.add(id); }
  check('C0 index.html 无重复 id', dup.length === 0, '重复: ' + dup.join(','));
  for (const need of ['page-question-submit', 'page-review', 'questionSubmitNavBtn', 'reviewNavBtn',
    'qsContent', 'qsAnswer', 'qsAnalysis', 'qsKnowledge', 'qsOptionsContainer',
    'reviewTabPending', 'reviewTabRejected', 'reviewList']) {
    check('C0 元素 #' + need + ' 存在且唯一', ids.filter((x) => x === need).length === 1);
  }
}

/* ════════ Part A：api.js 契约与回退 ════════ */
(async () => {
  /* A1: 真实 403（student 查审核队列）原样抛出，不被 mock 掩盖 */
  globalThis.fetch = async () => ({ ok: false, status: 403, json: async () => ({ error: '仅教师和管理员可查看', code: 'FORBIDDEN' }) });
  eval(webJs('api.js') + ';globalThis.__API = API;');
  globalThis.__API.setAuth('t', { id: 3, username: 's', name: 's', role: 'student' });
  try {
    await globalThis.__API.questions({ status: 'pending' });
    check('A1 student 查队列 403 抛出', false, '未抛出');
  } catch (e) {
    check('A1 student 查队列 403 抛出且 status=403', e.status === 403, 'status=' + e.status);
  }

  /* A2: 真实 409（审核非 pending）原样抛出 */
  globalThis.fetch = async () => ({ ok: false, status: 409, json: async () => ({ error: '该题目不在待审核状态', code: 'QUESTION_NOT_PENDING' }) });
  eval(webJs('api.js') + ';globalThis.__API = API;');
  globalThis.__API.setAuth('t', { id: 2, role: 'teacher' });
  try {
    await globalThis.__API.reviewQuestion(1, 'approve');
    check('A2 审核 409 抛出', false, '未抛出');
  } catch (e) {
    check('A2 审核 409 抛出且 status=409', e.status === 409, 'status=' + e.status);
  }

  /* A3: 网络不可达 → 回退 mock */
  globalThis.fetch = async () => { throw new TypeError('Failed to fetch'); };
  eval(webJs('api.js') + ';globalThis.__API = API;');
  globalThis.__API.setAuth('t', { id: 2, role: 'teacher' });
  const list = await globalThis.__API.questions({ status: 'pending' });
  check('A3 网络错误回退 mock 队列', list && Array.isArray(list.questions), JSON.stringify(list).slice(0, 50));

  /* A4: 离线投稿契约：student → pending；teacher → active */
  globalThis.window = { location: { protocol: 'file:' } };
  eval(webJs('api.js') + ';globalThis.__API = API;');
  globalThis.__API.setAuth('t', { id: 3, role: 'student' });
  const submitted = await globalThis.__API.createQuestion({
    content: '学生投稿题', type: 'single_choice',
    options: [{ label: 'A', text: '1' }, { label: 'B', text: '2' }],
    answer: 'A', analysis: '', knowledge_points: ['投稿']
  });
  check('A4a 离线 student 投稿 → pending', submitted.question.status === 'pending', 'status=' + submitted.question.status);
  const activeList = await globalThis.__API.questions();
  check('A4b pending 题不出现在默认列表', !activeList.questions.some((q) => q.id === submitted.question.id));

  globalThis.__API.setAuth('t', { id: 2, role: 'teacher' });
  const created = await globalThis.__API.createQuestion({
    content: '教师建的题', type: 'short_answer', options: [], answer: '略', analysis: '', knowledge_points: []
  });
  check('A4c 离线 teacher 新增 → active', created.question.status === 'active', 'status=' + created.question.status);

  /* A5: 离线队列权限 */
  globalThis.__API.setAuth('t', { id: 3, role: 'student' });
  try {
    await globalThis.__API.questions({ status: 'pending' });
    check('A5a 离线 student 查队列被拒', false, '未抛出');
  } catch (e) {
    check('A5a 离线 student 查队列被拒且 403', e.status === 403, 'status=' + e.status);
  }
  globalThis.__API.setAuth('t', { id: 2, role: 'teacher' });
  const pending = await globalThis.__API.questions({ status: 'pending' });
  check('A5b teacher 待审队列含投稿题', pending.questions.some((q) => q.id === submitted.question.id));

  /* A6: 审核状态机 */
  const approved = await globalThis.__API.reviewQuestion(submitted.question.id, 'approve');
  check('A6a approve → active', approved.question.status === 'active', 'status=' + approved.question.status);
  try {
    await globalThis.__API.reviewQuestion(submitted.question.id, 'approve');
    check('A6b 重复审核被拒', false, '未抛出');
  } catch (e) {
    check('A6b 重复审核被拒且 409', e.status === 409, 'status=' + e.status);
  }

  /* 待审题必须由 student 投稿产生（teacher 建的直接 active） */
  globalThis.__API.setAuth('t', { id: 3, role: 'student' });
  const rej = await globalThis.__API.createQuestion({
    content: '将被驳回的题', type: 'short_answer', options: [], answer: 'x', analysis: '', knowledge_points: []
  });
  check('A6c student 投稿进入待审', rej.question.status === 'pending', 'status=' + rej.question.status);
  globalThis.__API.setAuth('t', { id: 2, role: 'teacher' });
  const rejected = await globalThis.__API.reviewQuestion(rej.question.id, 'reject');
  check('A6d reject → rejected 终态', rejected.question.status === 'rejected');
  const rejectedList = await globalThis.__API.questions({ status: 'rejected' });
  check('A6d rejected 队列可见该题', rejectedList.questions.some((q) => q.id === rej.question.id));

  /* A7: 非法 action */
  try {
    await globalThis.__API.reviewQuestion(submitted.question.id, 'maybe');
    check('A7 非法 action 被拒', false, '未抛出');
  } catch (e) {
    check('A7 非法 action 被拒且 400', e.status === 400, 'status=' + e.status);
  }

  await partB();
})();

/* ════════ Part B：页面行为 + 路由 ════════ */
async function partB() {
  delete globalThis.window;
  const elements = new Map();
  function makeEl(id) {
    const classes = new Set();
    const el = {
      id, value: '', textContent: '', disabled: false,
      children: [],
      classList: {
        add: (c) => classes.add(c), remove: (c) => classes.delete(c),
        toggle: (c, f) => { const on = f === undefined ? !classes.has(c) : !!f; if (on) classes.add(c); else classes.delete(c); },
        contains: (c) => classes.has(c)
      },
      isHidden: () => classes.has('hidden'),
      appendChild: (child) => { el.children.push(child); return child; },
      addEventListener: () => {}
    };
    let _innerHTML = '';
    Object.defineProperty(el, 'innerHTML', {
      get: () => _innerHTML,
      set: (v) => { _innerHTML = v; if (v === '') el.children.length = 0; }
    });
    return el;
  }
  globalThis.document = {
    readyState: 'loading',
    getElementById: (id) => { if (!elements.has(id)) elements.set(id, makeEl(id)); return elements.get(id); },
    querySelectorAll: () => [],
    createElement: () => makeEl('dyn'),
    addEventListener: () => {}
  };
  globalThis.window = { location: { hash: '' }, addEventListener: () => {} };

  let currentUser = null;
  const calls = { create: [], review: [], listByStatus: [] };
  let pendingFixture = { question: null };
  globalThis.API = {
    getToken: () => currentUser ? 'tok' : null,
    getUser: () => currentUser,
    setAuth: () => {}, clearAuth: () => {},
    logout: () => Promise.resolve({}),
    questions: (opts) => {
      if (opts && opts.status) {
        calls.listByStatus.push(opts.status);
        return Promise.resolve({ questions: [pendingFixture.question].filter(Boolean) });
      }
      return Promise.resolve({ questions: [] });
    },
    questionDetail: () => Promise.resolve({ question: {} }),
    createQuestion: (payload) => { calls.create.push(payload); return Promise.resolve({ question: { id: 99, status: 'pending' } }); },
    reviewQuestion: (id, action) => { calls.review.push([id, action]); return Promise.resolve({ question: { id, status: 'active' } }); }
  };
  globalThis.ChatPage = { show() {}, resetConversations() {}, sendMessage() {}, cancel() {} };
  globalThis.AuthPage = { show() {}, handleLogin() {} };
  globalThis.RegisterPage = { show() {}, handleRegister() {} };
  globalThis.TeacherApiPage = { show() {}, load() {}, save() {}, toggle() {} };

  /* 浏览器多 script 共享作用域 → 按 index.html 顺序拼接一次 eval */
  eval(webJs('utils.js') + webJs('questions.js') + webJs('question-submit.js') + webJs('review.js') + webJs('app.js')
    + ';globalThis.__route = route; globalThis.__QS = QuestionSubmitPage; globalThis.__RP = ReviewPage;');
  const el = (id) => elements.get(id);
  const qs = globalThis.__QS;
  const rp = globalThis.__RP;

  /* B1/B2: 导航按钮显隐 */
  currentUser = { id: 3, role: 'student' };
  globalThis.window.location.hash = '#questions';
  globalThis.__route();
  check('B1a student 可见「投稿」入口', !el('questionSubmitNavBtn').isHidden());
  check('B1b student 不见「审核」入口', el('reviewNavBtn').isHidden());

  currentUser = { id: 2, role: 'teacher' };
  globalThis.__route();
  check('B2a teacher 可见「审核」入口', !el('reviewNavBtn').isHidden());
  check('B2b teacher 也可见「投稿」入口', !el('questionSubmitNavBtn').isHidden());

  /* B3: student 直接访问 #review → 无权限提示 */
  currentUser = { id: 3, role: 'student' };
  rp.show();
  check('B3 student 进审核页显示无权限', /仅教师/.test(el('reviewErrorText').textContent), el('reviewErrorText').textContent);

  /* B4: teacher 审核页渲染（XSS 转义 + 按钮存在） */
  currentUser = { id: 2, role: 'teacher' };
  pendingFixture.question = {
    id: 7, content: '<script>x</script> 学生投稿题', type: 'single_choice',
    knowledge_points: ['一元一次方程'], options: [{ label: 'A', text: '1' }], answer: 'A', analysis: '解析'
  };
  rp.show();
  await tick(); await tick();
  const html = el('reviewList').innerHTML;
  check('B4a 待审队列渲染题干且 XSS 转义', html.includes('&lt;script&gt;') && !html.includes('<script>'));
  check('B4b 通过/驳回按钮存在', html.includes('ReviewPage.approve(7)') && html.includes('ReviewPage.reject(7)'));

  /* B5: approve 调 API 并刷新列表 */
  calls.review.length = 0; calls.listByStatus.length = 0;
  await rp.approve(7);
  check('B5 approve 调用 reviewQuestion(7, approve)', JSON.stringify(calls.review) === '[[7,"approve"]]', JSON.stringify(calls.review));
  check('B5b 审核后刷新队列', calls.listByStatus.includes('pending'));

  /* B6: 学生投稿页提交 → pending 提示 */
  currentUser = { id: 3, role: 'student' };
  qs.show();
  el('qsContent').value = '我的投稿';
  el('qsAnswer').value = 'A';
  /* 模拟已有一个选项行：直接调用内部不可行，走 collectPayload 无选项 → mock 校验在前端不做，API stub 不校验 */
  await qs.submit();
  check('B6a 提交调用 createQuestion', calls.create.length === 1, 'calls=' + calls.create.length);
  check('B6b 显示待审核提示', /等待教师审核/.test(el('questionSubmitMsg').textContent), el('questionSubmitMsg').textContent);

  /* B7: 路由切换无残留 */
  currentUser = { id: 2, role: 'teacher' };
  globalThis.window.location.hash = '#review';
  globalThis.__route();
  check('B7a #review 显示审核页', !el('page-review').isHidden());
  globalThis.window.location.hash = '#questions';
  globalThis.__route();
  check('B7b 回列表后审核页隐藏', el('page-review').isHidden() && el('page-question-submit').isHidden());

  console.log('\n结果：' + pass + ' 通过 / ' + fail + ' 失败');
  process.exit(fail ? 1 : 0);
}
