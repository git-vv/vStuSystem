/* account.js - 账号管理页面交互逻辑 */

var isSubmittingPwd = false;

/* 修改密码按钮点击处理 - 直接打开弹窗，已登录时自动填充用户名 */
function handleChangePasswordClick() {
    openChangePwdModal();
    App.apiGet('/api/auth/session').then(function(res) {
        if (res.status === 200 && res.data.code === 0) {
            var usernameEl = document.getElementById('chg-username');
            if (usernameEl && !usernameEl.value) {
                usernameEl.value = res.data.data.username;
                document.getElementById('old-password').focus();
            }
        }
    }).catch(function() {});
}

/* 注册账号按钮点击处理 */
function handleRegisterClick() {
    App.apiGet('/api/auth/session').then(function(res) {
        if (res.status === 200 && res.data.code === 0) {
            App.showToast('请先退出当前账户后再注册新账号', 'info', 3000);
        } else {
            App.navigateTo('/register');
        }
    }).catch(function() {
        App.navigateTo('/register');
    });
}

/* 打开修改密码弹窗 */
function openChangePwdModal() {
    document.getElementById('change-pwd-modal').style.display = 'flex';
    document.getElementById('pwd-error').classList.remove('show');
    document.getElementById('chg-username').value = '';
    document.getElementById('old-password').value = '';
    document.getElementById('new-password').value = '';
    document.getElementById('confirm-password').value = '';
    document.getElementById('chg-username').focus();
}

/* 关闭修改密码弹窗 */
function closeChangePwdModal() {
    document.getElementById('change-pwd-modal').style.display = 'none';
    document.getElementById('pwd-error').classList.remove('show');
    document.getElementById('chg-username').value = '';
    document.getElementById('old-password').value = '';
    document.getElementById('new-password').value = '';
    document.getElementById('confirm-password').value = '';
    isSubmittingPwd = false;
    var btn = document.getElementById('change-pwd-btn');
    btn.textContent = '确认修改';
    btn.disabled = false;
}

/* 遮罩层点击关闭弹窗 */
document.getElementById('change-pwd-modal').addEventListener('click', function(e) {
    if (e.target === this) {
        closeChangePwdModal();
    }
});

/* 提交修改密码 */
function submitChangePassword() {
    if (isSubmittingPwd) return;

    var username = document.getElementById('chg-username').value.trim();
    var oldPwd = document.getElementById('old-password').value;
    var newPwd = document.getElementById('new-password').value;
    var confirmPwd = document.getElementById('confirm-password').value;

    /* 前端校验 */
    if (!username) {
        showPwdError('请输入用户名');
        return;
    }
    if (!oldPwd) {
        showPwdError('请输入旧密码');
        return;
    }
    if (!newPwd) {
        showPwdError('请输入新密码');
        return;
    }
    if (newPwd.length < 6) {
        showPwdError('新密码至少需要6位');
        return;
    }
    if (newPwd !== confirmPwd) {
        showPwdError('两次输入的新密码不一致');
        return;
    }

    /* 提交 */
    isSubmittingPwd = true;
    var btn = document.getElementById('change-pwd-btn');
    btn.textContent = '修改中...';
    btn.disabled = true;
    document.getElementById('pwd-error').classList.remove('show');

    App.apiPost('/api/auth/change-password', {
        username: username,
        old_password: oldPwd,
        new_password: newPwd
    }).then(function(res) {
        isSubmittingPwd = false;
        btn.textContent = '确认修改';
        btn.disabled = false;

        if (res.data.code === 0) {
            App.showToast('密码修改成功', 'success', 2500);
            closeChangePwdModal();
        } else if (res.data.code === 2002) {
            /* ERR_AUTH_INVALID_CREDENTIALS: 旧密码不正确 */
            showPwdError('旧密码不正确');
        } else if (res.data.code === 2007) {
            /* ERR_AUTH_USER_NOT_FOUND: 用户名不存在 */
            showPwdError('用户名不存在');
        } else {
            showPwdError(res.data.message || '修改失败，请重试');
        }
    }).catch(function() {
        isSubmittingPwd = false;
        btn.textContent = '确认修改';
        btn.disabled = false;
        showPwdError('网络错误，请重试');
    });
}

/* 弹窗内错误显示 */
function showPwdError(msg) {
    var errorEl = document.getElementById('pwd-error');
    errorEl.textContent = msg;
    errorEl.classList.add('show');
}
