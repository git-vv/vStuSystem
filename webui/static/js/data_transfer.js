/* data_transfer.js - Data transfer management page */

(function() {
    'use strict';

    var _files = { incremental: null, overwrite: null };

    function init() {
        App.requireLogin(function(userInfo) {
            if (userInfo) {
                var el = document.getElementById('user-display');
                if (el) {
                    el.textContent = userInfo.display_name || userInfo.username;
                }
            }
        }, 0);
    }

    window.handleLogout = function() {
        App.apiPost('/api/auth/logout', {}).then(function() {
            App.deleteCookie('session_id');
            window.location.replace('/login');
        });
    };

    window.exportData = function() {
        App.showToast('正在导出数据...', 'info');
        fetch('/api/data_transfer/export', {
            method: 'GET',
            credentials: 'same-origin'
        }).then(function(resp) {
            if (!resp.ok) {
                return resp.json().then(function(j) {
                    throw new Error(j.message || '导出失败');
                });
            }
            var disposition = resp.headers.get('Content-Disposition') || '';
            var filename = 'backup.dtz';
            var match = disposition.match(/filename="([^"]+)"/);
            if (match) { filename = match[1]; }
            return resp.blob().then(function(blob) {
                var url = URL.createObjectURL(blob);
                var a = document.createElement('a');
                a.href = url;
                a.download = filename;
                document.body.appendChild(a);
                a.click();
                document.body.removeChild(a);
                URL.revokeObjectURL(url);
                App.showToast('导出成功', 'success');
            });
        }).catch(function(err) {
            App.showToast(err.message || '导出失败', 'error');
        });
    };

    window.onFileSelected = function(mode) {
        var input = document.getElementById(mode + '-file');
        var btn = document.getElementById(mode + '-btn');
        var nameEl = document.getElementById(mode + '-filename');
        if (!input || !input.files || input.files.length === 0) { return; }

        var file = input.files[0];
        if (!file.name.toLowerCase().endsWith('.dtz')) {
            App.showToast('请选择 .dtz 文件', 'error');
            input.value = '';
            return;
        }

        _files[mode] = file;
        btn.disabled = false;
        nameEl.textContent = file.name;
    };

    window.importData = function(mode) {
        var file = _files[mode];
        if (!file) {
            App.showToast('请先选择文件', 'error');
            return;
        }

        if (mode === 'overwrite') {
            if (!confirm('确定要执行完全覆盖吗？\n\n当前系统的所有业务数据将被清空并替换为导入数据。\n系统会先自动备份当前数据。')) {
                return;
            }
        }

        var btn = document.getElementById(mode + '-btn');
        btn.disabled = true;
        App.showToast('正在处理...', 'info');

        var reader = new FileReader();
        reader.onload = function(e) {
            var base64 = e.target.result.split(',')[1];
            var url = mode === 'overwrite'
                ? '/api/data_transfer/overwrite'
                : '/api/data_transfer/import';

            App.apiPost(url, { file_data: base64 }).then(function(res) {
                if (res.data.code !== 0) {
                    App.showToast(res.data.message || '导入失败', 'error');
                    btn.disabled = false;
                    return;
                }
                showResult(res.data.data);
                App.showToast('导入成功', 'success');
                _files[mode] = null;
                document.getElementById(mode + '-file').value = '';
                document.getElementById(mode + '-filename').textContent = '';
            }).catch(function() {
                App.showToast('网络错误', 'error');
                btn.disabled = false;
            });
        };
        reader.onerror = function() {
            App.showToast('文件读取失败', 'error');
            btn.disabled = false;
        };
        reader.readAsDataURL(file);
    };

    function showResult(data) {
        var section = document.getElementById('result-section');
        var content = document.getElementById('result-content');
        section.style.display = 'block';

        var html = '<table class="result-table">';
        html += '<tr><th>数据表</th><th>新增</th><th>跳过</th><th>失败</th></tr>';

        var tables = data.tables || [];
        for (var i = 0; i < tables.length; i++) {
            var t = tables[i];
            html += '<tr>';
            html += '<td>' + _esc(t.table_name) + '</td>';
            html += '<td>' + (t.inserted || 0) + '</td>';
            html += '<td>' + (t.skipped || 0) + '</td>';
            html += '<td>' + (t.failed || 0) + '</td>';
            html += '</tr>';
        }
        html += '</table>';

        html += '<div class="result-summary">';
        html += '图片: 新增 ' + (data.images_added || 0) + ' 张';
        html += ', 跳过 ' + (data.images_skipped || 0) + ' 张';
        html += '</div>';

        content.innerHTML = html;
    }

    function _esc(s) {
        if (!s) { return ''; }
        var div = document.createElement('div');
        div.textContent = s;
        return div.innerHTML;
    }

    init();
})();
