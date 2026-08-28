/* activity_manage.js - Activity management page */

(function() {
    'use strict';

    var _activities = [];
    var _coverFiles = [];
    var _groupData = null;
    var _groupFilename = null;
    var _editingId = 0;
    var _activityUrl = '';

    /* HTML escape helper to prevent XSS */
    function _esc(s) {
        if (!s) { return ''; }
        var div = document.createElement('div');
        div.textContent = s;
        return div.innerHTML;
    }

    function init() {
        App.requireLogin(function(userInfo) {
            if (userInfo) {
                document.getElementById('user-name').textContent = userInfo.display_name || userInfo.username;
            }
        });
        loadActivities();
        loadActivityUrl();
    }

    window.handleLogout = function() {
        App.apiPost('/api/auth/logout', {}).then(function() {
            App.deleteCookie('session_id');
            window.location.replace('/login');
        });
    };

    window.switchTab = function(tab) {
        var tabs = document.querySelectorAll('.tab-item');
        var contents = document.querySelectorAll('.tab-content');
        for (var i = 0; i < tabs.length; i++) {
            tabs[i].classList.remove('active');
        }
        for (var i = 0; i < contents.length; i++) {
            contents[i].classList.remove('active');
        }
        if (tab === 'manage') {
            tabs[0].classList.add('active');
            document.getElementById('tab-manage').classList.add('active');
        } else {
            tabs[1].classList.add('active');
            document.getElementById('tab-tools').classList.add('active');
        }
    };

    function loadActivities() {
        App.apiGet('/api/activity/list').then(function(res) {
            if (res.data.code !== 0) {
                App.showToast('加载失败', 'error');
                return;
            }
            _activities = (res.data.data && res.data.data.activities) || [];
            renderActivityTable();
        }).catch(function() {
            App.showToast('网络错误', 'error');
        });
    }

    function renderActivityTable() {
        var tbody = document.getElementById('activity-tbody');
        if (_activities.length === 0) {
            tbody.innerHTML = '<tr><td colspan="7" style="text-align:center; padding:20px; color:#a0aec0;">暂无活动</td></tr>';
            return;
        }

        var html = '';
        for (var i = 0; i < _activities.length; i++) {
            var act = _activities[i];
            var statusClass = act.status === 1 ? 'status-published' : 'status-draft';
            var statusText = act.status === 1 ? '已发布' : '未发布';

            var countText = act.signup_count;
            if (act.capacity > 0) {
                countText += ' / ' + act.capacity;
            }

            html += '<tr>';
            html += '<td><span class="sort-handle" title="拖拽排序">&#9776;</span> ' + act.sort_order + '</td>';
            html += '<td>';
            if (act.cover_image) {
                html += '<img class="activity-cover-thumb" src="' + _esc(act.cover_image) + '" alt="">';
            }
            html += '</td>';
            html += '<td>' + _esc(act.title) + '</td>';
            html += '<td style="font-size:0.8rem;">' + _esc(act.start_time) + '<br>~ ' + _esc(act.end_time) + '</td>';
            html += '<td>' + countText + '</td>';
            html += '<td><span class="status-badge ' + statusClass + '">' + statusText + '</span></td>';
            html += '<td><div class="action-btns">';
            html += '<button class="btn btn-secondary btn-sm" onclick="viewSignups(' + act.id + ')">报名</button>';
            html += '<button class="btn btn-secondary btn-sm" onclick="editActivity(' + act.id + ')">编辑</button>';
            if (act.status === 1) {
                html += '<button class="btn btn-secondary btn-sm" onclick="togglePublish(' + act.id + ', 0)">下架</button>';
            } else {
                html += '<button class="btn btn-primary btn-sm" onclick="togglePublish(' + act.id + ', 1)">发布</button>';
            }
            html += '<button class="btn btn-danger btn-sm" onclick="deleteActivity(' + act.id + ')">删除</button>';
            html += '</div></td>';
            html += '</tr>';
        }
        tbody.innerHTML = html;
    }

    window.openCreateModal = function() {
        _editingId = 0;
        _coverFiles = [];
        _groupData = null;
        _groupFilename = null;
        document.getElementById('modal-title').textContent = '创建活动';
        document.getElementById('activity-id').value = '';
        document.getElementById('activity-title').value = '';
        document.getElementById('activity-desc').value = '';
        document.getElementById('activity-start').value = '';
        document.getElementById('activity-end').value = '';
        document.getElementById('activity-deadline').value = '';
        document.getElementById('activity-capacity').value = '0';
        document.getElementById('activity-group-type').value = '0';
        document.getElementById('activity-group-size').value = '2';
        updateGroupTypeVisibility();
        document.getElementById('cover-input').value = '';
        document.getElementById('group-input').value = '';
        document.getElementById('cover-preview-grid').innerHTML = '';
        document.getElementById('group-preview').style.display = 'none';
        document.getElementById('activity-error').textContent = '';
        document.getElementById('save-btn').disabled = false;
        App.showModal('activity-modal');
    };

    window.editActivity = function(id) {
        var act = null;
        for (var i = 0; i < _activities.length; i++) {
            if (_activities[i].id === id) {
                act = _activities[i];
                break;
            }
        }
        if (!act) { return; }

        _editingId = id;
        _coverFiles = [];
        _groupData = null;
        _groupFilename = null;

        document.getElementById('modal-title').textContent = '编辑活动';
        document.getElementById('activity-id').value = id;
        document.getElementById('activity-title').value = act.title || '';
        document.getElementById('activity-desc').value = act.description || '';
        document.getElementById('activity-start').value = formatDatetimeLocal(act.start_time);
        document.getElementById('activity-end').value = formatDatetimeLocal(act.end_time);
        document.getElementById('activity-deadline').value = formatDatetimeLocal(act.signup_deadline);
        document.getElementById('activity-capacity').value = act.capacity || 0;
        var gt = act.group_type || 0;
        if (gt === 0 && act.min_group_size > 1) { gt = 1; }
        document.getElementById('activity-group-type').value = String(gt);
        document.getElementById('activity-group-size').value = act.min_group_size || 2;
        updateGroupTypeVisibility();
        document.getElementById('cover-input').value = '';
        document.getElementById('group-input').value = '';
        document.getElementById('activity-error').textContent = '';
        document.getElementById('save-btn').disabled = false;

        var grid = document.getElementById('cover-preview-grid');
        grid.innerHTML = '';
        var existingCovers = act.cover_images || [];
        for (var i = 0; i < existingCovers.length; i++) {
            addCoverPreviewItem(grid, existingCovers[i].path, existingCovers[i].id, true);
        }

        if (act.group_image) {
            document.getElementById('group-preview').src = act.group_image;
            document.getElementById('group-preview').style.display = 'block';
        } else {
            document.getElementById('group-preview').style.display = 'none';
        }

        App.showModal('activity-modal');
    };

    function formatDatetimeLocal(dt) {
        if (!dt) { return ''; }
        return dt.replace(' ', 'T').substring(0, 16);
    }

    window.updateGroupTypeVisibility = function() {
        var groupType = parseInt(document.getElementById('activity-group-type').value) || 0;
        var sizeGroup = document.getElementById('group-size-group');
        if (groupType > 0) {
            sizeGroup.style.display = '';
        } else {
            sizeGroup.style.display = 'none';
        }
    };

    function addCoverPreviewItem(grid, src, imageId, isExisting) {
        var wrapper = document.createElement('div');
        wrapper.style.cssText = 'position:relative; display:inline-block;';

        var img = document.createElement('img');
        img.src = src;
        img.style.cssText = 'width:80px; height:80px; object-fit:cover; border-radius:4px;';
        wrapper.appendChild(img);

        var btn = document.createElement('button');
        btn.textContent = '\u00d7';
        btn.style.cssText = 'position:absolute; top:-6px; right:-6px; width:20px; height:20px; border-radius:50%; background:#e53e3e; color:#fff; border:none; cursor:pointer; font-size:14px; line-height:20px; text-align:center; padding:0;';
        wrapper.appendChild(btn);

        if (isExisting) {
            btn.onclick = function() {
                App.showConfirm('删除此封面图？', function() {
                    App.apiPost('/api/activity/delete_cover_image', {
                        image_id: imageId,
                        activity_id: _editingId
                    }).then(function(res) {
                        if (res.data.code === 0) {
                            wrapper.remove();
                            App.showToast('已删除', 'success');
                            loadActivities();
                        } else {
                            App.showToast('删除失败', 'error');
                        }
                    }).catch(function() {
                        App.showToast('删除失败', 'error');
                    });
                });
            };
        } else {
            btn.onclick = function() {
                wrapper.remove();
                for (var i = _coverFiles.length - 1; i >= 0; i--) {
                    if (_coverFiles[i]._el === wrapper) {
                        _coverFiles.splice(i, 1);
                        break;
                    }
                }
            };
        }

        grid.appendChild(wrapper);
        return wrapper;
    }

    window.handleCoverSelect = function(e) {
        var files = e.target.files;
        if (!files || files.length === 0) { return; }
        var grid = document.getElementById('cover-preview-grid');
        var remaining = files.length;

        for (var i = 0; i < files.length; i++) {
            (function(file) {
                App.compressImage(file, 1080, 0.85, function(data, name) {
                    var el = addCoverPreviewItem(grid, 'data:image/jpeg;base64,' + data, 0, false);
                    _coverFiles.push({ data: data, filename: name, _el: el });
                    remaining--;
                    if (remaining === 0) { e.target.value = ''; }
                });
            })(files[i]);
        }
    };

    window.handleGroupSelect = function(e) {
        var file = e.target.files[0];
        if (!file) { return; }
        App.compressImage(file, 1080, 0.9, function(data, name) {
            _groupData = data;
            _groupFilename = name;
            document.getElementById('group-preview').src = 'data:image/jpeg;base64,' + data;
            document.getElementById('group-preview').style.display = 'block';
        });
    };

    window.saveActivity = function(e) {
        e.preventDefault();
        var errEl = document.getElementById('activity-error');
        errEl.textContent = '';

        var title = document.getElementById('activity-title').value.trim();
        var desc = document.getElementById('activity-desc').value.trim();
        var start = document.getElementById('activity-start').value.replace('T', ' ');
        var end = document.getElementById('activity-end').value.replace('T', ' ');
        var deadline = document.getElementById('activity-deadline').value.replace('T', ' ');
        var capacity = parseInt(document.getElementById('activity-capacity').value) || 0;
        var groupType = parseInt(document.getElementById('activity-group-type').value) || 0;
        var minGroupSize = 0;
        if (groupType > 0) {
            minGroupSize = parseInt(document.getElementById('activity-group-size').value) || 0;
            if (minGroupSize < 2) {
                errEl.textContent = '拼团人数至少为2';
                return;
            }
        }

        if (!title) {
            errEl.textContent = '请输入标题';
            return;
        }
        if (!start || !end || !deadline) {
            errEl.textContent = '请填写完整时间';
            return;
        }
        if (start >= end) {
            errEl.textContent = '结束时间必须晚于开始时间';
            return;
        }
        if (deadline > end) {
            errEl.textContent = '报名截止时间不能晚于结束时间';
            return;
        }

        document.getElementById('save-btn').disabled = true;

        var payload = {
            title: title,
            description: desc,
            start_time: start,
            end_time: end,
            signup_deadline: deadline,
            capacity: capacity,
            min_group_size: minGroupSize,
            group_type: groupType
        };

        var isCreate = _editingId === 0;
        var url = isCreate ? '/api/activity/create' : '/api/activity/update';
        if (!isCreate) {
            payload.id = _editingId;
        }

        App.apiPost(url, payload).then(function(res) {
            if (res.data.code !== 0) {
                errEl.textContent = res.data.message || '保存失败';
                document.getElementById('save-btn').disabled = false;
                return;
            }

            var actId = isCreate ? res.data.data.id : _editingId;

            /* upload images if selected */
            uploadImages(actId, function() {
                App.hideModal('activity-modal');
                loadActivities();
                App.showToast('保存成功', 'success');
            });
        }).catch(function() {
            errEl.textContent = '网络错误';
            document.getElementById('save-btn').disabled = false;
        });
    };

    function uploadImages(actId, callback) {
        var pending = 0;
        for (var i = 0; i < _coverFiles.length; i++) { pending++; }
        if (_groupData) { pending++; }
        if (pending === 0) {
            callback();
            return;
        }

        var done = function() {
            pending--;
            if (pending === 0) {
                callback();
            }
        };

        for (var i = 0; i < _coverFiles.length; i++) {
            (function(cf) {
                App.apiPost('/api/activity/upload_image', {
                    activity_id: actId,
                    type: 'cover',
                    filename: cf.filename,
                    data: cf.data
                }).then(function(res) {
                    if (res.data.code !== 0) {
                        App.showToast('封面上传失败', 'error');
                    }
                    done();
                }).catch(function() {
                    App.showToast('封面上传失败', 'error');
                    done();
                });
            })(_coverFiles[i]);
        }

        if (_groupData) {
            App.apiPost('/api/activity/upload_image', {
                activity_id: actId,
                type: 'group',
                filename: _groupFilename,
                data: _groupData
            }).then(function(res) {
                if (res.data.code !== 0) {
                    App.showToast('加群图片上传失败', 'error');
                }
                done();
            }).catch(function() {
                App.showToast('加群图片上传失败', 'error');
                done();
            });
        }
    }

    window.togglePublish = function(id, status) {
        App.apiPost('/api/activity/publish', { id: id, status: status }).then(function(res) {
            if (res.data.code === 0) {
                App.showToast(status === 1 ? '已发布' : '已下架', 'success');
                loadActivities();
            } else {
                App.showToast(res.data.message || '操作失败', 'error');
            }
        });
    };

    window.deleteActivity = function(id) {
        if (!confirm('确定删除该活动？报名记录将一并删除。')) {
            return;
        }
        App.apiPost('/api/activity/delete', { id: id }).then(function(res) {
            if (res.data.code === 0) {
                App.showToast('已删除', 'success');
                loadActivities();
            } else {
                App.showToast(res.data.message || '删除失败', 'error');
            }
        });
    };

    window.viewSignups = function(id) {
        App.apiGet('/api/activity/signups?activity_id=' + id).then(function(res) {
            if (res.data.code !== 0) {
                App.showToast('加载失败', 'error');
                return;
            }
            var list = (res.data.data && res.data.data.signups) || [];
            renderSignupsTable(list);
            App.showModal('signups-modal');
        }).catch(function() {
            App.showToast('网络请求失败', 'error');
        });
    };

    function renderSignupsTable(list) {
        var tbody = document.getElementById('signups-tbody');
        if (list.length === 0) {
            tbody.innerHTML = '<tr><td colspan="6" style="text-align:center; padding:20px; color:#a0aec0;">暂无报名</td></tr>';
            return;
        }
        var html = '';
        for (var i = 0; i < list.length; i++) {
            var groupInfo = '';
            if (list[i].group_invite_code) {
                groupInfo = _esc(list[i].group_invite_code);
                if (list[i].is_leader) {
                    groupInfo += ' <span style="color:#667eea; font-size:0.75rem;">[团长]</span>';
                }
            } else {
                groupInfo = '-';
            }
            html += '<tr>';
            html += '<td>' + _esc(list[i].name) + '</td>';
            html += '<td>' + _esc(list[i].phone) + '</td>';
            html += '<td>' + _esc(list[i].grade) + '</td>';
            html += '<td>' + (_esc(list[i].signup_type) || '-') + '</td>';
            html += '<td>' + groupInfo + '</td>';
            html += '<td>' + _esc(list[i].created_at) + '</td>';
            html += '</tr>';
        }
        tbody.innerHTML = html;
    }

    window.copySignups = function() {
        var rows = document.querySelectorAll('#signups-tbody tr');
        var text = '姓名\t手机号\t年级\t报名类型\t拼团\t报名时间\n';
        for (var i = 0; i < rows.length; i++) {
            var cells = rows[i].querySelectorAll('td');
            if (cells.length >= 6) {
                text += cells[0].textContent + '\t' + cells[1].textContent + '\t' + cells[2].textContent + '\t' + cells[3].textContent + '\t' + cells[4].textContent + '\t' + cells[5].textContent + '\n';
            }
        }
        copyToClipboard(text);
        App.showToast('已复制', 'success');
    };

    function loadActivityUrl() {
        App.apiGet('/api/network/info').then(function(res) {
            if (res.data.code === 0 && res.data.data) {
                var info = res.data.data;
                var host = (info.domain || info.ipv4 || info.ip || 'localhost') + ':8000';
                _activityUrl = window.location.protocol + '//' + host + '/activity';
                document.getElementById('activity-url').textContent = _activityUrl;
                renderQrcode(_activityUrl);
            }
        }).catch(function() {
            document.getElementById('activity-url').textContent = '获取失败';
        });
    }

    function renderQrcode(url) {
        var container = document.getElementById('qrcode-container');
        container.innerHTML = '';
        if (typeof QRCode !== 'undefined') {
            new QRCode(container, {
                text: url,
                width: 200,
                height: 200
            });
        }
    }

    window.copyActivityUrl = function() {
        if (_activityUrl) {
            copyToClipboard(_activityUrl);
            App.showToast('已复制', 'success');
        }
    };

    function copyToClipboard(text) {
        if (navigator.clipboard) {
            navigator.clipboard.writeText(text);
        } else {
            var ta = document.createElement('textarea');
            ta.value = text;
            document.body.appendChild(ta);
            ta.select();
            document.execCommand('copy');
            document.body.removeChild(ta);
        }
    }

    /* ---- Promotion ---- */
    var _promotionFiles = [];
    var _dragSrcEl = null;

    function addPromotionPreviewItem(grid, src, imageId, isExisting) {
        var wrapper = document.createElement('div');
        wrapper.style.cssText = 'position:relative; display:inline-block; cursor:move;';
        wrapper.setAttribute('draggable', 'true');

        var handle = document.createElement('span');
        handle.textContent = '\u2630';
        handle.style.cssText = 'position:absolute; top:-6px; left:-6px; width:20px; height:20px; border-radius:50%; background:#667eea; color:#fff; font-size:12px; line-height:20px; text-align:center; cursor:move; z-index:1;';
        wrapper.appendChild(handle);

        var img = document.createElement('img');
        img.src = src;
        img.style.cssText = 'width:80px; height:80px; object-fit:cover; border-radius:4px;';
        wrapper.appendChild(img);

        var btn = document.createElement('button');
        btn.textContent = '\u00d7';
        btn.style.cssText = 'position:absolute; top:-6px; right:-6px; width:20px; height:20px; border-radius:50%; background:#e53e3e; color:#fff; border:none; cursor:pointer; font-size:14px; line-height:20px; text-align:center; padding:0;';
        wrapper.appendChild(btn);

        if (isExisting) {
            wrapper.dataset.imageId = imageId;
            btn.onclick = function(e) {
                e.stopPropagation();
                App.showConfirm('删除此宣传图片？', function() {
                    App.apiPost('/api/activity/delete_promotion_image', {
                        image_id: imageId
                    }).then(function(res) {
                        if (res.data.code === 0) {
                            wrapper.remove();
                            App.showToast('已删除', 'success');
                        } else {
                            App.showToast('删除失败', 'error');
                        }
                    }).catch(function() {
                        App.showToast('删除失败', 'error');
                    });
                });
            };
        } else {
            btn.onclick = function(e) {
                e.stopPropagation();
                wrapper.remove();
                for (var i = _promotionFiles.length - 1; i >= 0; i--) {
                    if (_promotionFiles[i]._el === wrapper) {
                        _promotionFiles.splice(i, 1);
                        break;
                    }
                }
            };
        }

        wrapper.addEventListener('dragstart', function(e) {
            _dragSrcEl = wrapper;
            wrapper.style.opacity = '0.4';
            e.dataTransfer.effectAllowed = 'move';
        });
        wrapper.addEventListener('dragend', function() {
            wrapper.style.opacity = '';
            var items = grid.querySelectorAll('[data-image-id]');
            for (var i = 0; i < items.length; i++) {
                items[i].style.borderLeft = '';
            }
        });
        wrapper.addEventListener('dragover', function(e) {
            e.preventDefault();
            e.dataTransfer.dropEffect = 'move';
            return false;
        });
        wrapper.addEventListener('dragenter', function() {
            wrapper.style.borderLeft = '3px solid #667eea';
        });
        wrapper.addEventListener('dragleave', function() {
            wrapper.style.borderLeft = '';
        });
        wrapper.addEventListener('drop', function(e) {
            e.preventDefault();
            wrapper.style.borderLeft = '';
            if (_dragSrcEl !== wrapper) {
                var allItems = Array.prototype.slice.call(grid.children);
                var srcIdx = allItems.indexOf(_dragSrcEl);
                var dstIdx = allItems.indexOf(wrapper);
                if (srcIdx < dstIdx) {
                    grid.insertBefore(_dragSrcEl, wrapper.nextSibling);
                } else {
                    grid.insertBefore(_dragSrcEl, wrapper);
                }
            }
            return false;
        });

        grid.appendChild(wrapper);
        return wrapper;
    }

    window.openPromotionModal = function() {
        _promotionFiles = [];
        document.getElementById('promotion-preview-grid').innerHTML = '';
        document.getElementById('promotion-text').value = '';
        document.getElementById('promotion-error').textContent = '';
        document.getElementById('promotion-image-input').value = '';

        App.showModal('promotion-modal');

        App.apiGet('/api/activity/get_promotion').then(function(res) {
            if (res.data.code !== 0) { return; }
            var data = res.data.data;
            var grid = document.getElementById('promotion-preview-grid');
            if (data.images) {
                for (var i = 0; i < data.images.length; i++) {
                    addPromotionPreviewItem(grid, data.images[i].path, data.images[i].id, true);
                }
            }
            if (data.text) {
                document.getElementById('promotion-text').value = data.text;
            }
        }).catch(function() {});
    };

    window.handlePromotionImageSelect = function(e) {
        var files = e.target.files;
        if (!files || files.length === 0) { return; }
        var grid = document.getElementById('promotion-preview-grid');
        var remaining = files.length;

        for (var i = 0; i < files.length; i++) {
            (function(file) {
                App.compressImage(file, 1080, 0.85, function(data, name) {
                    var el = addPromotionPreviewItem(grid, 'data:image/jpeg;base64,' + data, 0, false);
                    _promotionFiles.push({ data: data, filename: name, _el: el });
                    remaining--;
                    if (remaining === 0) { e.target.value = ''; }
                });
            })(files[i]);
        }
    };

    window.savePromotion = function() {
        var errEl = document.getElementById('promotion-error');
        errEl.textContent = '';
        var saveBtn = document.getElementById('promotion-save-btn');
        saveBtn.disabled = true;

        var text = document.getElementById('promotion-text').value;
        var uploadErrors = 0;

        function saveSortOrder(done) {
            var grid = document.getElementById('promotion-preview-grid');
            var items = grid.querySelectorAll('[data-image-id]');
            if (items.length === 0) { done(); return; }
            var orders = [];
            for (var i = 0; i < items.length; i++) {
                var id = parseInt(items[i].dataset.imageId, 10);
                if (id > 0) { orders.push({ id: id, sort_order: i }); }
            }
            if (orders.length === 0) { done(); return; }
            App.apiPost('/api/activity/sort_promotion_images', { orders: orders }).then(function() {
                done();
            }).catch(function() { done(); });
        }

        function finishSave() {
            saveSortOrder(function() {
                App.apiPost('/api/activity/update_promotion_text', {
                    content: text
                }).then(function(res) {
                    saveBtn.disabled = false;
                    if (res.data.code !== 0) {
                        errEl.textContent = res.data.message || '保存失败';
                        return;
                    }
                    App.hideModal('promotion-modal');
                    if (uploadErrors > 0) {
                        App.showToast(uploadErrors + '张图片上传失败，其他已保存', 'error');
                    } else {
                        App.showToast('保存成功', 'success');
                    }
                }).catch(function() {
                    saveBtn.disabled = false;
                    errEl.textContent = '网络错误';
                });
            });
        }

        if (_promotionFiles.length === 0) {
            finishSave();
            return;
        }

        var pendingUploads = _promotionFiles.length;
        for (var pi = 0; pi < _promotionFiles.length; pi++) {
            (function(pf) {
                App.apiPost('/api/activity/upload_promotion_image', {
                    filename: pf.filename,
                    data: pf.data
                }).then(function(res) {
                    if (res.data.code === 0 && res.data.data) {
                        pf.imageId = res.data.data.image_id;
                        if (pf._el) {
                            pf._el.dataset.imageId = res.data.data.image_id;
                        }
                    } else {
                        uploadErrors++;
                    }
                    pendingUploads--;
                    if (pendingUploads === 0) { finishSave(); }
                }).catch(function() {
                    uploadErrors++;
                    pendingUploads--;
                    if (pendingUploads === 0) { finishSave(); }
                });
            })(_promotionFiles[pi]);
        }
    };

    /* ---- Activity Notice ---- */
    window.openNoticeModal = function() {
        document.getElementById('notice-text').value = '';
        App.showModal('notice-modal');
        App.apiGet('/api/activity/get_notice').then(function(res) {
            if (res.data.code === 0 && res.data.data && res.data.data.content) {
                document.getElementById('notice-text').value = res.data.data.content;
            }
        }).catch(function() {});
    };

    window.saveNotice = function() {
        var btn = document.getElementById('notice-save-btn');
        btn.disabled = true;
        var content = document.getElementById('notice-text').value;
        App.apiPost('/api/activity/update_notice', { content: content }).then(function(res) {
            if (res.data.code === 0) {
                App.showToast('保存成功', 'success');
                App.hideModal('notice-modal');
            } else {
                App.showToast('保存失败', 'error');
            }
        }).catch(function() {
            App.showToast('保存失败', 'error');
        }).finally(function() {
            btn.disabled = false;
        });
    };

    /* ---- About Us ---- */
    var _aboutUsCards = [];
    var _aboutUsDragSrc = null;

    var _layoutLabels = [
        '', '上图下文', '左图右文', '右图左文', '上文下图',
        '小图左+文字环绕', '小图右+文字环绕', '全图背景+文字叠加',
        '小图顶部居中+文字在下', '文字在上+小图底部居中'
    ];

    function renderAboutUsCard(card) {
        var el = document.createElement('div');
        el.className = 'aboutus-card-item';
        el.setAttribute('draggable', 'true');
        el.style.cssText = 'border:1px solid #e2e8f0; border-radius:8px; padding:12px; margin-bottom:8px; background:#f7fafc; position:relative;';

        var handle = document.createElement('span');
        handle.textContent = '\u2630';
        handle.style.cssText = 'position:absolute; top:8px; left:8px; width:20px; height:20px; border-radius:50%; background:#667eea; color:#fff; font-size:12px; line-height:20px; text-align:center; cursor:move;';
        el.appendChild(handle);

        var delBtn = document.createElement('button');
        delBtn.textContent = '\u00d7';
        delBtn.style.cssText = 'position:absolute; top:6px; right:6px; width:24px; height:24px; border-radius:50%; background:#e53e3e; color:#fff; border:none; cursor:pointer; font-size:16px; line-height:24px; text-align:center; padding:0;';
        delBtn.onclick = function() {
            if (card.id) {
                card._deleted = true;
            }
            el.remove();
        };
        el.appendChild(delBtn);

        var imgRow = document.createElement('div');
        imgRow.style.cssText = 'display:flex; align-items:center; gap:8px; margin-top:28px; margin-bottom:8px;';

        var imgPreview = document.createElement('img');
        imgPreview.style.cssText = 'width:80px; height:80px; object-fit:cover; border-radius:4px; background:#eee;';
        imgPreview.src = card.image_path || '';
        if (!card.image_path) { imgPreview.style.display = 'none'; }
        imgRow.appendChild(imgPreview);

        var imgInput = document.createElement('input');
        imgInput.type = 'file';
        imgInput.accept = 'image/*';
        imgInput.style.cssText = 'flex:1; font-size:13px;';
        imgInput.onchange = function() {
            var file = imgInput.files[0];
            if (!file) { return; }
            App.compressImage(file, 1080, 0.85, function(base64, fname) {
                card._newImage = { data: base64, filename: fname };
                imgPreview.src = 'data:image/jpeg;base64,' + base64;
                imgPreview.style.display = '';
            });
        };
        imgRow.appendChild(imgInput);
        el.appendChild(imgRow);

        var textArea = document.createElement('textarea');
        textArea.className = 'form-control';
        textArea.rows = 3;
        textArea.placeholder = '输入文字内容';
        textArea.style.cssText = 'margin-bottom:8px; font-size:13px;';
        textArea.value = card.text_content || '';
        el.appendChild(textArea);

        var layoutSelect = document.createElement('select');
        layoutSelect.className = 'form-control';
        layoutSelect.style.cssText = 'font-size:13px;';
        for (var i = 1; i <= 9; i++) {
            var opt = document.createElement('option');
            opt.value = i;
            opt.textContent = i + '. ' + _layoutLabels[i];
            if (i === (card.layout_type || 1)) { opt.selected = true; }
            layoutSelect.appendChild(opt);
        }
        el.appendChild(layoutSelect);

        el.addEventListener('dragstart', function(e) {
            _aboutUsDragSrc = el;
            el.style.opacity = '0.4';
        });
        el.addEventListener('dragend', function() {
            el.style.opacity = '';
        });
        el.addEventListener('dragover', function(e) {
            e.preventDefault();
            e.dataTransfer.dropEffect = 'move';
        });
        el.addEventListener('drop', function(e) {
            e.preventDefault();
            if (_aboutUsDragSrc && _aboutUsDragSrc !== el) {
                var list = document.getElementById('aboutus-card-list');
                var items = Array.prototype.slice.call(list.children);
                var srcIdx = items.indexOf(_aboutUsDragSrc);
                var dstIdx = items.indexOf(el);
                if (srcIdx < dstIdx) {
                    list.insertBefore(_aboutUsDragSrc, el.nextSibling);
                } else {
                    list.insertBefore(_aboutUsDragSrc, el);
                }
            }
        });

        card._el = el;
        card._textArea = textArea;
        card._layoutSelect = layoutSelect;
        return el;
    }

    window.openAboutUsModal = function() {
        _aboutUsCards = [];
        document.getElementById('aboutus-card-list').innerHTML = '';
        document.getElementById('aboutus-error').textContent = '';
        App.showModal('aboutus-modal');

        App.apiGet('/api/activity/get_about_us_cards').then(function(res) {
            if (res.data.code !== 0) { return; }
            var cards = res.data.data || [];
            for (var i = 0; i < cards.length; i++) {
                var card = {
                    id: cards[i].id,
                    image_path: cards[i].image_path,
                    text_content: cards[i].text_content,
                    layout_type: cards[i].layout_type
                };
                _aboutUsCards.push(card);
                document.getElementById('aboutus-card-list').appendChild(renderAboutUsCard(card));
            }
        }).catch(function() {});
    };

    window.addAboutUsCard = function() {
        var card = { id: 0, image_path: '', text_content: '', layout_type: 1 };
        _aboutUsCards.push(card);
        document.getElementById('aboutus-card-list').appendChild(renderAboutUsCard(card));
    };

    window.saveAboutUs = function() {
        var btn = document.getElementById('aboutus-save-btn');
        var errEl = document.getElementById('aboutus-error');
        btn.disabled = true;
        errEl.textContent = '';

        var list = document.getElementById('aboutus-card-list');
        var cardEls = Array.prototype.slice.call(list.children);

        var deletedCards = [];
        var newCards = [];
        var existingCards = [];

        for (var i = 0; i < _aboutUsCards.length; i++) {
            var c = _aboutUsCards[i];
            if (c._deleted) {
                deletedCards.push(c);
            } else if (!c.id) {
                newCards.push(c);
            } else {
                existingCards.push(c);
            }
        }

        var idx = 0;
        var totalOps = deletedCards.length + newCards.length + existingCards.length;

        function done() {
            idx++;
            if (idx < totalOps) { return; }
            var orders = [];
            for (var j = 0; j < cardEls.length; j++) {
                var cid = 0;
                for (var k = 0; k < _aboutUsCards.length; k++) {
                    if (_aboutUsCards[k]._el === cardEls[j] && _aboutUsCards[k].id) {
                        cid = _aboutUsCards[k].id;
                        break;
                    }
                }
                if (cid) { orders.push({ id: cid, sort_order: j }); }
            }
            if (orders.length > 0) {
                App.apiPost('/api/activity/sort_about_us_cards', { orders: orders }).then(function() {
                    App.showToast('保存成功', 'success');
                    App.hideModal('aboutus-modal');
                }).catch(function() {
                    errEl.textContent = '排序保存失败';
                }).finally(function() { btn.disabled = false; });
            } else {
                App.showToast('保存成功', 'success');
                App.hideModal('aboutus-modal');
                btn.disabled = false;
            }
        }

        for (var d = 0; d < deletedCards.length; d++) {
            (function(card) {
                App.apiPost('/api/activity/delete_about_us_card', { card_id: card.id }).then(function(res) {
                    if (res.data.code !== 0) { errEl.textContent = '删除卡片失败'; }
                    done();
                }).catch(function() { errEl.textContent = '网络错误'; done(); });
            })(deletedCards[d]);
        }

        for (var n = 0; n < newCards.length; n++) {
            (function(card) {
                if (!card._newImage) { done(); return; }
                var layoutVal = parseInt(card._layoutSelect.value) || 1;
                var textVal = card._textArea.value;
                App.apiPost('/api/activity/upload_about_us_card_image', {
                    filename: card._newImage.filename,
                    data: card._newImage.data,
                    layout_type: layoutVal,
                    text: textVal
                }).then(function(res) {
                    if (res.data.code === 0) {
                        card.id = res.data.data.card_id;
                        done();
                    } else { errEl.textContent = '上传失败'; done(); }
                }).catch(function() { errEl.textContent = '网络错误'; done(); });
            })(newCards[n]);
        }

        for (var e = 0; e < existingCards.length; e++) {
            (function(card) {
                var textVal = card._textArea.value;
                var layoutVal = parseInt(card._layoutSelect.value) || 1;
                var payload = { card_id: card.id, text: textVal, layout_type: layoutVal };
                if (card._newImage) {
                    payload.filename = card._newImage.filename;
                    payload.data = card._newImage.data;
                }
                App.apiPost('/api/activity/update_about_us_card', payload).then(function(res) {
                    if (res.data.code !== 0) { errEl.textContent = '更新卡片失败'; }
                    done();
                }).catch(function() { errEl.textContent = '网络错误'; done(); });
            })(existingCards[e]);
        }

        if (deletedCards.length + newCards.length + existingCards.length === 0) { done(); }
    };

    init();
})();
