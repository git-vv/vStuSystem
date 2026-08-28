/* admin.js - Super admin page logic */

var adminLogPage = 1;
var adminLogPageSize = 20;

(function() {
    App.requireLogin(function(session) {
        var nameEl = document.getElementById('admin-name');
        if (nameEl) { nameEl.textContent = session.username; }
        loadRegistrationRequests();
        loadResetRequests();
        loadUsers();
        loadLogs(1);
    }, 0);
})();

function handleLogout() {
    App.apiPost('/api/auth/logout', {}).then(function() {
        window.location.replace('/');
    });
}

/* --- Registration requests --- */
function loadRegistrationRequests() {
    App.apiGet('/api/admin/registration-requests').then(function(res) {
        var container = document.getElementById('reg-req-list');
        if (!container) { return; }

        if (res.data.code !== 0) {
            container.innerHTML = '<div class="empty-state"><div class="empty-text">' + (res.data.message || '加载失败') + '</div></div>';
            return;
        }

        var requests = (res.data.data && res.data.data.requests) ? res.data.data.requests : [];
        if (requests.length === 0) {
            container.innerHTML = '<div class="empty-state"><div class="empty-icon">&#128203;</div><div class="empty-text">暂无待审核的注册申请</div></div>';
            return;
        }

        var roleMap = {0: '管理员', 1: '教师'};
        var html = '<table style="width:100%;border-collapse:collapse;font-size:0.9rem;"><thead><tr style="border-bottom:1px solid #e0e0e0;">';
        html += '<th style="padding:8px;text-align:left;"><input type="checkbox" id="reg-select-all" onclick="toggleRegSelectAll(this)"></th>';
        html += '<th style="padding:8px;text-align:left;">用户名</th>';
        html += '<th style="padding:8px;text-align:left;">角色</th>';
        html += '<th style="padding:8px;text-align:left;">显示名称</th>';
        html += '<th style="padding:8px;text-align:left;">申请时间</th>';
        html += '<th style="padding:8px;text-align:left;">操作</th>';
        html += '</tr></thead><tbody>';

        for (var i = 0; i < requests.length; i++) {
            var r = requests[i];
            html += '<tr style="border-bottom:1px solid #f0f0f0;">';
            html += '<td style="padding:8px;"><input type="checkbox" class="reg-check" value="' + r.id + '"></td>';
            html += '<td style="padding:8px;">' + _esc(r.username) + '</td>';
            html += '<td style="padding:8px;">' + (roleMap[r.role] || '未知') + '</td>';
            html += '<td style="padding:8px;">' + _esc(r.display_name || r.username) + '</td>';
            html += '<td style="padding:8px;">' + _esc(r.request_time || '') + '</td>';
            html += '<td style="padding:8px;">';
            html += '<button class="btn btn-success btn-sm" onclick="approveRegistration(' + r.id + ')">同意</button> ';
            html += '<button class="btn btn-danger btn-sm" onclick="rejectRegistration(' + r.id + ')">拒绝</button>';
            html += '</td>';
            html += '</tr>';
        }

        html += '</tbody></table>';
        html += '<div style="margin-top:8px;">';
        html += '<button class="btn btn-success btn-sm" onclick="batchApproveRegistrations()">批量同意</button> ';
        html += '<button class="btn btn-danger btn-sm" onclick="batchRejectRegistrations()">批量拒绝</button>';
        html += '</div>';
        container.innerHTML = html;
    });
}

function toggleRegSelectAll(el) {
    var checks = document.querySelectorAll('.reg-check');
    for (var i = 0; i < checks.length; i++) { checks[i].checked = el.checked; }
}

function approveRegistration(id) {
    App.apiPost('/api/admin/approve-registration', {ids: [id]}).then(function(res) {
        if (res.data.code === 0) {
            App.showToast('审核通过', 'success');
            loadRegistrationRequests();
            loadLogs(adminLogPage);
        } else {
            App.showToast(res.data.message || '操作失败', 'error');
        }
    });
}

function rejectRegistration(id) {
    if (!confirm('确认拒绝该注册申请？拒绝后申请记录将被删除。')) { return; }
    App.apiPost('/api/admin/reject-registration', {ids: [id]}).then(function(res) {
        if (res.data.code === 0) {
            App.showToast('已拒绝', 'success');
            loadRegistrationRequests();
        } else {
            App.showToast(res.data.message || '操作失败', 'error');
        }
    });
}

function batchApproveRegistrations() {
    var ids = _getCheckedRegIds();
    if (ids.length === 0) { App.showToast('请先选择申请', 'error'); return; }
    App.apiPost('/api/admin/approve-registration', {ids: ids}).then(function(res) {
        if (res.data.code === 0) {
            App.showToast('批量审核通过', 'success');
            loadRegistrationRequests();
            loadLogs(adminLogPage);
        } else {
            App.showToast(res.data.message || '操作失败', 'error');
        }
    });
}

function batchRejectRegistrations() {
    var ids = _getCheckedRegIds();
    if (ids.length === 0) { App.showToast('请先选择申请', 'error'); return; }
    if (!confirm('确认拒绝选中的 ' + ids.length + ' 条注册申请？拒绝后申请记录将被删除。')) { return; }
    App.apiPost('/api/admin/reject-registration', {ids: ids}).then(function(res) {
        if (res.data.code === 0) {
            App.showToast('批量已拒绝', 'success');
            loadRegistrationRequests();
        } else {
            App.showToast(res.data.message || '操作失败', 'error');
        }
    });
}

function _getCheckedRegIds() {
    var checks = document.querySelectorAll('.reg-check:checked');
    var ids = [];
    for (var i = 0; i < checks.length; i++) { ids.push(parseInt(checks[i].value, 10)); }
    return ids;
}

/* --- Reset requests --- */
function loadResetRequests() {
    App.apiGet('/api/admin/reset-requests').then(function(res) {
        var container = document.getElementById('reset-list');
        if (!container) { return; }

        if (res.data.code !== 0) {
            container.innerHTML = '<div class="empty-state"><div class="empty-text">' + (res.data.message || '加载失败') + '</div></div>';
            return;
        }

        var requests = (res.data.data && res.data.data.requests) ? res.data.data.requests : [];
        if (requests.length === 0) {
            container.innerHTML = '<div class="empty-state"><div class="empty-icon">&#128203;</div><div class="empty-text">暂无待审批的重置申请</div></div>';
            return;
        }

        var html = '';
        for (var i = 0; i < requests.length; i++) {
            var r = requests[i];
            html += '<div class="list-item" data-request-id="' + r.id + '" data-user-id="' + r.user_id + '" data-username="' + r.username + '">';
            html += '  <div class="item-main">';
            html += '    <div class="item-title">' + _esc(r.username) + '</div>';
            html += '    <div class="item-sub">申请时间: ' + _esc(r.request_time || '') + '</div>';
            html += '  </div>';
            html += '  <div class="item-actions">';
            html += '    <button class="btn btn-success btn-sm" onclick="openApproveModal(this)">审批</button>';
            html += '  </div>';
            html += '</div>';
        }
        container.innerHTML = html;
    });
}

function openApproveModal(btn) {
    var item = btn.closest('.list-item');
    if (!item) { return; }

    document.getElementById('approve-request-id').value = item.getAttribute('data-request-id');
    document.getElementById('approve-user-id').value = item.getAttribute('data-user-id');
    document.getElementById('approve-username').value = item.getAttribute('data-username');
    document.getElementById('approve-new-password').value = '';
    document.getElementById('approve-new-password2').value = '';
    var errEl = document.getElementById('approve-error');
    if (errEl) { errEl.textContent = ''; errEl.classList.remove('show'); }

    App.showModal('approve-modal');
}

function handleApprove(e) {
    e.preventDefault();
    var errEl = document.getElementById('approve-error');
    if (errEl) { errEl.textContent = ''; errEl.classList.remove('show'); }

    var requestId = parseInt(document.getElementById('approve-request-id').value, 10);
    var userId = parseInt(document.getElementById('approve-user-id').value, 10);
    var newPwd = document.getElementById('approve-new-password').value;
    var newPwd2 = document.getElementById('approve-new-password2').value;

    if (newPwd.length < 6) {
        errEl.textContent = '新密码至少6位';
        errEl.classList.add('show');
        return;
    }
    if (newPwd !== newPwd2) {
        errEl.textContent = '两次输入的密码不一致';
        errEl.classList.add('show');
        return;
    }

    App.apiPost('/api/admin/approve-reset', {
        request_id: requestId,
        user_id: userId,
        new_password: newPwd
    }).then(function(res) {
        if (res.data.code === 0) {
            App.showToast('审批成功', 'success');
            App.hideModal('approve-modal');
            loadResetRequests();
            loadLogs(adminLogPage);
        } else {
            errEl.textContent = res.data.message || '审批失败';
            errEl.classList.add('show');
        }
    }).catch(function() {
        errEl.textContent = '网络错误';
        errEl.classList.add('show');
    });
}

/* --- Users --- */
function loadUsers() {
    App.apiGet('/api/admin/users').then(function(res) {
        var container = document.getElementById('user-list');
        if (!container) { return; }

        if (res.data.code !== 0) {
            container.innerHTML = '<div class="empty-state"><div class="empty-text">' + (res.data.message || '加载失败') + '</div></div>';
            return;
        }

        var users = res.data.data.users || [];
        if (users.length === 0) {
            container.innerHTML = '<div class="empty-state"><div class="empty-icon">&#128100;</div><div class="empty-text">暂无用户</div></div>';
            return;
        }

        var html = '';
        for (var i = 0; i < users.length; i++) {
            var u = users[i];
            var roleLabel = (u.role === 0) ? '<span class="item-badge badge-red">管理员</span>' : '<span class="item-badge badge-blue">教师</span>';
            html += '<div class="list-item" data-user-id="' + u.id + '" data-username="' + _esc(u.username) + '">';
            html += '  <div class="item-main">';
            html += '    <div class="item-title">' + _esc(u.username) + roleLabel + '</div>';
            html += '    <div class="item-sub">创建时间: ' + _esc(u.create_time || '') + '</div>';
            html += '  </div>';
            if (u.role !== 0) {
                html += '  <div class="item-actions">';
                html += '    <button type="button" class="btn btn-danger btn-sm" onclick="deleteUser(' + u.id + ', \'' + _esc(u.username) + '\')">删除</button>';
                html += '  </div>';
            }
            html += '</div>';
        }
        container.innerHTML = html;

        /* Bind long-press context menu on user items */
        var items = container.querySelectorAll('.list-item');
        for (var j = 0; j < items.length; j++) {
            (function(el) {
                var uid = parseInt(el.getAttribute('data-user-id'), 10);
                var uname = el.getAttribute('data-username');
                if (uid && uname) {
                    App.bindLongPress(el, function(e) {
                        App.showContextMenu(e, [
                            { label: '删除用户', danger: true, action: function() { deleteUser(uid, uname); } }
                        ]);
                    });
                }
            })(items[j]);
        }
    }).catch(function() {
        var container = document.getElementById('user-list');
        if (container) {
            container.innerHTML = '<div class="empty-state"><div class="empty-text">加载失败，请刷新页面重试</div></div>';
        }
    });
}

function deleteUser(userId, username) {
    if (!confirm('确定要删除用户 "' + username + '" 吗？')) { return; }

    App.apiPost('/api/admin/user/delete', { user_id: userId }).then(function(res) {
        if (res.data.code === 0) {
            App.showToast('删除成功', 'success');
            loadUsers();
            loadLogs(adminLogPage);
        } else {
            App.showToast(res.data.message || '删除失败', 'error');
        }
    }).catch(function() {
        App.showToast('网络错误', 'error');
    });
}

/* --- Logs --- */
function loadLogs(page) {
    adminLogPage = page || 1;

    var params = '?page=' + adminLogPage + '&page_size=' + adminLogPageSize;
    var opType = document.getElementById('log-op-type').value;
    var startTime = document.getElementById('log-start').value;
    var endTime = document.getElementById('log-end').value;
    var className = document.getElementById('log-class').value.trim();
    var teacherName = document.getElementById('log-teacher').value.trim();
    var studentName = document.getElementById('log-student').value.trim();
    var resourceName = document.getElementById('log-resource').value.trim();

    if (opType) { params += '&op_type=' + opType; }
    if (startTime) { params += '&start_time=' + startTime; }
    if (endTime) { params += '&end_time=' + endTime; }
    if (className) { params += '&class_name=' + encodeURIComponent(className); }
    if (teacherName) { params += '&teacher_name=' + encodeURIComponent(teacherName); }
    if (studentName) { params += '&student_name=' + encodeURIComponent(studentName); }
    if (resourceName) { params += '&resource_name=' + encodeURIComponent(resourceName); }

    App.apiGet('/api/admin/logs' + params).then(function(res) {
        var container = document.getElementById('log-list');
        if (!container) { return; }

        if (res.data.code !== 0) {
            container.innerHTML = '<div class="empty-state"><div class="empty-text">' + (res.data.message || '加载失败') + '</div></div>';
            return;
        }

        var data = res.data.data || {};
        var logs = data.logs || [];
        var total = data.total || 0;

        if (logs.length === 0) {
            container.innerHTML = '<div class="empty-state"><div class="empty-icon">&#128196;</div><div class="empty-text">暂无日志记录</div></div>';
            document.getElementById('log-pagination').innerHTML = '';
            return;
        }

        var html = '';
        for (var i = 0; i < logs.length; i++) {
            var log = logs[i];
            html += '<div class="list-item">';
            html += '  <div class="item-main">';
            html += '    <div class="item-title">' + App.opTypeLabel(log.op_type) + '</div>';
            html += '    <div class="item-sub">';
            html += '      操作人: ' + _esc(log.operator_name || '-');
            if (log.target_class) { html += ' | 班级: ' + _esc(log.target_class); }
            if (log.target_student) { html += ' | 学生: ' + _esc(log.target_student); }
            if (log.target_resource) { html += ' | 资源: ' + _esc(log.target_resource); }
            html += '    </div>';
            html += '    <div class="item-sub">' + _esc(log.detail || '') + ' | ' + _esc(log.op_time || '') + '</div>';
            html += '  </div>';
            html += '</div>';
        }
        container.innerHTML = html;

        App.renderPagination('log-pagination', total, adminLogPage, adminLogPageSize, function(p) {
            loadLogs(p);
        });
    });
}

function cleanLogs() {
    if (!confirm('确定要清理符合条件的日志吗？此操作不可恢复！')) { return; }

    var body = {};
    var opType = document.getElementById('log-op-type').value;
    var startTime = document.getElementById('log-start').value;
    var endTime = document.getElementById('log-end').value;
    var className = document.getElementById('log-class').value.trim();
    var teacherName = document.getElementById('log-teacher').value.trim();
    var studentName = document.getElementById('log-student').value.trim();
    var resourceName = document.getElementById('log-resource').value.trim();

    if (opType) { body.op_type = parseInt(opType, 10); }
    if (startTime) { body.start_time = startTime; }
    if (endTime) { body.end_time = endTime; }
    if (className) { body.class_name = className; }
    if (teacherName) { body.teacher_name = teacherName; }
    if (studentName) { body.student_name = studentName; }
    if (resourceName) { body.resource_name = resourceName; }

    App.apiPost('/api/admin/logs/clean', body).then(function(res) {
        if (res.data.code === 0) {
            App.showToast('清理成功', 'success');
            loadLogs(1);
        } else {
            App.showToast(res.data.message || '清理失败', 'error');
        }
    }).catch(function() {
        App.showToast('网络错误', 'error');
    });
}

/* --- Utility --- */
function _esc(s) {
    if (!s) { return ''; }
    var div = document.createElement('div');
    div.appendChild(document.createTextNode(s));
    return div.innerHTML;
}
