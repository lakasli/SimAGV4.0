import { ProductionLineLoader } from '../SimEquipment/ProductionLine/index.js';

let productionLineLoader = null;
let currentMapId = null;

export async function initProductionLines() {
    console.log('[production-lines.initProductionLines] 开始初始化产线...');
    if (!window.SimViewer3D) {
        console.warn('SimViewer3D 未初始化');
        return;
    }

    const scene = window.SimViewer3D.getScene();
    const camera = window.SimViewer3D.getCamera();

    if (!scene || !camera) {
        console.warn('场景或相机未就绪');
        return;
    }

    console.log('[production-lines.initProductionLines] 创建 ProductionLineLoader 实例');
    productionLineLoader = new ProductionLineLoader();
    await productionLineLoader.init(scene, camera);

    console.log('产线加载完成');
}

export function loadProductionLinesForMap(mapId) {
    if (!productionLineLoader) {
        console.warn('产线加载器未初始化');
        return;
    }

    currentMapId = mapId;
    console.log(`加载地图 ${mapId} 的产线`);
}

export function animateProductionLines() {
    if (productionLineLoader) {
        productionLineLoader.animate();
    }
}

export function onProductionLineClick(event, domElement) {
    if (productionLineLoader) {
        return productionLineLoader.onMouseClick(event, domElement);
    }
    return { clicked: false };
}

export function onProductionLineMouseMove(event, domElement) {
    if (productionLineLoader) {
        productionLineLoader.onMouseMove(event, domElement);
    }
}

export default {
    initProductionLines,
    loadProductionLinesForMap,
    animateProductionLines,
    onProductionLineClick,
    onProductionLineMouseMove
};
