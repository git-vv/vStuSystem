/* register.js - Register page logic (register only, no login) */

(function() {
    /* Check if already logged in */
    App.apiGet('/api/auth/session').then(function(res) {
        if (res.status === 200 && res.data.code === 0) {
            window.location.href = '/';
        }
    }).catch(function() {});

    /* Check admin exists on load */
    App.checkAdminExists(function(exists) {
        var roleSelect = document.getElementById('reg-role');
        if (!roleSelect) { return; }
        if (exists) {
            roleSelect.value = '1';
            var adminOpt = roleSelect.querySelector('option[value="0"]');
            if (adminOpt) { adminOpt.disabled = true; }
        } else {
            roleSelect.value = '0';
            var teacherOpt = roleSelect.querySelector('option[value="1"]');
            if (teacherOpt) { teacherOpt.disabled = true; }
            var hint = document.getElementById('reg-role-hint');
            if (hint) {
                hint.textContent = '请先创建管理员账号';
                hint.style.display = 'block';
            }
        }
    });

    /* Role select change */
    var roleSelect = document.getElementById('reg-role');
    if (roleSelect) {
        roleSelect.addEventListener('change', function() {
            var hint = document.getElementById('reg-role-hint');
            if (hint) {
                hint.style.display = (this.value === '0') ? 'block' : 'none';
            }
        });
    }
})();

function _showError(id, msg) {
    var el = document.getElementById(id);
    if (el) {
        el.textContent = msg;
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

function handleRegister(e) {
    e.preventDefault();
    _hideError('reg-error');

    var username = document.getElementById('reg-username').value.trim();
    var password = document.getElementById('reg-password').value;
    var password2 = document.getElementById('reg-password2').value;
    var role = parseInt(document.getElementById('reg-role').value, 10);

    if (!username) {
        _showError('reg-error', '请输入用户名');
        return;
    }
    if (username.length < 3) {
        _showError('reg-error', '用户名至少3个字符');
        return;
    }
    if (!password) {
        _showError('reg-error', '请输入密码');
        return;
    }
    if (password.length < 6) {
        _showError('reg-error', '密码至少6位');
        return;
    }
    if (password !== password2) {
        _showError('reg-error', '两次输入的密码不一致');
        return;
    }

    var btn = document.getElementById('reg-btn');
    btn.disabled = true;
    btn.textContent = '注册中...';

    App.apiPost('/api/auth/register', {
        username: username,
        password: password,
        role: role
    }).then(function(res) {
        btn.disabled = false;
        btn.textContent = '注册';
        if (res.data.code === 0) {
            if (res.data.data && res.data.data.pending_review) {
                App.showToast('注册申请已提交，请等待管理员审核', 'success');
            } else {
                App.showToast('注册成功', 'success');
            }
            setTimeout(function() {
                window.location.replace('/login');
            }, 1500);
        } else {
            _showError('reg-error', res.data.message || '注册失败');
        }
    }).catch(function() {
        btn.disabled = false;
        btn.textContent = '注册';
        _showError('reg-error', '网络错误，请重试');
    });
}
