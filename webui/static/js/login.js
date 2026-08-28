/* login.js - Login page logic (login only, no register) */

(function() {
    /* Check if already logged in */
    App.apiGet('/api/auth/session').then(function(res) {
        if (res.status === 200 && res.data.code === 0) {
            window.location.href = '/';
        }
    }).catch(function() {});

    /* Read role param from URL (e.g. /login?role=0 for admin-only login) */
    var roleParam = '';
    var qs = window.location.search;
    if (qs) {
        var m = qs.match(/[?&]role=(\d+)/);
        if (m) { roleParam = m[1]; }
    }
    if (roleParam === '0') {
        var titleEl = document.querySelector('.header h1');
        if (titleEl) { titleEl.textContent = '管理员登录'; }
    }
    window._loginRole = roleParam;
})();

function _showError(id, msg) {
    var el = document.getElementById(id);
    if (el) {
        el.innerHTML = msg;
        el.classList.add('show');
    }
}

function _hideError(id) {
    var el = document.getElementById(id);
    if (el) {
        el.textContent = '';
        el.classList.remove('show');
    }
}

function showResetForm() {
    var section = document.getElementById('reset-section');
    if (section) { section.style.display = 'block'; }
    var loginForm = document.getElementById('login-form');
    if (loginForm) { loginForm.style.display = 'none'; }
}

function hideResetForm() {
    var section = document.getElementById('reset-section');
    if (section) { section.style.display = 'none'; }
    var loginForm = document.getElementById('login-form');
    if (loginForm) { loginForm.style.display = 'block'; }
}

function handleLogin(e) {
    e.preventDefault();
    _hideError('login-error');

    var username = document.getElementById('login-username').value.trim();
    var password = document.getElementById('login-password').value;

    if (!username) {
        _showError('login-error', '请输入用户名');
        return;
    }
    if (!password) {
        _showError('login-error', '请输入密码');
        return;
    }

    var btn = document.getElementById('login-btn');
    btn.disabled = true;
    btn.textContent = '登录中...';

    var postData = {
        username: username,
        password: password
    };
    if (window._loginRole !== '') {
        postData.role = parseInt(window._loginRole, 10);
    }

    App.apiPost('/api/auth/login', postData).then(function(res) {
        btn.disabled = false;
        btn.textContent = '登录';
        if (res.data.code === 0) {
            App.showToast('登录成功', 'success');
            setTimeout(function() {
                window.location.href = '/';
            }, 500);
        } else if (res.data.code === 2007) {
            _showError('login-error', '用户不存在，请先<a href="/register" style="color:#667eea;">注册账号</a>');
        } else if (res.data.code === 2002) {
            _showError('login-error', '密码错误，请重新输入');
        } else if (res.data.code === 2005) {
            _showError('login-error', '该账号无管理员权限');
        } else {
            _showError('login-error', res.data.message || '登录失败');
        }
    }).catch(function() {
        btn.disabled = false;
        btn.textContent = '登录';
        _showError('login-error', '网络错误，请重试');
    });
}

function handleResetRequest(e) {
    e.preventDefault();
    _hideError('reset-error');

    var username = document.getElementById('reset-username').value.trim();
    if (!username) {
        _showError('reset-error', '请输入用户名');
        return;
    }

    var btn = document.getElementById('reset-btn');
    btn.disabled = true;
    btn.textContent = '提交中...';

    App.apiPost('/api/auth/reset-request', {
        username: username
    }).then(function(res) {
        btn.disabled = false;
        btn.textContent = '提交重置申请';
        if (res.data.code === 0) {
            App.showToast('重置申请已提交，请等待管理员审批', 'success', 3000);
            hideResetForm();
        } else {
            _showError('reset-error', res.data.message || '提交失败');
        }
    }).catch(function() {
        btn.disabled = false;
        btn.textContent = '提交重置申请';
        _showError('reset-error', '网络错误，请重试');
    });
}
