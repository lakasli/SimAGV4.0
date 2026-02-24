#include "vda_instant_actions_acceptance_molecule.hpp"

#include <algorithm>

namespace simagv::l3 {
namespace {

bool hasCapability(const std::vector<std::string>& capabilities, const std::string& actionType)
{
    return std::find(capabilities.begin(), capabilities.end(), actionType) != capabilities.end();
}

ActionDefinition toActionDefinition(const VdaAction& vdaAction)
{
    ActionDefinition action{}; // 动作定义
    action.actionType = vdaAction.actionType;
    action.timeoutMs = 0U;
    action.blocking = (vdaAction.blockingType == "Hard");
    for (const auto& param : vdaAction.parameters) {
        action.params[param.key] = param.value;
    }
    return action;
}

} // namespace

VdaInstantActionsResult acceptVdaInstantActions(const VdaInstantActions& instantActions, const VehicleContext& vehicleContext, const InstantActionsConfig& acceptConfig)
{
    VdaInstantActionsResult result{}; // 返回结果
    result.accepted = true;

    if (instantActions.actions.size() > acceptConfig.maxActionCount) {
        result.accepted = false;
        VdaRejectedAction rejected{}; // 拒绝项
        rejected.actionId = "";
        rejected.rejectCode = "TOO_MANY_ACTIONS";
        rejected.rejectMessage = "action count exceeds limit";
        result.rejected.push_back(rejected);
        return result;
    }

    for (const auto& action : instantActions.actions) {
        if (acceptConfig.enableCapabilityCheck && !hasCapability(vehicleContext.capabilities, action.actionType)) {
            VdaRejectedAction rejected{}; // 拒绝项
            rejected.actionId = action.actionId;
            rejected.rejectCode = "CAPABILITY_MISMATCH";
            rejected.rejectMessage = "unsupported action: " + action.actionType;
            result.rejected.push_back(rejected);
            result.accepted = false;
            continue;
        }
        result.commands.push_back(toActionDefinition(action));
    }

    return result;
}

} // namespace simagv::l3

