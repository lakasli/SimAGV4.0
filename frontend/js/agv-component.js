class SimAGV extends HTMLElement {
    constructor() {
        super();
        this.attachShadow({ mode: 'open' });
        this.isMoving = false;
        this.currentRotation = 0;
        this.trayRotation = 0;
        this.currentThetaRad = null;
        this.currentTrayThetaRad = null;
    }

    connectedCallback() {
        this.render();
        this.setupEventListeners();
    }

    render() {
        this.shadowRoot.innerHTML = `
            <link rel="stylesheet" href="/static/css/agv.css">
            <div class="agv-container">
                <div class="agv-label" id="agvLabel"></div>
                <div class="agv-wrapper" id="agvWrapper">
                    <!-- Body with clip-path -->
                    <div class="agv-body" id="agvBody">
                        <div class="agv-front"></div>
                        
                        <!-- Wheels -->
                        <div class="wheel wheel-front-left">
                            <div class="tread"></div><div class="tread"></div><div class="tread"></div>
                            <div class="tread"></div><div class="tread"></div><div class="tread"></div>
                        </div>
                        <div class="wheel wheel-front-right">
                            <div class="tread"></div><div class="tread"></div><div class="tread"></div>
                            <div class="tread"></div><div class="tread"></div><div class="tread"></div>
                        </div>
                        <div class="wheel wheel-rear-left">
                            <div class="tread"></div><div class="tread"></div><div class="tread"></div>
                            <div class="tread"></div><div class="tread"></div><div class="tread"></div>
                        </div>
                        <div class="wheel wheel-rear-right">
                            <div class="tread"></div><div class="tread"></div><div class="tread"></div>
                            <div class="tread"></div><div class="tread"></div><div class="tread"></div>
                        </div>
                        
                        <!-- Body Details -->
                        <div class="detail-stripes stripe-1"></div>
                        <div class="detail-stripes stripe-2"></div>
                        <div class="detail-stripes stripe-3"></div>
                        <div class="detail-stripes stripe-4"></div>
                    </div>

                    <!-- Tray Structure - Sibling to agv-body -->
                    <div class="tray-container">
                        <div class="tray" id="tray">
                            <div class="shelf" id="shelf" style="display:none"></div>
                            <div class="tray-marker"></div>
                        </div>
                        <div class="bearing"></div>
                    </div>
                </div>
            </div>
        `;
    }

    setupEventListeners() {
        // Internal logic if needed
    }

    // Public API
    startMove() {
        if (!this.isMoving) {
            this.shadowRoot.querySelector('.agv-container').classList.add('agv-moving');
            this.isMoving = true;
            this.dispatchEvent(new CustomEvent('status-change', { detail: '移动中' }));
        }
    }

    stopMove() {
        this.shadowRoot.querySelector('.agv-container').classList.remove('agv-moving');
        this.isMoving = false;
        this.dispatchEvent(new CustomEvent('status-change', { detail: '静止' }));
    }

    reset() {
        this.currentRotation = 0;
        this.trayRotation = 0;
        this.currentThetaRad = null;
        this.currentTrayThetaRad = null;
        this.updateTransforms();
        this.stopMove();
        this.dispatchEvent(new CustomEvent('status-change', { detail: '已重置' }));
    }

    rotateCar(angle) {
        this.currentRotation = this._normalizeDeg(this.currentRotation + angle);
        this.updateTransforms();
    }
    
    setRotation(angle) {
        // 使用最短路径更新旋转角度，避免跨越 0/360 度时出现反向旋转
        const target = Number(angle);
        const current = this.currentRotation;
        
        let diff = this._normalizeDeg(target - current);
        if (diff === -180) diff = 180;
        
        // 累加差值，保持角度连续性
        this.currentRotation = current + diff;
        this.updateTransforms();
    }

    setTheta(thetaRad) {
        const nextThetaRad = Number(thetaRad);
        if (!isFinite(nextThetaRad)) return;
        if (this.currentThetaRad === null || !isFinite(this.currentThetaRad)) {
            this.currentThetaRad = nextThetaRad;
        } else {
            let diff = this._normalizeRad(nextThetaRad - this.currentThetaRad);
            if (diff === -Math.PI) diff = Math.PI;
            this.currentThetaRad = this.currentThetaRad + diff;
        }
        const deg = ((-this.currentThetaRad * 180) / Math.PI) - 180;
        this.currentRotation = deg;
        this.updateTransforms();
    }

    rotateTray(angle) {
        this.trayRotation += angle;
        this.updateTransforms();
    }

    setTrayRotation(angle) {
        const target = Number(angle);
        const current = this.trayRotation;
        if (!isFinite(target) || !isFinite(current)) return;
        let diff = this._normalizeDeg(target - current);
        if (diff === -180) diff = 180;
        this.trayRotation = current + diff;
        this.updateTransforms();
    }

    setTrayTheta(thetaRad) {
        const nextThetaRad = Number(thetaRad);
        if (!isFinite(nextThetaRad)) return;
        if (this.currentTrayThetaRad === null || !isFinite(this.currentTrayThetaRad)) {
            this.currentTrayThetaRad = nextThetaRad;
        } else {
            let diff = this._normalizeRad(nextThetaRad - this.currentTrayThetaRad);
            if (diff === -Math.PI) diff = Math.PI;
            this.currentTrayThetaRad = this.currentTrayThetaRad + diff;
        }
        const deg = (this.currentTrayThetaRad * 180) / Math.PI;
        this.trayRotation = deg;
        this.updateTransforms();
    }

    updateTransforms() {
        const wrapper = this.shadowRoot.getElementById('agvWrapper');
        const tray = this.shadowRoot.getElementById('tray');
        
        if (wrapper) wrapper.style.transform = `rotate(${this.currentRotation}deg)`;
        if (tray) tray.style.transform = `rotate(${this.trayRotation}deg)`;
    }
    
    _normalizeDeg(d) {
        let r = d % 360;
        if (r > 180) r -= 360;
        if (r <= -180) r += 360;
        return r;
    }

    _normalizeRad(a) {
        const twoPi = 2 * Math.PI;
        let r = a % twoPi;
        if (r > Math.PI) r -= twoPi;
        if (r <= -Math.PI) r += twoPi;
        return r;
    }

    setLoaded(loaded) {
        if (loaded) this.classList.add('has-pallet');
        else this.classList.remove('has-pallet');
    }

    animateLoadForkBar() {
        this.setLoaded(true);
    }

    animateUnloadForkBar() {
        this.setLoaded(false);
    }

    // Adaptation for existing control/config
    setConfig(config) {
        if (!config) return;
        
        // Set serial number
        if (config.serial_number) {
            const label = this.shadowRoot.getElementById('agvLabel');
            if (label) {
                label.textContent = config.serial_number;
            }
        }

        // Apply width and length if provided, overriding CSS aspect-ratio
        // Note: The component itself is sized by the parent container (via index.js logic)
        // but we might want to adjust internal proportions if the ratio is very different.
        // For now, let's assume the parent sets the correct size and we just fill it.
        // However, if we need to adjust the wheel positions or other internal elements based on
        // specific physical config, we could do it here.
        
        // Example: If width/length ratio is significantly different from 2:1 (default),
        // we might want to adjust .agv-container aspect-ratio or remove it.
        
        if (config.width && config.length) {
            const w = Number(config.width);
            const l = Number(config.length);
            if (w > 0 && l > 0) {
                // In agv.css, aspect-ratio is 2 / 1 (width / height? No, standard is w/h).
                // Our AGV is usually longer than wide. 
                // Default CSS seems to assume a certain shape.
                // Let's update the container aspect ratio to match physical dims.
                // Note: 'width' in config usually means the narrower dimension (side-to-side),
                // and 'length' is front-to-back.
                // But in CSS aspect-ratio: x/y, x is width, y is height.
                // If our AGV is drawn horizontally (facing right), then CSS width = length, CSS height = width.
                // If drawn vertically (facing up), CSS width = width, CSS height = length.
                
                // Looking at index.js drawRobotIcon: 
                // ctx.rotate(-rotationDeg - Math.PI / 2);
                // And fillRect(-wPx / 2, -lRectPx / 2, wPx, lRectPx);
                // This draws a vertical rectangle (width < length).
                
                // Looking at agv.css:
                // .agv-front { right: 5%; ... }
                // This suggests the front is on the Right. So it's drawn Horizontally by default.
                // So CSS Width corresponds to Physical Length.
                // CSS Height corresponds to Physical Width.
                
                const cssAspectRatio = l / w; // Length (horizontal) / Width (vertical)
                const container = this.shadowRoot.querySelector('.agv-container');
                if (container) {
                    container.style.aspectRatio = `${cssAspectRatio}`;
                }
            }
        }
    }
    setShelfDimensions(widthMeters, lengthMeters, vehicleWidthMeters, vehicleLengthMeters) {
        const shelf = this.shadowRoot.getElementById('shelf');
        if (!shelf) return;
        const wm = Number(widthMeters);
        const lm = Number(lengthMeters);
        const vw = Math.max(1e-6, Number(vehicleWidthMeters));
        const vl = Math.max(1e-6, Number(vehicleLengthMeters));
        const wPct = Math.max(5, (lm / (vl * 0.6)) * 100);
        const hPct = Math.max(5, (wm / (vw * 0.8)) * 100);
        // 若货架超过托盘容器，则允许显示溢出：由 CSS overflow: visible 控制
        // 仅做最小值保护，不再在此处做上限钳制，以便展示真实尺寸
        shelf.style.setProperty('--shelf-width', wPct + '%');
        shelf.style.setProperty('--shelf-height', hPct + '%');
        shelf.style.display = 'block';
    }

    setShelfOpacityScale(scale) {
        const shelf = this.shadowRoot.getElementById('shelf');
        if (!shelf) return;
        const v = Math.max(0, Math.min(1, Number(scale)));
        const base = parseFloat(getComputedStyle(shelf).getPropertyValue('--shelf-opacity')) || 0.3;
        shelf.style.opacity = String(base * v);
        if (v > 0) shelf.style.display = 'block';
    }

    clearShelf() {
        const shelf = this.shadowRoot.getElementById('shelf');
        if (!shelf) return;
        shelf.style.display = 'none';
        shelf.style.removeProperty('--shelf-width');
        shelf.style.removeProperty('--shelf-height');
    }
}

customElements.define('sim-agv', SimAGV);
