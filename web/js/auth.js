/* ── 登录页 ── */
const AuthPage = (() => {
  const $ = (id) => document.getElementById(id);

  function show() {
    $('page-login').classList.remove('hidden');
    $('page-register').classList.add('hidden');
    $('navbar').classList.add('hidden');
    $('page-questions').classList.add('hidden');
    $('page-chat').classList.add('hidden');
    $('loginError').classList.add('hidden');
    $('username').value = '';
    $('password').value = '';
    $('username').focus();
  }

  async function handleLogin(e) {
    e.preventDefault();
    const btn = $('loginBtn');
    const errEl = $('loginError');
    btn.disabled = true;
    btn.textContent = '登录中...';
    errEl.classList.add('hidden');

    try {
      const username = $('username').value.trim();
      const password = $('password').value;
      const data = await API.login(username, password);
      // data: { token, user: { id, username, name, role } }
      API.setAuth(data.token, data.user);
      // 跳转到题目列表
      navigate('#questions');
    } catch (e) {
      errEl.textContent = e.message || '登录失败，请重试';
      errEl.classList.remove('hidden');
    } finally {
      btn.disabled = false;
      btn.textContent = '登 录';
    }
  }

  return { show, handleLogin };
})();

/* ── 注册页 ── */
const RegisterPage = (() => {
  const $ = (id) => document.getElementById(id);

  function show() {
    $('page-register').classList.remove('hidden');
    $('page-login').classList.add('hidden');
    $('navbar').classList.add('hidden');
    $('page-questions').classList.add('hidden');
    $('page-chat').classList.add('hidden');
    $('regError').classList.add('hidden');
    $('regUsername').value = '';
    $('regPassword').value = '';
    $('regConfirm').value = '';
    $('regRole').value = 'student';
    $('regUsername').focus();
  }

  function showError(message) {
    const errEl = $('regError');
    errEl.textContent = message;
    errEl.classList.remove('hidden');
  }

  async function handleRegister(e) {
    e.preventDefault();
    const btn = $('registerBtn');
    const username = $('regUsername').value.trim();
    const password = $('regPassword').value;
    const confirm = $('regConfirm').value;
    const role = $('regRole').value;

    $('regError').classList.add('hidden');

    /* 客户端先做一次与后端一致的校验，减少无谓请求 */
    if (!username) {
      showError('用户名不能为空');
      return false;
    }
    if (password !== confirm) {
      showError('两次输入的密码不一致');
      return false;
    }
    if (password.length < 8 || !/[A-Za-z]/.test(password) || !/[0-9]/.test(password)) {
      showError('密码至少 8 位，且必须包含字母和数字');
      return false;
    }

    btn.disabled = true;
    btn.textContent = '注册中...';
    try {
      const data = await API.register(username, password, confirm, role);
      /* docs/auth.md：注册成功自动创建 Session，直接进入已登录状态 */
      API.setAuth(data.token, data.user);
      navigate('#questions');
    } catch (err) {
      if (err && err.status === 409) {
        showError('用户名已存在，请更换后重试');
      } else {
        showError((err && err.message) || '注册失败，请重试');
      }
    } finally {
      btn.disabled = false;
      btn.textContent = '注 册';
    }
    return false;
  }

  return { show, handleRegister };
})();