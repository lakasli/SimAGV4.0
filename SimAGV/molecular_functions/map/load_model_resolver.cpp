#include "load_model_resolver.hpp"

#include <algorithm>
#include <stdexcept>

namespace simagv::l3 {
namespace {

float parseFloatOrDefault(const std::map<std::string, std::string>& params, const std::string& key, float defaultValue)
{
    const auto it = params.find(key); // 参数迭代器
    if (it == params.end()) {
        return defaultValue;
    }
    return std::stof(it->second);
}

} // namespace

LoadResolution resolveLoadModel(const LoadRecognition& recognition, const VehicleContext& vehicle, const ActionContext& action)
{
    (void)vehicle;
    if (recognition.modelFilePath.empty()) {
        throw std::invalid_argument("modelFilePath is empty");
    }

    const auto loadModel = simagv::l4::parseLoadModelFile(recognition.modelFilePath); // 载荷模型

    LoadResolution resolution{}; // 解析结果
    resolution.loadId = !recognition.loadId.empty() ? recognition.loadId : loadModel.loadId;
    resolution.dimensions = loadModel.dimensions;
    resolution.weightKg = loadModel.weightKg;
    resolution.footprint = loadModel.footprint;

    const float explicitForkHeightM = parseFloatOrDefault(action.params, "targetForkHeightM", -1.0F); // 显式叉高
    if (explicitForkHeightM > 0.0F) {
        resolution.targetForkHeightM = explicitForkHeightM;
        return resolution;
    }

    const float minForkHeightM = parseFloatOrDefault(action.params, "minForkHeightM", 0.0F); // 最小叉高
    const float suggestedForkHeightM = 0.5F * loadModel.dimensions.height; // 建议叉高
    resolution.targetForkHeightM = std::max(minForkHeightM, suggestedForkHeightM);
    (void)action.actionType;
    return resolution;
}

} // namespace simagv::l3
