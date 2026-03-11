import { ProductionLine } from './production-line.js';

export class ProductionLineLoader {
    constructor() {
        this.productionLines = new Map();
        this.scene = null;
        this.camera = null;
        this.config = null;
        this.currentMapId = null;
    }

    async init(scene, camera) {
        this.scene = scene;
        this.camera = camera;
        await this.loadAllProductionLines();
        if (this.currentMapId) {
            this.updateAllVisibility();
        }
    }

    async loadAllProductionLines() {
        try {
            const resp = await fetch('/api/equipments');
            const equipments = await resp.json();

            const productionLines = equipments.filter(eq => eq.type === 'production_line');

            for (const eq of productionLines) {
                await this.loadProductionLine(eq.dir_name);
            }
        } catch (err) {
            console.error('加载产线列表失败:', err);
        }
    }

    async loadProductionLine(dirName) {
        try {
            const resp = await fetch(`/api/equipments/${dirName}/config`);
            if (!resp.ok) {
                throw new Error(`HTTP ${resp.status}`);
            }
            const config = await resp.json();

            const sites = config.site;
            const mapId = config.map_id;
            if (!sites || !Array.isArray(sites) || sites.length === 0) {
                console.error(`[${dirName}] site 校验失败，跳过加载`);
                return false;
            }

            for (const site of sites) {
                const pl = new ProductionLine(null, this.scene, this.camera);
                pl.init();
                pl.setPosition(site.x, site.y, site.theta);

                const key = `${dirName}_${mapId}_${site.floor_name}`;
                this.productionLines.set(key, {
                    instance: pl,
                    config: config,
                    mapId: mapId,
                    floorName: site.floor_name,
                    position: { x: site.x, y: site.y, theta: site.theta }
                });

                const isVisible = this.isMapMatch(mapId, site.floor_name);
                this.setProductionLineVisibility(pl, isVisible);
            }

            return true;
        } catch (err) {
            console.error(`加载产线 ${dirName} 失败:`, err);
            return false;
        }
    }

    isMapMatch(mapId, floorName) {
        if (!mapId || !this.currentMapId) {
            return false;
        }
        
        const siteId = String(mapId).trim().toLowerCase();
        const currentId = String(this.currentMapId).trim().toLowerCase();
        
        return siteId === currentId;
    }

    setProductionLineVisibility(pl, visible) {
        if (pl && pl.conveyorGroup) {
            pl.conveyorGroup.visible = visible;
        }
    }

    updateAllVisibility() {
        this.productionLines.forEach((value, key) => {
            const isVisible = this.isMapMatch(value.mapId, value.floorName);
            this.setProductionLineVisibility(value.instance, isVisible);
        });
    }

    setCurrentMap(mapId) {
        this.currentMapId = mapId;
        this.updateAllVisibility();
    }

    getProductionLinesByMapId(mapId) {
        const results = [];
        this.productionLines.forEach((value, key) => {
            if (value.mapId === mapId) {
                results.push(value.instance);
            }
        });
        return results;
    }

    animate() {
        if (!this.currentMapId) return;
        this.productionLines.forEach(({ instance, mapId }) => {
            const isVisible = this.isMapMatch(mapId);
            if (isVisible) {
                instance.animate();
            }
        });
    }

    onMouseClick(event, domElement) {
        if (!this.currentMapId) return { clicked: false };
        for (const { instance, mapId } of this.productionLines.values()) {
            const isVisible = this.isMapMatch(mapId);
            if (isVisible) {
                const result = instance.onMouseClick(event, domElement);
                if (result.clicked) {
                    return result;
                }
            }
        }
        return { clicked: false };
    }

    onMouseMove(event, domElement) {
        if (!this.currentMapId) return;
        for (const { instance, mapId } of this.productionLines.values()) {
            const isVisible = this.isMapMatch(mapId);
            if (isVisible) {
                instance.onMouseMove(event, domElement);
            }
        }
    }

    dispose() {
        this.productionLines.forEach(({ instance }) => {
            instance.dispose();
        });
        this.productionLines.clear();
    }
}

export default ProductionLineLoader;
