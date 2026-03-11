import * as THREE from 'three';

const SCALE_FACTOR = 20;

export class ProductionLine {
    constructor(container, scene, camera) {
        this.container = container;
        this.scene = scene;
        this.camera = camera;
        this.conveyorGroup = null;
        this.beltTexture = null;
        this.isRunning = true;
        this.beltSpeed = 1.0;
        this.originalMaterials = new Map();
        this.highlightedMesh = null;
        this.isBubbleVisible = false;
        this.wasModelClicked = false;

        this.productionLineData = {
            name: '产线一',
            productionStatus: '停产',
            productStatus: '无产品',
            faultStatus: '错误',
            output: 0,
            efficiency: 0,
            runtime: 0,
            availability: 100
        };

        this.raycaster = new THREE.Raycaster();
        this.mouse = new THREE.Vector2();
    }

    init() {
        this.createConveyor();
        this.setupEventListeners();
    }

    createConveyor() {
        this.conveyorGroup = new THREE.Group();

        const frameMaterial = new THREE.MeshStandardMaterial({
            color: 0x4a90e2,
            metalness: 0.6,
            roughness: 0.3
        });

        const sideMaterial = new THREE.MeshStandardMaterial({
            color: 0x2c3e50,
            metalness: 0.5,
            roughness: 0.4
        });

        const baseGeometry = new THREE.BoxGeometry(2, 0.08, 1);
        const base = new THREE.Mesh(baseGeometry, sideMaterial);
        base.position.set(0, 0.04, 0);
        base.castShadow = true;
        base.receiveShadow = true;
        this.conveyorGroup.add(base);

        const sidePanelGeometry = new THREE.BoxGeometry(2, 0.45, 0.04);

        const sidePanel1 = new THREE.Mesh(sidePanelGeometry, sideMaterial);
        sidePanel1.position.set(0, 0.275, 0.48);
        sidePanel1.castShadow = true;
        sidePanel1.receiveShadow = true;
        this.conveyorGroup.add(sidePanel1);

        const sidePanel2 = new THREE.Mesh(sidePanelGeometry, sideMaterial);
        sidePanel2.position.set(0, 0.275, -0.48);
        sidePanel2.castShadow = true;
        sidePanel2.receiveShadow = true;
        this.conveyorGroup.add(sidePanel2);

        const endPanelGeometry = new THREE.BoxGeometry(0.04, 0.45, 1);

        const endPanel1 = new THREE.Mesh(endPanelGeometry, sideMaterial);
        endPanel1.position.set(0.98, 0.275, 0);
        endPanel1.castShadow = true;
        endPanel1.receiveShadow = true;
        this.conveyorGroup.add(endPanel1);

        const endPanel2 = new THREE.Mesh(endPanelGeometry, sideMaterial);
        endPanel2.position.set(-0.98, 0.275, 0);
        endPanel2.castShadow = true;
        endPanel2.receiveShadow = true;
        this.conveyorGroup.add(endPanel2);

        const topPlateGeometry = new THREE.BoxGeometry(1.96, 0.04, 0.96);
        const topPlate = new THREE.Mesh(topPlateGeometry, frameMaterial);
        topPlate.position.set(0, 0.5, 0);
        topPlate.castShadow = true;
        topPlate.receiveShadow = true;
        this.conveyorGroup.add(topPlate);

        const beltCanvas = document.createElement('canvas');
        beltCanvas.width = 512;
        beltCanvas.height = 256;
        const ctx = beltCanvas.getContext('2d');

        ctx.fillStyle = '#2c3e50';
        ctx.fillRect(0, 0, 512, 256);

        ctx.strokeStyle = '#34495e';
        ctx.lineWidth = 2;
        for (let i = 0; i < 512; i += 32) {
            ctx.beginPath();
            ctx.moveTo(i, 0);
            ctx.lineTo(i, 256);
            ctx.stroke();
        }

        ctx.strokeStyle = '#4a90e2';
        ctx.lineWidth = 4;
        ctx.beginPath();
        ctx.moveTo(0, 128);
        ctx.lineTo(512, 128);
        ctx.stroke();

        this.beltTexture = new THREE.CanvasTexture(beltCanvas);
        this.beltTexture.wrapS = THREE.RepeatWrapping;
        this.beltTexture.wrapT = THREE.RepeatWrapping;
        this.beltTexture.repeat.set(2, 1);

        const beltMaterial = new THREE.MeshStandardMaterial({
            map: this.beltTexture,
            metalness: 0.3,
            roughness: 0.7
        });

        const beltGeometry = new THREE.BoxGeometry(1.9, 0.02, 0.9);
        const conveyorBelt = new THREE.Mesh(beltGeometry, beltMaterial);
        conveyorBelt.position.set(0, 0.53, 0);
        conveyorBelt.receiveShadow = true;
        this.conveyorGroup.add(conveyorBelt);

        const canopyMaterial = new THREE.MeshStandardMaterial({
            color: 0x4a90e2,
            metalness: 0.6,
            roughness: 0.3,
            side: THREE.DoubleSide
        });

        const canopyWidth = 1.1;
        const canopyLength = 1.5;
        const canopyHeight = 1;
        const canopyThickness = 0.04;
        const offsetX = -0.4;

        const canopyCenterX = offsetX + canopyLength / 2;

        const outerWidth = canopyWidth;
        const outerLength = canopyLength;
        const outerHeight = canopyHeight;
        const innerWidth = canopyWidth - canopyThickness * 2;
        const innerLength = canopyLength - canopyThickness * 2;
        const innerHeight = canopyHeight - canopyThickness;

        const frontWall = new THREE.BoxGeometry(outerLength, outerHeight, canopyThickness);
        const frontWallMesh = new THREE.Mesh(frontWall, canopyMaterial);
        frontWallMesh.position.set(canopyCenterX, outerHeight / 2, outerWidth / 2 - canopyThickness / 2);
        frontWallMesh.castShadow = true;
        frontWallMesh.receiveShadow = true;
        this.conveyorGroup.add(frontWallMesh);

        const backWall = new THREE.BoxGeometry(outerLength, outerHeight, canopyThickness);
        const backWallMesh = new THREE.Mesh(backWall, canopyMaterial);
        backWallMesh.position.set(canopyCenterX, outerHeight / 2, -outerWidth / 2 + canopyThickness / 2);
        backWallMesh.castShadow = true;
        backWallMesh.receiveShadow = true;
        this.conveyorGroup.add(backWallMesh);

        const rightWall = new THREE.BoxGeometry(canopyThickness, outerHeight, outerWidth);
        const rightWallMesh = new THREE.Mesh(rightWall, canopyMaterial);
        rightWallMesh.position.set(canopyCenterX + outerLength / 2 - canopyThickness / 2, outerHeight / 2, 0);
        rightWallMesh.castShadow = true;
        rightWallMesh.receiveShadow = true;
        this.conveyorGroup.add(rightWallMesh);

        const leftWallBottom = new THREE.BoxGeometry(canopyThickness, canopyThickness, outerWidth);
        const leftWallBottomMesh = new THREE.Mesh(leftWallBottom, canopyMaterial);
        leftWallBottomMesh.position.set(canopyCenterX - outerLength / 2 + canopyThickness / 2, canopyThickness / 2, 0);
        leftWallBottomMesh.castShadow = true;
        leftWallBottomMesh.receiveShadow = true;
        this.conveyorGroup.add(leftWallBottomMesh);

        const leftWallFront = new THREE.BoxGeometry(canopyThickness, innerHeight, canopyThickness);
        const leftWallFrontMesh = new THREE.Mesh(leftWallFront, canopyMaterial);
        leftWallFrontMesh.position.set(canopyCenterX - outerLength / 2 + canopyThickness / 2, canopyThickness + innerHeight / 2, outerWidth / 2 - canopyThickness / 2);
        leftWallFrontMesh.castShadow = true;
        leftWallFrontMesh.receiveShadow = true;
        this.conveyorGroup.add(leftWallFrontMesh);

        const leftWallBack = new THREE.BoxGeometry(canopyThickness, innerHeight, canopyThickness);
        const leftWallBackMesh = new THREE.Mesh(leftWallBack, canopyMaterial);
        leftWallBackMesh.position.set(canopyCenterX - outerLength / 2 + canopyThickness / 2, canopyThickness + innerHeight / 2, -outerWidth / 2 + canopyThickness / 2);
        leftWallBackMesh.castShadow = true;
        leftWallBackMesh.receiveShadow = true;
        this.conveyorGroup.add(leftWallBackMesh);

        const topWall = new THREE.BoxGeometry(outerLength, canopyThickness, innerWidth);
        const topWallMesh = new THREE.Mesh(topWall, canopyMaterial);
        topWallMesh.position.set(canopyCenterX, outerHeight - canopyThickness / 2, 0);
        topWallMesh.castShadow = true;
        topWallMesh.receiveShadow = true;
        this.conveyorGroup.add(topWallMesh);

        const bottomWall = new THREE.BoxGeometry(innerLength, canopyThickness, innerWidth);
        const bottomWallMesh = new THREE.Mesh(bottomWall, canopyMaterial);
        bottomWallMesh.position.set(canopyCenterX, canopyThickness / 2, 0);
        bottomWallMesh.castShadow = true;
        bottomWallMesh.receiveShadow = true;
        this.conveyorGroup.add(bottomWallMesh);

        const sideTopFront = new THREE.BoxGeometry(canopyThickness, canopyThickness, canopyThickness);
        const sideTopFrontMesh = new THREE.Mesh(sideTopFront, canopyMaterial);
        sideTopFrontMesh.position.set(canopyCenterX + outerLength / 2 - canopyThickness / 2, outerHeight - canopyThickness / 2, outerWidth / 2 - canopyThickness / 2);
        sideTopFrontMesh.castShadow = true;
        sideTopFrontMesh.receiveShadow = true;
        this.conveyorGroup.add(sideTopFrontMesh);

        const sideTopBack = new THREE.BoxGeometry(canopyThickness, canopyThickness, canopyThickness);
        const sideTopBackMesh = new THREE.Mesh(sideTopBack, canopyMaterial);
        sideTopBackMesh.position.set(canopyCenterX + outerLength / 2 - canopyThickness / 2, outerHeight - canopyThickness / 2, -outerWidth / 2 + canopyThickness / 2);
        sideTopBackMesh.castShadow = true;
        sideTopBackMesh.receiveShadow = true;
        this.conveyorGroup.add(sideTopBackMesh);

        const cornerPositions = [
            [canopyCenterX + outerLength / 2 - canopyThickness / 2, outerHeight - canopyThickness / 2, outerWidth / 2 - canopyThickness / 2],
            [canopyCenterX + outerLength / 2 - canopyThickness / 2, outerHeight - canopyThickness / 2, -outerWidth / 2 + canopyThickness / 2],
            [canopyCenterX - outerLength / 2 + canopyThickness / 2, outerHeight - canopyThickness / 2, outerWidth / 2 - canopyThickness / 2],
            [canopyCenterX - outerLength / 2 + canopyThickness / 2, outerHeight - canopyThickness / 2, -outerWidth / 2 + canopyThickness / 2]
        ];

        const cornerGeometry = new THREE.BoxGeometry(canopyThickness, canopyThickness, canopyThickness);
        cornerPositions.forEach(pos => {
            const corner = new THREE.Mesh(cornerGeometry, canopyMaterial);
            corner.position.set(pos[0], pos[1], pos[2]);
            corner.castShadow = true;
            corner.receiveShadow = true;
            this.conveyorGroup.add(corner);
        });

        const statusZ = outerWidth / 2 + 0.01;
        const statusZNeg = -outerWidth / 2 - 0.01;

        const productionStatus = this.createStatusLabel('停产');
        productionStatus.position.set(canopyCenterX, outerHeight - 0.15, statusZ);
        productionStatus.rotation.y = 0;
        this.conveyorGroup.add(productionStatus);

        const productStatus = this.createStatusLabel('无产品');
        productStatus.position.set(canopyCenterX, outerHeight - 0.4, statusZ);
        productStatus.rotation.y = 0;
        this.conveyorGroup.add(productStatus);

        const faultStatus = this.createStatusLabel('故障');
        faultStatus.position.set(canopyCenterX, outerHeight - 0.65, statusZ);
        faultStatus.rotation.y = 0;
        this.conveyorGroup.add(faultStatus);

        const productionStatusNeg = this.createStatusLabel('停产');
        productionStatusNeg.position.set(canopyCenterX, outerHeight - 0.15, statusZNeg);
        productionStatusNeg.rotation.y = Math.PI;
        this.conveyorGroup.add(productionStatusNeg);

        const productStatusNeg = this.createStatusLabel('无产品');
        productStatusNeg.position.set(canopyCenterX, outerHeight - 0.4, statusZNeg);
        productStatusNeg.rotation.y = Math.PI;
        this.conveyorGroup.add(productStatusNeg);

        const faultStatusNeg = this.createStatusLabel('故障');
        faultStatusNeg.position.set(canopyCenterX, outerHeight - 0.65, statusZNeg);
        faultStatusNeg.rotation.y = Math.PI;
        this.conveyorGroup.add(faultStatusNeg);

        const labelSprite = this.createTextSprite('产线一', { x: canopyCenterX, y: canopyHeight + 0.3, z: 0 }, '#4a90e2', 72);
        this.conveyorGroup.add(labelSprite);

        this.scene.add(this.conveyorGroup);
    }

    createTextSprite(text, position, color = '#ffffff', fontSize = 80) {
        const canvas = document.createElement('canvas');
        const context = canvas.getContext('2d');
        canvas.width = 512;
        canvas.height = 128;

        context.fillStyle = color;
        context.font = `Bold ${fontSize}px Arial`;
        context.textAlign = 'center';
        context.textBaseline = 'middle';
        context.fillText(text, 256, 64);

        const texture = new THREE.CanvasTexture(canvas);
        const spriteMaterial = new THREE.SpriteMaterial({
            map: texture,
            transparent: true
        });
        const sprite = new THREE.Sprite(spriteMaterial);
        sprite.position.set(position.x, position.y, position.z);
        sprite.scale.set(1.5, 0.375, 1);

        return sprite;
    }

    createStatusLabel(text, bgColor = '#ff0000', textColor = '#ffffff') {
        const canvas = document.createElement('canvas');
        const context = canvas.getContext('2d');
        canvas.width = 512;
        canvas.height = 128;

        context.fillStyle = bgColor;
        context.fillRect(0, 0, 512, 128);

        context.strokeStyle = '#ffffff';
        context.lineWidth = 4;
        context.strokeRect(2, 2, 508, 124);

        context.fillStyle = textColor;
        context.font = 'Bold 64px Arial';
        context.textAlign = 'center';
        context.textBaseline = 'middle';
        context.fillText(text, 256, 64);

        const texture = new THREE.CanvasTexture(canvas);
        const material = new THREE.MeshBasicMaterial({
            map: texture,
            transparent: true,
            side: THREE.DoubleSide
        });
        const geometry = new THREE.PlaneGeometry(0.8, 0.2);
        const mesh = new THREE.Mesh(geometry, material);

        return mesh;
    }

    setPosition(x, y, theta) {
        if (!this.conveyorGroup) return;

        const worldX = x / SCALE_FACTOR;
        const worldZ = y / SCALE_FACTOR;

        this.conveyorGroup.position.set(worldX, 0, worldZ);
        this.conveyorGroup.rotation.y = -theta;
    }

    updateData(data) {
        Object.assign(this.productionLineData, data);
    }

    animate() {
        if (this.isRunning && this.beltTexture) {
            this.beltTexture.offset.x -= 0.01 * this.beltSpeed;
            if (this.beltTexture.offset.x < -1) {
                this.beltTexture.offset.x = 0;
            }
        }
    }

    onMouseClick(event, domElement) {
        const rect = domElement.getBoundingClientRect();
        this.mouse.x = ((event.clientX - rect.left) / rect.width) * 2 - 1;
        this.mouse.y = -((event.clientY - rect.top) / rect.height) * 2 + 1;

        this.raycaster.setFromCamera(this.mouse, this.camera);

        const intersects = this.raycaster.intersectObject(this.conveyorGroup, true);

        if (intersects.length > 0) {
            if (this.highlightedMesh && this.highlightedMesh !== this.conveyorGroup) {
                this.restoreAllMaterials();
            }

            this.highlightConveyorGroup();
            this.highlightedMesh = this.conveyorGroup;
            this.wasModelClicked = true;

            return { clicked: true, position: { x: event.clientX, y: event.clientY } };
        } else {
            this.wasModelClicked = false;
        }

        return { clicked: false };
    }

    onMouseMove(event, domElement) {
        const rect = domElement.getBoundingClientRect();
        this.mouse.x = ((event.clientX - rect.left) / rect.width) * 2 - 1;
        this.mouse.y = -((event.clientY - rect.top) / rect.height) * 2 + 1;

        this.raycaster.setFromCamera(this.mouse, this.camera);

        const intersects = this.raycaster.intersectObject(this.conveyorGroup, true);

        if (intersects.length > 0) {
            domElement.style.cursor = 'pointer';
        } else {
            domElement.style.cursor = 'default';
        }
    }

    highlightConveyorGroup() {
        this.conveyorGroup.traverse((child) => {
            if (child.isMesh) {
                if (!this.originalMaterials.has(child.uuid)) {
                    this.originalMaterials.set(child.uuid, child.material);
                }
                const highlightMaterial = child.material.clone();
                highlightMaterial.emissive = new THREE.Color(0x4a90e2);
                highlightMaterial.emissiveIntensity = 0.3;
                child.material = highlightMaterial;
            }
        });
    }

    restoreAllMaterials() {
        this.conveyorGroup.traverse((child) => {
            if (child.isMesh) {
                const originalMaterial = this.originalMaterials.get(child.uuid);
                if (originalMaterial) {
                    child.material = originalMaterial;
                }
            }
        });
        this.originalMaterials.clear();
    }

    dispose() {
        if (this.conveyorGroup) {
            this.scene.remove(this.conveyorGroup);
            this.conveyorGroup.traverse((child) => {
                if (child.geometry) child.geometry.dispose();
                if (child.material) {
                    if (Array.isArray(child.material)) {
                        child.material.forEach(m => m.dispose());
                    } else {
                        child.material.dispose();
                    }
                }
            });
        }
        if (this.beltTexture) {
            this.beltTexture.dispose();
        }
    }

    setupEventListeners() {
    }
}

export default ProductionLine;
