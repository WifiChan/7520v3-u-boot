function ajax(opt) {
    var xhr = (window.XMLHttpRequest) ? new XMLHttpRequest() : new ActiveXObject('Microsoft.XMLHTTP');

    if (opt.progress && xhr.upload) {
        xhr.upload.addEventListener('progress', opt.progress);
    }

    xhr.onreadystatechange = function () {
        if (xhr.readyState === 4) {
            if (xhr.status === 200) {
                if (opt.done) opt.done(xhr.responseText);
            } else {
                if (opt.fail) opt.fail(xhr.status);
            }
        }
    };

    if (opt.timeout) xhr.timeout = opt.timeout;

    var method = opt.data ? 'POST' : 'GET';
    xhr.open(method, opt.url, true);
    xhr.send(opt.data || null);
}

function startup() {
    getVersion();
    getFlashInfo();
    loadPartitions();
    loadBootargs();
}

function switchTab(tabId) {
    var tabs = document.querySelectorAll('.tab-btn');
    var contents = document.querySelectorAll('.tab-content');

    tabs.forEach(function (btn) {
        btn.classList.remove('active');
    });

    contents.forEach(function (content) {
        content.classList.remove('active');
    });

    var targetTab = document.getElementById('tab_' + tabId);
    if (targetTab) {
        targetTab.classList.add('active');
    }

    var eventTarget = event ? (event.target || event.srcElement) : null;
    if (eventTarget && eventTarget.classList.contains('tab-btn')) {
        eventTarget.classList.add('active');
    } else {
        tabs.forEach(function(b) {
            if (b.getAttribute('onclick') && b.getAttribute('onclick').indexOf(tabId) !== -1) {
                b.classList.add('active');
            }
        });
    }

    if (tabId === 'partitions') {
        loadPartitions();
    } else if (tabId === 'bootargs') {
        loadBootargs();
    }
}

function getVersion() {
    ajax({
        url: '/version',
        done: function (ver) {
            var el = document.getElementById('version');
            if (el) el.innerText = ver;
            var badge = document.getElementById('uboot_badge');
            if (badge && ver) {
                var m = ver.match(/U-Boot\s+\d+\.\d+(?:-rc\d+)?/);
                if (m) badge.innerText = m[0];
            }
        }
    });
}

function getFlashInfo() {
    ajax({
        url: '/get_flash_info',
        done: function (info) {
            var el = document.getElementById('flash_info');
            if (el) el.innerText = info;
        },
        fail: function () {
            var el = document.getElementById('flash_info');
            if (el) el.innerText = "SPI NOR / NAND";
        }
    });
}

function switchFlashProfile(sizeMb) {
    if (!confirm('确定要手动切换存储介质为 ' + sizeMb + 'MB 闪存配置吗？\n(无需接 TTL，各分区限额和全盘备份将立即更新)')) return;
    ajax({
        url: '/set_flash_profile?size=' + sizeMb,
        done: function (res) {
            alert('已成功切换为 ' + sizeMb + 'MB 闪存配置！');
            getFlashInfo();
            loadPartitions();
            loadBootargs();
        },
        fail: function () {
            alert('切换失败！');
        }
    });
}

function selectType(type) {
    var cards = document.querySelectorAll('.radio-card');
    cards.forEach(function (card) {
        var input = card.querySelector('input');
        if (input && input.value === type) {
            card.classList.add('selected');
            input.checked = true;
        } else {
            card.classList.remove('selected');
        }
    });

    var customWrap = document.getElementById('custom_input_wrap');
    if (customWrap) {
        customWrap.style.display = (type === 'custom') ? 'block' : 'none';
    }
}

function fileSelected(input) {
    if (input.files && input.files[0]) {
        var f = input.files[0];
        var sizeStr = (f.size / (1024 * 1024)).toFixed(2) + ' MB (' + f.size + ' 字节)';
        document.getElementById('file_label').innerHTML = '<strong>' + f.name + '</strong><br>' + sizeStr;
    }
}

function startUpload() {
    var fileInput = document.getElementById('file');
    if (!fileInput || !fileInput.files || !fileInput.files[0]) {
        alert('请先选择要刷写的固件文件！');
        return;
    }

    var file = fileInput.files[0];
    var selectedType = 'firmware';
    var checkedRadio = document.querySelector('input[name="flash_type"]:checked');
    if (checkedRadio) selectedType = checkedRadio.value;

    var targetPart = selectedType;
    if (selectedType === 'custom') {
        targetPart = document.getElementById('custom_target').value.trim();
    } else if (selectedType === 'firmware') {
        targetPart = 'imagefs';
    }

    /* 大于 16MB 的超大固件自动采用 1MB 步进擦除 + 256KB 切片流式写入与回读校验 */
    if (file.size > 16 * 1024 * 1024) {
        startChunkedUpload(file, targetPart);
        return;
    }

    var stageTitle = document.getElementById('upload_stage_title');
    if (stageTitle) stageTitle.innerText = '上传进度';

    var form = new FormData();
    form.append(selectedType, fileInput.files[0]);

    if (selectedType === 'custom') {
        var customTarget = document.getElementById('custom_target').value.trim();
        form.append('custom_target', customTarget);
    }

    document.getElementById('form').style.display = 'none';
    document.getElementById('upload_progress_wrap').style.display = 'block';

    ajax({
        url: '/upload',
        data: form,
        progress: function (e) {
            if (e.lengthComputable) {
                var pct = Math.round((e.loaded / e.total) * 100);
                document.getElementById('bar').style.width = pct + '%';
                document.getElementById('percent_txt').innerText = pct + '%';
            }
        },
        done: function (resp) {
            if (resp === 'fail') {
                location = '/fail.html';
                return;
            }
            var parts = resp.split(' ');
            var size = parts[0] ? (parseInt(parts[0]) / 1024).toFixed(1) + ' KB (' + parts[0] + ' 字节)' : '-';
            var md5 = parts[1] || '-';

            document.getElementById('v_size').innerText = size;
            document.getElementById('v_md5').innerText = md5;

            var descMap = {
                'rootfs': 'rootfs (根文件系统)',
                'imagefs': 'imagefs (系统内核/固件)',
                'uboot': 'uboot (U-Boot 引导程序)',
                'nvrofs': 'nvrofs (出厂射频校准参数)',
                'userdata': 'userdata (用户数据分区)',
                'zloader': 'zloader (Stage 1 引导)',
                'full': '全盘镜像 (从 0x0 完整覆盖 Flash)',
                'custom': '指定分区: (' + (document.getElementById('custom_target').value.trim()) + ')'
            };
            document.getElementById('v_target').innerText = descMap[selectedType] || ('自定义目标: ' + selectedType);

            document.getElementById('upload_progress_wrap').style.display = 'none';
            document.getElementById('verify_card').style.display = 'block';
        },
        fail: function () {
            location = '/fail.html';
        }
    });
}

function startChunkedUpload(file, targetPart) {
    /*
     * Pass 1 (total erase) slices the full-region erase into 1MB keep-alive
     * steps so every request completes within a few seconds while U-Boot also
     * pumps USB RNDIS & TCP ACK keep-alives. Pass 2 (write) streams the file
     * in 256KB chunks.
     */
    var CHUNK_SIZE = 256 * 1024;        // 256 KB write chunk
    var ERASE_CHUNK_SIZE = 1024 * 1024; // 1 MB keep-alive erase step

    if (!confirm('检测到大文件/流式写入 (' + (file.size / (1024 * 1024)).toFixed(1) + ' MB)，系统已自动启用【流式分块安全刷写】技术，每次分块 256KB。\n\n目标分区: [' + targetPart + ']\n确定立即开始分块写入吗？')) {
        return;
    }

    var totalSize = file.size;
    var totalChunks = Math.ceil(totalSize / CHUNK_SIZE);
    var totMb = (totalSize / (1024 * 1024)).toFixed(1);

    document.getElementById('form').style.display = 'none';
    document.getElementById('upload_progress_wrap').style.display = 'block';
    var bar = document.getElementById('bar');
    var pctTxt = document.getElementById('percent_txt');
    var stageTitle = document.getElementById('upload_stage_title');
    if (stageTitle) stageTitle.innerText = '阶段 1/2: 闪存步进总擦除 (1MB/步)';

    var phase = 'erase';
    var retryCount = 0;
    var MAX_RETRIES = 5;

    function sendNext(offset) {
        if (offset >= totalSize) {
            if (phase === 'erase') {
                phase = 'write';
                retryCount = 0;
                if (stageTitle) stageTitle.innerText = '阶段 2/2: 分块流式写入与回读校验 (256KB/块)';
                pctTxt.innerText = '总擦除完成，开始流式写入与回读校验...';
                bar.style.width = '50%';
                sendNext(0);
                return;
            }
            document.getElementById('upload_progress_wrap').style.display = 'none';
            alert('✅ 分块流式刷写与回读校验成功！已完整写入目标分区 [' + targetPart + '] (' + totMb + ' MB)。\n系统将在确认后重启引导。');
            location = '/booting.html';
            return;
        }

        var stepSize = (phase === 'erase') ? ERASE_CHUNK_SIZE : CHUNK_SIZE;
        var end = Math.min(offset + stepSize, totalSize);
        var chunkLen = end - offset;
        /* first half of the bar for erase, second half for write */
        var basePct = (phase === 'erase') ? 0 : 50;

        var form = new FormData();
        form.append('mode', phase);
        form.append('part', targetPart);
        form.append('offset', offset.toString());
        if (phase === 'erase') {
            form.append('len', chunkLen.toString());
        } else {
            form.append('chunk', file.slice(offset, end));
        }

        var curMb = (offset / (1024 * 1024)).toFixed(1);
        if (phase === 'erase') {
            pctTxt.innerText = '正在总擦除闪存 (连接保活): ' + curMb + 'MB / ' + totMb + 'MB ...';
        } else {
            var chunkIdx = Math.floor(offset / CHUNK_SIZE) + 1;
            pctTxt.innerText = '正在写入并校验 [' + chunkIdx + '/' + totalChunks + ' 块] ' +
                               curMb + 'MB / ' + totMb + 'MB ...';
        }

        var xhr = new XMLHttpRequest();
        xhr.open('POST', '/flash_chunk', true);
        xhr.timeout = 300000;

        if (phase === 'erase') {
            bar.style.width = (basePct + Math.round((offset / totalSize) * 50)) + '%';
        } else {
            xhr.upload.onprogress = function (e) {
                if (e.lengthComputable) {
                    var pct = basePct + Math.round(((offset + e.loaded) / totalSize) * 50);
                    bar.style.width = Math.min(99, pct) + '%';
                }
            };
        }

        xhr.onload = function () {
            var res = null;
            try {
                res = JSON.parse(xhr.responseText);
            } catch (e) {}

            if (xhr.status === 200 && res && res.status === 'ok') {
                retryCount = 0;
                bar.style.width = (basePct + Math.round((end / totalSize) * 50)) + '%';
                if (phase === 'erase') {
                    pctTxt.innerText = '正在总擦除闪存: 已擦除 ' + (end / (1024 * 1024)).toFixed(1) + 'MB / ' + totMb + 'MB (本步 ' + res.erase_ms + 'ms)';
                } else {
                    pctTxt.innerText = '已写入并校验 ' + (end / (1024 * 1024)).toFixed(1) + 'MB / ' + totMb + 'MB — 本块用时 ' + res.write_ms + 'ms';
                }
                sendNext(end);
            } else {
                var errDetail = (res && res.msg) ? res.msg : ('HTTP ' + xhr.status + ': ' + (xhr.responseText || '写入失败'));
                alert('分片刷写失败: ' + errDetail);
                location = '/fail.html';
            }
        };

        function handleTransientError() {
            if (retryCount < MAX_RETRIES) {
                retryCount++;
                pctTxt.innerText = '连接保活重连中 (' + retryCount + '/' + MAX_RETRIES + ')，继续当前进度...';
                setTimeout(function () {
                    sendNext(offset);
                }, 1000);
                return;
            }
            alert('网络传输中断，刷写失败！');
            location = '/fail.html';
        }

        xhr.onerror = handleTransientError;
        xhr.ontimeout = handleTransientError;

        xhr.send(form);
    }

    sendNext(0);
}

function onSelectPartDropdown(sel) {
    if (sel && sel.value) {
        var input = document.getElementById('custom_target');
        if (input) input.value = sel.value;
    }
}

function prepareFlashPart(partName) {
    switchTab('flash');
    if (partName === 'rootfs') {
        selectType('rootfs');
    } else if (partName === 'imagefs' || partName === 'firmware') {
        selectType('imagefs');
    } else if (partName === 'uboot') {
        selectType('uboot');
    } else if (partName === 'nvrofs') {
        selectType('nvrofs');
    } else if (partName === 'userdata') {
        selectType('userdata');
    } else if (partName === 'full' || partName === 'raw') {
        selectType('full');
    } else {
        selectType('custom');
        var input = document.getElementById('custom_target');
        if (input) input.value = partName;
        var dd = document.getElementById('part_dropdown');
        if (dd) dd.value = partName;
    }
    var formEl = document.getElementById('form');
    if (formEl) formEl.scrollIntoView({ behavior: 'smooth' });
}

function downloadBackup(part) {
    if (!part) part = 'full';
    window.location.href = '/backup?part=' + encodeURIComponent(part);
}

function downloadCustomBackup() {
    var input = document.getElementById('custom_backup_name');
    var val = input ? input.value.trim() : '';
    if (!val) {
        alert('请输入要备份的分区名称，如 rootfs 或 imagefs');
        return;
    }
    downloadBackup(val);
}

function loadPartitions() {
    var statusEl = document.getElementById('ptable_status');
    var tbody = document.getElementById('partition_tbody');
    var dropdown = document.getElementById('part_dropdown');

    ajax({
        url: '/get_partitions',
        done: function (resp) {
            try {
                var data = JSON.parse(resp);
                if (statusEl) {
                    if (!data.parts || data.parts.length === 0) {
                        statusEl.className = 'alert alert-danger';
                        statusEl.innerHTML = '❌ 闪存探测失败或未就绪 (硬件未应答，已禁用写入/备份)';
                    } else if (data.has_ptable) {
                        statusEl.className = 'alert alert-success';
                        statusEl.innerHTML = '✔ 已自动识别中兴 (ZTE) 闪存动态分区表 (Magic: 0x31594876)';
                    } else {
                        statusEl.className = 'alert alert-info';
                        statusEl.innerHTML = 'ℹ 未在 Flash 检测到专用分区表头，当前使用标准静态 MTD 分区布局';
                    }
                }

                if (dropdown && data.parts) {
                    var curVal = dropdown.value;
                    var dhtml = '<option value="">-- 选择已识别分区 --</option>';
                    for (var d = 0; d < data.parts.length; d++) {
                        var dp = data.parts[d];
                        dhtml += '<option value="' + dp.name + '">' + dp.name + ' (' + dp.size + ')</option>';
                    }
                    dropdown.innerHTML = dhtml;
                    if (curVal) dropdown.value = curVal;
                }

                if (tbody && data.parts && data.parts.length > 0) {
                    var html = '';
                    for (var i = 0; i < data.parts.length; i++) {
                        var p = data.parts[i];
                        html += '<tr>' +
                            '<td>' + (i + 1) + '</td>' +
                            '<td><strong>' + p.name + '</strong></td>' +
                            '<td><code>' + p.offset + '</code></td>' +
                            '<td>' + p.size + '</td>' +
                            '<td>' +
                            '<button class="btn btn-sm btn-primary" onclick="prepareFlashPart(\'' + p.name + '\')">刷入此分区</button> ' +
                            '<button class="btn btn-sm btn-secondary" onclick="downloadBackup(\'' + p.name + '\')" style="margin-left:4px;">备份导出</button>' +
                            '</td>' +
                            '</tr>';
                    }
                    tbody.innerHTML = html;
                } else if (tbody) {
                    tbody.innerHTML = '<tr><td colspan="5" style="color:#ef4444; text-align:center; padding:18px; font-weight:bold;">❌ 闪存未检测到或探测失败，暂无可用分区。<br><span style="font-size:12px; font-weight:normal; color:#4b5563;">如确认硬件连接正常，可在顶部点击【⚡ 强制设为 16MB/8MB】进行手动强制配置</span></td></tr>';
                }
            } catch (e) {
                if (tbody) tbody.innerHTML = '<tr><td colspan="5" style="color:red; text-align:center;">解析分区数据异常</td></tr>';
            }
        },
        fail: function () {
            if (tbody) tbody.innerHTML = '<tr><td colspan="5" style="color:red; text-align:center;">获取分区信息失败</td></tr>';
        }
    });
}

function loadBootargs() {
    var input = document.getElementById('bootargs_input');
    if (!input) return;

    ajax({
        url: '/get_bootargs',
        done: function (resp) {
            input.value = resp.trim();
        }
    });
}

function applyPreset(preset) {
    var input = document.getElementById('bootargs_input');
    if (!input) return;

    /* 手动预设：控制台固定为 ttyS1；root 设备统一为 mtdblock4
     * （默认值由后端 /get_bootargs 依据真实分区表自动生成，这里仅为手动覆盖用）。 */
    var presets = {
        'openwrt': 'console=ttyS1,115200 root=/dev/mtdblock4 rootfstype=squashfs,jffs2 noinitrd init=/sbin/init',
        'mf761w': 'console=ttyS1,115200 root=/dev/mtdblock4 rootfstype=squashfs,jffs2 init=/init',
        'am206': 'console=ttyS1,115200 root=/dev/mtdblock4 rootfstype=squashfs,jffs2 init=/init',
        'debug': 'console=ttyS1,115200 earlyprintk rootwait ignore_loglevel'
    };

    if (presets[preset]) {
        input.value = presets[preset];
    }
}

function saveBootargs() {
    var input = document.getElementById('bootargs_input');
    var statusEl = document.getElementById('bootargs_status');
    var btn = document.getElementById('btn_save_bootargs');
    if (!input) return;

    var val = input.value.trim();
    if (!val) {
        alert('启动参数不能为空！');
        return;
    }

    if (btn) btn.disabled = true;
    if (statusEl) {
        statusEl.style.color = '#2563eb';
        statusEl.innerText = '正在保存并写入 Flash...';
    }

    ajax({
        url: '/set_bootargs?args=' + encodeURIComponent(val),
        done: function (resp) {
            if (btn) btn.disabled = false;
            if (statusEl) {
                statusEl.style.color = '#16a34a';
                statusEl.innerText = '✔ ' + resp;
                setTimeout(function () { statusEl.innerText = ''; }, 4000);
            }
        },
        fail: function () {
            if (btn) btn.disabled = false;
            if (statusEl) {
                statusEl.style.color = '#dc2626';
                statusEl.innerText = '❌ 保存失败，请重试';
            }
        }
    });
}

function rebootDevice() {
    if (!confirm('确定要立即重启设备吗？')) return;
    document.body.innerHTML =
        '<div class="container small-container">' +
            '<div class="card status-card">' +
                '<div class="spinner"></div>' +
                '<h2>设备正在重启...</h2>' +
                '<p>设备已退出 Failsafe 恢复模式并正在执行硬件重启，请稍候。</p>' +
                '<div id="reboot_countdown" class="countdown-txt">5 秒后自动尝试重新连接...</div>' +
            '</div>' +
        '</div>';
    ajax({ url: '/reboot' });
    var count = 5;
    var timer = setInterval(function () {
        var el = document.getElementById('reboot_countdown');
        if (el) el.innerHTML = count + ' 秒后自动尝试重新连接...';
        count--;
        if (count < 0) {
            clearInterval(timer);
            location = '/';
        }
    }, 1000);
}
