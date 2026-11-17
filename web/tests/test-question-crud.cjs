/* #18 重建验证：CRUD 严格回退 / 权限 / 页面行为 / 路由集成 */
const fs = require('fs');
const path = require('path');
const webJs = (f) => fs.readFileSync(path.join(__dirname, '..', 'js', f), 'utf8');

let pass = 0, fail = 0;
function check(name, cond, detail) {
  if (cond) { pass++; console.log('PASS', name); }
  else { fail++; console.log('FAIL', name, detail === undefined ? '' : detail); }
}
const tick = () => new Promise((r) => setTimeout(r, 0));

/* ════════ Part 0：index.html 静态检查（防 id 重复回归，见 PR #29 review） ════════
 * 页面脚本的 getElementById 取文档中第一个匹配；表单页若复用详情页已有的
 * id，会让 renderDetail 把内容写进隐藏的表单控件。此处直接解析 index.html
 * 做唯一性断言，不依赖 DOM 环境。 */
{
  const html = fs.readFileSync(path.join(__dirname, '..', 'index.html'), 'utf8');
  const ids = [...html.matchAll(/\bid="([^"]+)"/g)].map((m) => m[1]);
  const seen = new Set();
  const dup = [];
  for (const id of ids) { if (seen.has(id)) dup.push(id); seen.add(id); }
  check('C0 index.html 无重复 id', dup.length === 0, '重复: ' + dup.join(','));
  /* 对话页详情区依赖的元素必须存在且唯一（chat.js renderDetail 引用） */
  for (const need of ['qContent', 'qAnswer', 'qAnalysis', 'qKnowledge', 'qOptions', 'qType']) {
    check('C0 详情页元素 #' + need + ' 存在且唯一', ids.filter((x) => x === need).length === 1);
  }
  const fors = [...html.matchAll(/\bfor="([^"]+)"/g)].map((m) => m[1]);
  const badFor = fors.filter((f) => !seen.has(f));
  check('C0 label for 均指向存在的 id', badFor.length === 0, '无效: ' + badFor.join(','));
}

/* ════════ Part A：api.js CRUD 回退与 mock 契约 ════════ */
(async () => {
  /* A1: 真实 403 → 原样抛出，不被 mock 掩盖 */
  globalThis.fetch = async () => ({ ok: false, status: 403, json: async () => ({ error: '仅教师和管理员可写', code: 'FORBIDDEN' }) });
  eval(webJs('api.js') + ';globalThis.__API = API;');
  globalThis.__API.setAuth('t', { id: 3, username: 's', name: 's', role: 'student' });
  try {
    await globalThis.__API.createQuestion({ content: 'x', type: 'short_answer', answer: 'y', options: [] });
    check('A1 真实 403 抛出', false, '未抛出');
  } catch (e) {
    check('A1 真实 403 抛出且 status=403', e.status === 403, 'status=' + e.status);
  }

  /* A2: 真实 404（题目不存在）→ 原样抛出 */
  globalThis.fetch = async () => ({ ok: false, status: 404, json: async () => ({ error: 'question not found' }) });
  eval(webJs('api.js') + ';globalThis.__API = API;');
  globalThis.__API.setAuth('t', { id: 2, role: 'teacher' });
  try {
    await globalThis.__API.deleteQuestion(999);
    check('A2 真实 404 抛出', false, '未抛出');
  } catch (e) {
    check('A2 真实 404 抛出且 status=404', e.status === 404, 'status=' + e.status);
  }

  /* A3: 网络不可达 → 回退 mock（teacher 创建成功） */
  globalThis.fetch = async () => { throw new TypeError('Failed to fetch'); };
  eval(webJs('api.js') + ';globalThis.__API = API;');
  globalThis.__API.setAuth('t', { id: 2, role: 'teacher' });
  const created = await globalThis.__API.createQuestion({
    content: '新题', type: 'single_choice',
    options: [{ label: 'A', text: '1' }, { label: 'B', text: '2' }],
    answer: 'A', analysis: '', knowledge_points: ['测试']
  });
  check('A3 网络错误回退 mock 且创建成功', created && created.question && created.question.id > 5, JSON.stringify(created).slice(0, 60));

  /* A4: file:// 离线 + student → mock 也执行 403 契约 */
  globalThis.window = { location: { protocol: 'file:' } };
  eval(webJs('api.js') + ';globalThis.__API = API;');
  globalThis.__API.setAuth('t', { id: 3, role: 'student' });
  try {
    await globalThis.__API.deleteQuestion(1);
    check('A4 离线 student 删除被拒', false, '未抛出');
  } catch (e) {
    check('A4 离线 student 删除被拒且 status=403', e.status === 403, 'status=' + e.status);
  }

  /* A5: 离线 teacher → 校验失败原样暴露（answer 不在选项中） */
  eval(webJs('api.js') + ';globalThis.__API = API;');
  globalThis.__API.setAuth('t', { id: 2, role: 'teacher' });
  try {
    await globalThis.__API.createQuestion({ content: 'x', type: 'single_choice', options: [{ label: 'A', text: '1' }], answer: 'Z' });
    check('A5 answer 不在选项中被拒', false, '未抛出');
  } catch (e) {
    check('A5 answer 不在选项中被拒', /answer/.test(e.message), e.message);
  }

  /* A6: 离线 teacher 更新 + 删除不存在的题 */
  const upd = await globalThis.__API.updateQuestion(1, { content: '改过的题干' });
  check('A6 离线更新成功', upd.question.content === '改过的题干');
  try {
    await globalThis.__API.updateQuestion(999, { content: 'x' });
    check('A6b 更新不存在的题被拒', false, '未抛出');
  } catch (e) {
    check('A6b 更新不存在的题被拒', /not found/.test(e.message), e.message);
  }

  await partB();
})();

/* ════════ Part B：页面行为 + 路由集成（真实 utils/questions/question-form/app） ════════ */
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
      appendChild: (child) => { el.children.push(child); return child; }
    };
    /* 真实 DOM 中 innerHTML 赋值会替换子节点；stub 至少要模拟 '' 清空 */
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
  globalThis.confirm = () => true;
  globalThis.alert = () => {};

  let currentUser = null;
  const questionDetailCalls = [];
  globalThis.API = {
    getToken: () => 'tok',
    getUser: () => currentUser,
    setAuth: () => {}, clearAuth: () => {},
    logout: () => Promise.resolve({}),
    questions: () => Promise.resolve({ questions: [
      { id: 1, content: '<img onerror=alert(1)> 题干A', type: 'single_choice', knowledge_points: ['X'], options: [] },
      { id: 2, content: '题干B', type: 'short_answer', knowledge_points: [], options: [] }
    ] }),
    questionDetail: (id) => { questionDetailCalls.push(id); return Promise.resolve({ question: {
      id: 1, content: '题干A', analysis: '解析', answer: 'A',
      knowledge_points: ['一元一次方程'], type: 'single_choice',
      options: [{ label: 'A', text: '1' }, { label: 'B', text: '2' }]
    } }); },
    createQuestion: () => Promise.resolve({ question: { id: 99 } }),
    updateQuestion: () => Promise.resolve({ question: { id: 1 } }),
    deleteQuestion: (id) => Promise.resolve({ status: 'ok' })
  };
  /* app.js 引用但本测试不加载的页面模块 */
  globalThis.ChatPage = { show() {}, resetConversations() {}, sendMessage() {}, cancel() {} };
  globalThis.AuthPage = { show() {}, handleLogin() {} };
  globalThis.RegisterPage = { show() {}, handleRegister() {} };
  globalThis.TeacherApiPage = { show() {}, load() {}, save() {}, toggle() {} };

  eval(webJs('utils.js'));
  /* 浏览器里多个 <script> 共享全局词法作用域；node 的多次 eval 不共享，
   * 故按 index.html 的加载顺序拼成一次 eval，语义等价。 */
  eval(webJs('utils.js') + webJs('questions.js') + webJs('question-form.js') + webJs('app.js')
    + ';globalThis.__route = route; globalThis.__QP = QuestionsPage; globalThis.__QF = QuestionFormPage;');
  const el = (id) => elements.get(id);

  /* B1: teacher 列表渲染 → 管理入口 + 按钮 */
  currentUser = { id: 2, role: 'teacher' };
  globalThis.__QP.show();
  await tick(); await tick();
  const html = el('questionsList').innerHTML;
  check('B1a teacher 可见「新增题目」入口', html.includes('+ 新增题目'));
  check('B1b teacher 卡片有编辑/删除按钮', html.includes('QuestionsPage.edit(1)') && html.includes('QuestionsPage.remove(1)'));
  check('B1c 题干 XSS 已转义', html.includes('&lt;img onerror=alert(1)&gt;') && !html.includes('<img onerror'), 'XSS 未转义');

  /* B2: student 列表渲染 → 无管理入口 */
  currentUser = { id: 3, role: 'student' };
  globalThis.__QP.show();
  await tick(); await tick();
  const htmlS = el('questionsList').innerHTML;
  check('B2 student 无新增入口与按钮', !htmlS.includes('新增题目') && !htmlS.includes('QuestionsPage.edit('));

  /* B3: QuestionFormPage 权限与 id 守卫 */
  currentUser = { id: 2, role: 'teacher' };
  globalThis.window.location.hash = '';
  globalThis.__QF.show('abc');
  check('B3a 非法 id 重定向 #questions', globalThis.window.location.hash === '#questions', globalThis.window.location.hash);
  globalThis.window.location.hash = '';
  globalThis.__QF.show('');
  check('B3b 空 id 重定向 #questions', globalThis.window.location.hash === '#questions', globalThis.window.location.hash);

  currentUser = { id: 3, role: 'student' };
  globalThis.window.location.hash = '';
  globalThis.__QF.show('1');
  check('B3c student 进表单页被踢回 #questions', globalThis.window.location.hash === '#questions', globalThis.window.location.hash);

  currentUser = { id: 2, role: 'teacher' };
  questionDetailCalls.length = 0;
  globalThis.__QF.show('new');
  check('B3d new 模式标题正确', el('questionFormTitle').textContent === '新增题目');
  check('B3e new 模式不请求详情', questionDetailCalls.length === 0);

  globalThis.__QF.show('1');
  await tick(); await tick();
  check('B3f 编辑模式加载详情', questionDetailCalls.includes('1') && el('qfContent').value === '题干A');
  check('B3g 选项渲染进容器', el('qOptionsContainer').children.length === 2, 'children=' + el('qOptionsContainer').children.length);

  /* B4: 路由集成 —— route() 单点隐藏 */
  currentUser = { id: 2, role: 'teacher' };
  globalThis.window.location.hash = '#question-form/new';
  globalThis.__route();
  check('B4a #question-form/new 显示表单页', !el('page-question-form').isHidden());
  check('B4b 题目列表页被隐藏', el('page-questions').isHidden());
  globalThis.window.location.hash = '#questions';
  globalThis.__route();
  check('B4c 回 #questions 后表单页被隐藏（无残留）', el('page-question-form').isHidden());
  globalThis.window.location.hash = '#chat/1';
  globalThis.__route();
  check('B4d 去 #chat/1 后表单页仍隐藏', el('page-question-form').isHidden());

  console.log('\n结果：' + pass + ' 通过 / ' + fail + ' 失败');
  process.exit(fail ? 1 : 0);
}
