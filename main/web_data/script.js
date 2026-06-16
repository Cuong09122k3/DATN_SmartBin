let lastId = -1;
let displayFrames = 5;
let isManualOperating = false;

function fetchStatus() {
    fetch('/status')
        .then(r => r.json())
        .then(data => {
            const b1 = document.getElementById('bin1-status');
            const b2 = document.getElementById('bin2-status');

            b1.innerText = data.bin1_full ? "ĐẦY" : "CHƯA ĐẦY";
            b1.className = data.bin1_full ? "bin-state state-full" : "bin-state state-empty";

            b2.innerText = data.bin2_full ? "ĐẦY" : "CHƯA ĐẦY";
            b2.className = data.bin2_full ? "bin-state state-full" : "bin-state state-empty";

            document.getElementById('warning-banner').className = (data.bin1_full || data.bin2_full) ? "" : "hidden";
            const predEl = document.getElementById('prediction-result');
            if (predEl) {
                predEl.innerText = data.prediction || "Đang chờ rác...";
            }

            const framesCount = data.display_frames || displayFrames;
            try {
                let html = '';
                // Hien thi tom tat ket qua vote ngan gon
                if (data.frames && data.frames.length > 0 && data.frames[0].label !== '---') {
                    // Tim ket qua vote cao nhat
                    let maxVote = 0, maxName = '', totalConf = 0, validCount = 0;
                    const threshold = data.threshold || 70;
                    if (data.probs) {
                        for (const [k, v] of Object.entries(data.probs)) {
                            if (v && typeof v === 'object' && v.vote > maxVote) {
                                maxVote = v.vote;
                                maxName = k;
                            }
                        }
                    }

                    // Tinh trung binh confidence cua cac frame dat nguong
                    for (let i = 0; i < framesCount; i++) {
                        if (data.frames[i] && data.frames[i].label !== '---') {
                            if (data.frames[i].conf >= threshold) {
                                totalConf += data.frames[i].conf;
                                validCount++;
                            }
                        }
                    }
                    const avgConf = validCount > 0 ? (totalConf / validCount) : 0;

                    // Dong tong ket vote
                    html += `<div class="vote-summary">`;
                    if (maxVote > 0) {
                        html += `<div class="vote-title"><strong>Kết quả vote: ${maxName} ${maxVote}/${framesCount} phiếu (TB: ${avgConf.toFixed(2)}%)</strong></div>`;
                    } else {
                        html += `<div class="vote-title"><strong>Kết quả vote: Không xác định 0/${framesCount} phiếu (TB: 0.00%)</strong></div>`;
                    }

                    // Liet ke ket qua tung frame
                    let frameList = [];
                    for (let i = 0; i < framesCount; i++) {
                        const f = data.frames[i];
                        if (f && f.label !== '---') {
                            frameList.push(`L${i + 1}: ${f.label} ${f.conf.toFixed(2)}%`);
                        } else {
                            frameList.push(`L${i + 1}: ---`);
                        }
                    }
                    html += `<div class="vote-frames"><strong>${frameList.join(' | ')}</strong></div>`;
                    html += `</div>`;
                } else if (data.prediction && data.prediction !== "Đang chờ rác..." && data.prediction !== "Sẵn sàng...") {
                    html = `<div class="vote-summary"><div class="vote-title">${data.prediction}</div></div>`;
                } else {
                    html = `<p style="text-align:center; color: #a0a8c0;">Đang chờ dữ liệu phân loại...</p>`;
                }
                document.getElementById('voting-results-container').innerHTML = html;
            } catch (err) {
                console.error("Loi render voting:", err);
                document.getElementById('voting-results-container').innerHTML = `<p style="text-align:center; color: #dc3545;">Lỗi xử lý dữ liệu voting</p>`;
            }

            if (data.prediction_id !== lastId && data.prediction_id > 0) {
                lastId = data.prediction_id;
                const ts = Date.now();
                for (let i = 0; i < framesCount; i++) {
                    setTimeout(() => {
                        const el = document.getElementById('ai-img-' + i);
                        if (el) el.src = `/photo_infer?id=${i}&t=${ts}`;
                    }, i * 100);
                }
            }

            // Hien thi ket qua du doan rieng tung frame duoi moi anh
            if (data.frames && data.frames.length > 0) {
                for (let i = 0; i < framesCount; i++) {
                    const labelEl = document.getElementById('ai-label-' + i);
                    if (labelEl && data.frames[i]) {
                        const f = data.frames[i];
                        if (f.label !== '---') {
                            labelEl.innerText = f.label + ' (' + f.conf.toFixed(2) + '%)';
                        } else {
                            labelEl.innerText = '---';
                        }
                    }
                }
            }

            // Tự động vô hiệu hóa nút điều khiển thủ công 
            const btnOpen1 = document.getElementById('btn-open-bin1');
            const btnOpen2 = document.getElementById('btn-open-bin2');

            if (!isManualOperating) {
                if (btnOpen1) {
                    btnOpen1.disabled = data.busy || data.bin1_full;
                    btnOpen1.title = data.bin1_full ? "Không thể bấm: Ngăn 1 đã đầy!" : (data.busy ? "Không thể bấm: Hệ thống đang bận!" : "Mở Ngăn 1 (Rác Pin)");
                }
                if (btnOpen2) {
                    btnOpen2.disabled = data.busy || data.bin2_full;
                    btnOpen2.title = data.bin2_full ? "Không thể bấm: Ngăn 2 đã đầy!" : (data.busy ? "Không thể bấm: Hệ thống đang bận!" : "Mở Ngăn 2 (Rác Thường)");
                }
            }

            // Cập nhật trạng thái kết nối
            document.getElementById('sys-status').className = "status-dot status-online";
            document.getElementById('sys-text').innerText = "Online";
        })
        .catch(e => {
            document.getElementById('sys-status').className = "status-dot status-offline";
            document.getElementById('sys-text').innerText = "Offline";
        });
}

const btnCapture = document.getElementById('btn-capture');
if (btnCapture) {
    btnCapture.addEventListener('click', () => {
        btnCapture.innerText = "Đang chụp...";
        btnCapture.disabled = true;

        fetch('/capture', { method: 'POST' }).then(r => {
            if (r.ok) {
                setTimeout(() => {
                    const m = document.getElementById('manual-img');
                    if (m) {
                        m.src = `/photo?t=${Date.now()}`;
                        m.classList.remove('hidden');
                    }
                    btnCapture.innerText = "Chụp ảnh thùng rác";
                    btnCapture.disabled = false;
                }, 1000);
            } else {
                btnCapture.innerText = "Lỗi chụp ảnh";
                setTimeout(() => {
                    btnCapture.innerText = "Chụp ảnh thùng rác";
                    btnCapture.disabled = false;
                }, 2000);
            }
        });
    });
}

setInterval(fetchStatus, 2000);
fetchStatus();

// === Cấu hình WiFi ===
const wifiToggleBtn = document.getElementById('wifi-toggle-btn');
const wifiPanelContent = document.getElementById('wifi-panel-content');
const wifiPanel = document.querySelector('.collapsible-panel');

if (wifiToggleBtn && wifiPanelContent && wifiPanel) {
    wifiToggleBtn.addEventListener('click', () => {
        const isHidden = wifiPanelContent.classList.toggle('hidden');
        if (isHidden) {
            wifiPanel.classList.remove('expanded');
        } else {
            wifiPanel.classList.add('expanded');
        }
    });
}

const btnWifiScan = document.getElementById('btn-wifi-scan');
const wifiList = document.getElementById('wifi-list');
const wifiSsidInput = document.getElementById('wifi-ssid');
const wifiPassInput = document.getElementById('wifi-pass');
const btnWifiConnect = document.getElementById('btn-wifi-connect');
const btnWifiAp = document.getElementById('btn-wifi-ap');
const wifiMsg = document.getElementById('wifi-msg');

function showWifiMsg(text, type = 'info') {
    wifiMsg.innerText = text;
    wifiMsg.className = `status-message ${type}`;
    wifiMsg.classList.remove('hidden');
}

function hideWifiMsg() {
    wifiMsg.classList.add('hidden');
}

// Quét WiFi
if (btnWifiScan && wifiList) {
    btnWifiScan.addEventListener('click', () => {
        btnWifiScan.innerText = "Đang quét...";
        btnWifiScan.disabled = true;
        hideWifiMsg();
        wifiList.classList.add('hidden');
        wifiList.innerHTML = '';

        fetch('/wifi/scan')
            .then(res => {
                if (!res.ok) throw new Error("Không thể quét WiFi");
                return res.json();
            })
            .then(data => {
                if (data && data.length > 0) {
                    data.forEach(ap => {
                        const item = document.createElement('div');
                        item.className = 'wifi-item';

                        const ssidSpan = document.createElement('span');
                        ssidSpan.className = 'wifi-ssid-name';
                        ssidSpan.innerText = ap.ssid;

                        const infoRight = document.createElement('div');
                        infoRight.className = 'wifi-info-right';
                        infoRight.innerText = `${ap.rssi} dBm ${ap.auth > 0 ? '(Bảo mật)' : '(Mở)'}`;

                        item.appendChild(ssidSpan);
                        item.appendChild(infoRight);
                        item.addEventListener('click', () => {
                            if (wifiSsidInput) wifiSsidInput.value = ap.ssid;
                            if (wifiPassInput) {
                                wifiPassInput.value = "";
                                wifiPassInput.focus();
                            }
                        });

                        wifiList.appendChild(item);
                    });
                    wifiList.classList.remove('hidden');
                    showWifiMsg(`Đã tìm thấy ${data.length} mạng WiFi.`, 'success');
                } else {
                    showWifiMsg("Không tìm thấy mạng WiFi nào xung quanh.", 'error');
                }
            })
            .catch(err => {
                console.error(err);
                showWifiMsg("Lỗi khi quét WiFi xung quanh.", 'error');
            })
            .finally(() => {
                btnWifiScan.innerText = "Quét WiFi xung quanh";
                btnWifiScan.disabled = false;
            });
    });
}

// Kết nối WiFi mới
if (btnWifiConnect && wifiSsidInput && wifiPassInput) {
    btnWifiConnect.addEventListener('click', () => {
        const ssid = wifiSsidInput.value.trim();
        const password = wifiPassInput.value;

        if (!ssid) {
            showWifiMsg("Vui lòng nhập hoặc chọn SSID WiFi!", "error");
            return;
        }

        if (btnWifiScan) btnWifiScan.disabled = true;
        btnWifiConnect.disabled = true;
        let timeLeft = 15;
        btnWifiConnect.innerText = `Đang kết nối (${timeLeft}s)...`;
        if (btnWifiAp) btnWifiAp.disabled = true;

        showWifiMsg(`Đang kết nối WiFi mới, quá trình này mất tối đa 15 giây (còn ${timeLeft}s)...`, "info");
        const timer = setInterval(() => {
            timeLeft--;
            if (timeLeft > 0) {
                btnWifiConnect.innerText = `Đang kết nối (${timeLeft}s)...`;
                showWifiMsg(`Đang kết nối WiFi mới, quá trình này mất tối đa 15 giây (còn ${timeLeft}s)...`, "info");
            } else {
                clearInterval(timer);
            }
        }, 1000);

        fetch('/wifi/connect', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({ ssid, password })
        })
            .then(res => {
                clearInterval(timer);
                if (!res.ok) throw new Error("Lỗi kết nối");
                return res.json();
            })
            .then(data => {
                if (data.success) {
                    showWifiMsg(`Kết nối thành công! IP mới: ${data.ip}. Đang chạy Station Mode.`, "success");
                    wifiPassInput.value = "";
                } else {
                    showWifiMsg("Kết nối thất bại. Thiết bị đã tự động quay lại chế độ AP Mode.", "error");
                }
            })
            .catch(err => {
                clearInterval(timer);
                console.error(err);
                showWifiMsg("Gửi yêu cầu thất bại hoặc hết thời gian chờ kết nối. Thiết bị đã quay lại AP Mode.", "error");
            })
            .finally(() => {
                clearInterval(timer);
                if (btnWifiScan) btnWifiScan.disabled = false;
                btnWifiConnect.disabled = false;
                btnWifiConnect.innerText = "Kết nối WiFi";
                if (btnWifiAp) btnWifiAp.disabled = false;
            });
    });
}

// Chuyển sang chế độ AP Mode
if (btnWifiAp) {
    btnWifiAp.addEventListener('click', () => {
        const confirmSwitch = confirm("Bạn có chắc muốn chuyển sang chế độ AP Mode?\nThiết bị sẽ dừng kết nối WiFi Router hiện tại và tự phát WiFi 'SmartBin_WiFi' với mật khẩu '12345678'.");
        if (!confirmSwitch) return;

        if (btnWifiScan) btnWifiScan.disabled = true;
        if (btnWifiConnect) btnWifiConnect.disabled = true;
        btnWifiAp.disabled = true;

        let timeLeft = 5;
        btnWifiAp.innerText = `Đang chuyển (${timeLeft}s)...`;
        showWifiMsg(`Đang chuyển sang AP Mode. Thiết bị sẽ phát WiFi sau ${timeLeft} giây...`, "info");

        let isSuccess = false;
        let apSsid = "SmartBin_WiFi";
        let apPass = "12345678";
        let hasError = false;

        fetch('/wifi/ap', { method: 'POST' })
            .then(res => {
                if (!res.ok) throw new Error("Lỗi chuyển đổi");
                return res.json();
            })
            .then(data => {
                if (data.success) {
                    isSuccess = true;
                    apSsid = data.ssid || apSsid;
                    apPass = data.password || apPass;
                } else {
                    hasError = true;
                }
            })
            .catch(err => {
                console.error(err);
                hasError = true;
            });

        const timer = setInterval(() => {
            timeLeft--;
            if (timeLeft > 0) {
                btnWifiAp.innerText = `Đang chuyển (${timeLeft}s)...`;
                showWifiMsg(`Đang chuyển sang AP Mode. Thiết bị sẽ phát WiFi sau ${timeLeft} giây...`, "info");
            } else {
                clearInterval(timer);

                // Khôi phục trạng thái nút bấm 
                if (btnWifiScan) btnWifiScan.disabled = false;
                if (btnWifiConnect) btnWifiConnect.disabled = false;
                btnWifiAp.disabled = false;
                btnWifiAp.innerText = "Phát AP Mode (Phát WiFi)";

                if (hasError) {
                    showWifiMsg("Chuyển đổi sang AP Mode thất bại hoặc thiết bị không phản hồi.", "error");
                } else {
                    showWifiMsg(`Đã chuyển thành công! Vui lòng kết nối vào WiFi: '${apSsid}' mật khẩu: '${apPass}' và truy cập IP: 192.168.4.1`, "success");
                }
            }
        }, 1000);
    });
}

// Xóa WiFi đã lưu và Khởi động lại thiết bị
const btnWifiReset = document.getElementById('btn-wifi-reset');
if (btnWifiReset) {
    btnWifiReset.addEventListener('click', () => {
        const confirmReset = confirm("Bạn có chắc chắn muốn xóa toàn bộ thông tin WiFi đã lưu và khởi động lại thiết bị?\nThiết bị sẽ quay về chế độ phát WiFi (AP Mode) mặc định 'SmartBin_WiFi'.");
        if (!confirmReset) return;

        if (btnWifiScan) btnWifiScan.disabled = true;
        if (btnWifiConnect) btnWifiConnect.disabled = true;
        if (btnWifiAp) btnWifiAp.disabled = true;
        btnWifiReset.disabled = true;

        let timeLeft = 5;
        btnWifiReset.innerText = `Đang xóa & reset (${timeLeft}s)...`;
        showWifiMsg(`Đang gửi yêu cầu xóa cấu hình WiFi và khởi động lại thiết bị...`, "info");

        let isDone = false;

        // Gửi yêu cầu reset
        fetch('/wifi/reset', { method: 'POST' })
            .then(res => {
                if (!res.ok) throw new Error("Lỗi khi reset");
                isDone = true;
            })
            .catch(err => {
                console.error("Fetch reset error (đây là bình thường khi chip reset đột ngột):", err);
                isDone = true;
            });

        const timer = setInterval(() => {
            timeLeft--;
            if (timeLeft > 0) {
                btnWifiReset.innerText = `Đang xóa & reset (${timeLeft}s)...`;
                showWifiMsg(`Thiết bị đang thực hiện xóa bộ nhớ và khởi động lại. Vui lòng kết nối lại sau ${timeLeft} giây...`, "info");
            } else {
                clearInterval(timer);

                // Khôi phục nút bấm sau đếm ngược
                if (btnWifiScan) btnWifiScan.disabled = false;
                if (btnWifiConnect) btnWifiConnect.disabled = false;
                if (btnWifiAp) btnWifiAp.disabled = false;
                btnWifiReset.disabled = false;
                btnWifiReset.innerText = "Xóa WiFi đã lưu & Reset thiết bị";

                if (isDone) {
                    showWifiMsg("Đã xóa toàn bộ cấu hình WiFi đã lưu thành công! Vui lòng kết nối vào mạng WiFi mặc định 'SmartBin_WiFi' (mật khẩu: 12345678) và truy cập IP 192.168.4.1 để thiết lập lại.", "success");
                    if (wifiSsidInput) wifiSsidInput.value = "";
                    if (wifiPassInput) wifiPassInput.value = "";
                } else {
                    showWifiMsg("Yêu cầu xóa cấu hình thất bại hoặc thiết bị không phản hồi.", "error");
                }
            }
        }, 1000);
    });
}

// === Logic điều khiển thủ công cơ cấu đổ rác ===
const btnOpenBin1 = document.getElementById('btn-open-bin1');
const btnOpenBin2 = document.getElementById('btn-open-bin2');
const manualMsg = document.getElementById('manual-msg');

function showManualMsg(text, type) {
    if (!manualMsg) return;
    manualMsg.innerText = text;
    manualMsg.className = `status-message ${type}`;
    manualMsg.classList.remove('hidden');
}

function clearManualMsg() {
    if (!manualMsg) return;
    manualMsg.innerText = "";
    manualMsg.className = "status-message hidden";
}

function sendServoControl(binId) {
    if (!btnOpenBin1 || !btnOpenBin2) return;

    // Đánh dấu đang trong tiến trình điều khiển thủ công
    isManualOperating = true;

    btnOpenBin1.disabled = true;
    btnOpenBin2.disabled = true;

    showManualMsg("Đang gửi yêu cầu mở ngăn...", "info");

    fetch(`/servo/control?bin=${binId}`, { method: 'POST' })
        .then(r => {
            if (!r.ok) {
                throw new Error("Lỗi kết nối máy chủ");
            }
            return r.json();
        })
        .then(data => {
            if (data.success) {
                showManualMsg(`Thành công: Đang mở ngăn ${binId == 1 ? "1 (Rác Pin)" : "2 (Rác Thường)"}...`, "success");
                setTimeout(() => {
                    clearManualMsg();
                    isManualOperating = false;
                    btnOpenBin1.disabled = false;
                    btnOpenBin2.disabled = false;
                }, 5000);
            } else {
                showManualMsg(`Thất bại: ${data.message || "Hệ thống đang bận!"}`, "error");
                setTimeout(() => {
                    clearManualMsg();
                    isManualOperating = false;
                    btnOpenBin1.disabled = false;
                    btnOpenBin2.disabled = false;
                }, 3000);
            }
        })
        .catch(err => {
            console.error("Loi gui lenh servo:", err);
            showManualMsg("Lỗi: Không thể kết nối tới thiết bị nhúng!", "error");
            setTimeout(() => {
                clearManualMsg();
                isManualOperating = false;
                btnOpenBin1.disabled = false;
                btnOpenBin2.disabled = false;
            }, 3000);
        });
}

if (btnOpenBin1) {
    btnOpenBin1.addEventListener('click', () => sendServoControl(1));
}
if (btnOpenBin2) {
    btnOpenBin2.addEventListener('click', () => sendServoControl(2));
}

