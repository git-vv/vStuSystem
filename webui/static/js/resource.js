/* resource.js - Resource management page logic */

(function() {
    App.requireLogin(function(session) {
        var nameEl = document.getElementById('user-name');
        if (nameEl) { nameEl.textContent = session.username; }
        loadResources();
    }, 0);
})();

function handleLogout() {
    App.apiPost('/api/auth/logout', {}).then(function() {
        window.location.replace('/');
    });
}

/* --- Resource type toggle --- */
function onResourceTypeChange() {
    var typeEl = document.getElementById('res-type');
    var nameEl = document.getElementById('res-name');
    if (!typeEl || !nameEl) { return; }

    if (typeEl.value === '1') {
        /* 床位类型：名称锁定为"床位" */
        nameEl.value = '床位';
        nameEl.readOnly = true;
        nameEl.placeholder = '床位';
    } else {
        /* 其他类型：用户自定义名称 */
        nameEl.value = '';
        nameEl.readOnly = false;
        nameEl.placeholder = '如：教材、桌椅等';
    }
}

/* --- Resource list --- */
function loadResources() {
    var container = document.getElementById('resource-list');
    if (!container) { return; }
    container.innerHTML = '<div class="loading">加载中...</div>';

    App.apiGet('/api/resource/list').then(function(res) {
        if (res.data.code !== 0) {
            container.innerHTML = '<div class="empty-state"><div class="empty-text">' + (res.data.message || '加载失败') + '</div></div>';
            return;
        }

        var list = res.data.data.list || [];
        if (list.length === 0) {
            container.innerHTML = '<div class="empty-state"><div class="empty-icon">&#128218;</div><div class="empty-text">暂无资源</div></div>';
            return;
        }

        var html = '';
        for (var i = 0; i < list.length; i++) {
            var r = list[i];
            var usagePercent = r.total_count > 0 ? Math.round(r.used_count / r.total_count * 100) : 0;
            var statusBadge = usagePercent >= 100
                ? '<span class="item-badge badge-red">已用完</span>'
                : '<span class="item-badge badge-green">可用</span>';
            var typeTag = r.resource_type === 1
                ? '<span class="tag tag-orange">床位</span>'
                : '';

            html += '<div class="list-item" data-res-id="' + r.id + '" data-res-name="' + _esc(r.name) + '" data-res-total="' + r.total_count + '" data-res-type="' + (r.resource_type || 0) + '">';
            html += '  <div class="item-main">';
            html += '    <div class="item-title">' + _esc(r.name) + typeTag + statusBadge + '</div>';
            html += '    <div class="item-sub">';
            html += '      总量: ' + r.total_count + ' | 已分配: ' + r.used_count + ' | 剩余: ' + r.remain_count;
            if (r.resource_type === 1 && r.bed_reserved_count > 0) {
                html += ' | 报名占用: ' + r.bed_reserved_count;
            }
            html += '      (' + usagePercent + '%)';
            html += '    </div>';
            html += '  </div>';
            html += '  <div class="item-actions">';
            html += '    <button class="btn btn-secondary btn-sm" onclick="showUsage(' + r.id + ', \'' + _esc(r.name) + '\')">详情</button>';
            html += '    <button class="btn btn-primary btn-sm" onclick="openEditModal(' + r.id + ', \'' + _esc(r.name) + '\', ' + r.total_count + ')">修改</button>';
            html += '    <button class="btn btn-danger btn-sm" onclick="deleteResource(' + r.id + ', \'' + _esc(r.name) + '\')">删除</button>';
            html += '  </div>';
            html += '</div>';
        }
        container.innerHTML = html;

        /* Bind long-press context menu */
        var items = container.querySelectorAll('.list-item');
        for (var j = 0; j < items.length; j++) {
            (function(el) {
                var rid = parseInt(el.getAttribute('data-res-id'), 10);
                var rname = el.getAttribute('data-res-name');
                var rtotal = parseInt(el.getAttribute('data-res-total'), 10);
                App.bindLongPress(el, function(e) {
                    App.showContextMenu(e, [
                        { label: '查看详情', action: function() { showUsage(rid, rname); } },
                        { label: '修改', action: function() { openEditModal(rid, rname, rtotal); } },
                        { label: '删除', danger: true, action: function() { deleteResource(rid, rname); } }
                    ]);
                });
            })(items[j]);
        }
    }).catch(function() {
        container.innerHTML = '<div class="empty-state"><div class="empty-text">网络错误</div></div>';
    });
}

/* --- Add resource --- */
function handleAddResource(e) {
    e.preventDefault();
    var errEl = document.getElementById('add-error');
    if (errEl) { errEl.textContent = ''; errEl.classList.remove('show'); }

    var name = document.getElementById('res-name').value.trim();
    var total = parseInt(document.getElementById('res-total').value, 10);
    var resourceType = parseInt(document.getElementById('res-type').value, 10) || 0;

    if (!name) { _showAddError('请输入资源名称'); return; }
    if (!total || total <= 0) { _showAddError('总数量必须大于0'); return; }

    var btn = document.getElementById('add-btn');
    btn.disabled = true;
    btn.textContent = '添加中...';

    App.apiPost('/api/resource/create', {
        name: name,
        total_count: total,
        resource_type: resourceType
    }).then(function(res) {
        btn.disabled = false;
        btn.textContent = '添加资源';
        if (res.data.code === 0) {
            App.showToast('添加成功', 'success');
            document.getElementById('add-form').reset();
            loadResources();
        } else {
            _showAddError(res.data.message || '添加失败');
        }
    }).catch(function() {
        btn.disabled = false;
        btn.textContent = '添加资源';
        _showAddError('网络错误');
    });
}

function _showAddError(msg) {
    var errEl = document.getElementById('add-error');
    if (errEl) { errEl.textContent = msg; errEl.classList.add('show'); }
}

/* --- Edit resource --- */
function openEditModal(id, name, total) {
    document.getElementById('edit-res-id').value = id;
    document.getElementById('edit-res-name').value = name;
    document.getElementById('edit-res-total').value = total;
    var errEl = document.getElementById('edit-error');
    if (errEl) { errEl.textContent = ''; errEl.classList.remove('show'); }
    App.showModal('edit-modal');
}

function handleEditResource(e) {
    e.preventDefault();
    var errEl = document.getElementById('edit-error');
    if (errEl) { errEl.textContent = ''; errEl.classList.remove('show'); }

    var id = parseInt(document.getElementById('edit-res-id').value, 10);
    var total = parseInt(document.getElementById('edit-res-total').value, 10);

    if (!total || total <= 0) {
        errEl.textContent = '总数量必须大于0';
        errEl.classList.add('show');
        return;
    }

    App.apiPut('/api/resource/update', {
        id: id,
        total_count: total
    }).then(function(res) {
        if (res.data.code === 0) {
            App.showToast('修改成功', 'success');
            App.hideModal('edit-modal');
            loadResources();
        } else {
            errEl.textContent = res.data.message || '修改失败';
            errEl.classList.add('show');
        }
    }).catch(function() {
        errEl.textContent = '网络错误';
        errEl.classList.add('show');
    });
}

/* --- Delete resource --- */
function deleteResource(id, name) {
    if (!confirm('确定要删除资源 "' + name + '" 吗？')) { return; }

    App.apiDelete('/api/resource/delete', { id: id }).then(function(res) {
        if (res.data.code === 0) {
            App.showToast('删除成功', 'success');
            loadResources();
        } else {
            App.showToast(res.data.message || '删除失败', 'error');
        }
    }).catch(function() {
        App.showToast('网络错误', 'error');
    });
}

/* --- Resource usage --- */
function showUsage(id, name) {
    var titleEl = document.getElementById('usage-title');
    if (titleEl) { titleEl.textContent = name + ' - 使用详情'; }

    var detailEl = document.getElementById('usage-detail');
    if (detailEl) { detailEl.innerHTML = '<div class="loading">加载中...</div>'; }

    App.showModal('usage-modal');

    App.apiGet('/api/resource/usage?id=' + id).then(function(res) {
        if (res.data.code !== 0) {
            detailEl.innerHTML = '<div class="empty-state"><div class="empty-text">' + (res.data.message || '加载失败') + '</div></div>';
            return;
        }

        var data = res.data.data || {};
        var allocs = data.allocations || [];

        var html = '<div style="font-size:0.9rem; color:#4a5568; margin-bottom:12px; line-height:1.8;">';
        html += '<div>总量: ' + data.total_count + ' | 已用: ' + data.used_count + ' | 剩余: ' + data.remain_count + '</div>';
        html += '</div>';

        if (allocs.length === 0) {
            html += '<div class="empty-state"><div class="empty-text">暂无分配记录</div></div>';
        } else {
            for (var i = 0; i < allocs.length; i++) {
                var a = allocs[i];
                html += '<div class="list-item">';
                html += '  <div class="item-main">';
                html += '    <div class="item-title">' + _esc(a.student_name) + ' <span class="tag tag-blue">编号' + a.resource_code + '</span></div>';
                html += '    <div class="item-sub">班级: ' + _esc(a.class_name) + ' | 教师: ' + _esc(a.teacher_name) + '</div>';
                html += '    <div class="item-sub">分配时间: ' + _esc(a.allocate_time) + '</div>';
                html += '  </div>';
                html += '</div>';
            }
        }

        detailEl.innerHTML = html;
    }).catch(function() {
        detailEl.innerHTML = '<div class="empty-state"><div class="empty-text">网络错误</div></div>';
    });
}

/* --- Utility --- */
function _esc(s) {
    if (!s) { return ''; }
    var div = document.createElement('div');
    div.appendChild(document.createTextNode(s));
    return div.innerHTML;
}
