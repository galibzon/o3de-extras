/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzFramework/Asset/AssetSystemBus.h>

#include <Atom/RPI.Reflect/Asset/AssetUtils.h>

#include "OpenXRActionsBindingAsset.h"
#include <OpenXRVk/OpenXRInteractionProfileBus.h>
#include <OpenXRVk/OpenXRVkUtils.h>
#include <OpenXRVk/OpenXRVisualizedSpacesInterface.h>
#include "OpenXRActionsManager.h"

namespace OpenXRVk
{
    bool ActionsManager::Init(XrInstance xrInstance, XrSession xrSession)
    {
        m_xrInstance = xrInstance;
        m_xrSession = xrSession;

        auto outcome = SetBaseVisualizedSpaceForPoseActions(IOpenXRVisualizedSpaces::ReferenceSpaceViewName);
        if (!outcome.IsSuccess())
        {
            const auto outcomeMsg = outcome.TakeError();
            auto errorMsg = AZStd::string::format("Failed to set [%s] as the default base visualized space. Reason:\n%s.",
                IOpenXRVisualizedSpaces::ReferenceSpaceViewName, outcomeMsg.c_str());
            AZ_Assert(false, "%s", errorMsg.c_str());
            AZ_Error(LogName, false, "%s", errorMsg.c_str());
            return false;
        }

        // OpenXR only allows to define ActionSets during session creation.
        // From the point of view of O3DE, the developer defines action sets
        // in an asset of type OpenXRActionBindingsAsset.
        // The default source path for said asset is "@project@/openxr.xractions".
        const auto actionsBindingAsset = AZ::RPI::AssetUtils::LoadCriticalAsset<OpenXRActionBindingsAsset>({ DefaultActionsAssetPath });
        if (!actionsBindingAsset.IsReady())
        {
            AZ_Printf(LogName, "This application won't support user interactions. Default action bindings asset [%s] not found.\n", DefaultActionsAssetPath);
            return true;
        }

        AZStd::unordered_set<XrPath> activeProfiles;
        AZStd::vector<XrActionSuggestedBinding> activeBindings;
        for (const auto& actionSet : actionsBindingAsset->m_actionSets)
        {
            if (!InitActionSetInternal(actionSet, activeProfiles, activeBindings))
            {
                return false;
            }
        }

        if (activeBindings.empty() || activeProfiles.empty())
        {
            AZ_Printf(LogName, "This application will run without actions.\n");
            return true;
        }

        // Register the bindings for each active interaction profile.
        for (const auto& profilePath : activeProfiles)
        {
            XrInteractionProfileSuggestedBinding suggestedBindings{ XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
            suggestedBindings.interactionProfile = profilePath;
            suggestedBindings.suggestedBindings = activeBindings.data();
            suggestedBindings.countSuggestedBindings = static_cast<uint32_t>(activeBindings.size());
            XrResult result = xrSuggestInteractionProfileBindings(m_xrInstance, &suggestedBindings);
            WARN_IF_UNSUCCESSFUL(result);
        }

        AZStd::vector<XrActionSet> xrActionSets;
        xrActionSets.reserve(m_actionSets.size());
        size_t actionSetIdx = 0;
        for (const auto& actionSetInfo : m_actionSets)
        {
            xrActionSets.push_back(actionSetInfo.m_xrActionSet);
            m_activeActionSets.set(actionSetIdx, true); // By default all actionSets will be active.
            actionSetIdx++;
        }
        RecreateXrActiveActionSets();

        XrSessionActionSetsAttachInfo attachInfo{ XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
        attachInfo.countActionSets = static_cast<uint32_t>(xrActionSets.size());
        attachInfo.actionSets = xrActionSets.data();
        XrResult result = xrAttachSessionActionSets(m_xrSession, &attachInfo);
        if (IsError(result))
        {
            m_xrActiveActionSets.clear();
            PrintXrError(LogName, result, "Failed to attach %zu action sets to the session.", xrActionSets.size());
            return false;
        }

        return true;
    }

    bool ActionsManager::SyncActions(XrTime predictedDisplayTime)
    {
        if (m_xrActiveActionSets.empty())
        {
            // Nothing to do
            return true;
        }

        m_predictedDisplaytime = predictedDisplayTime;

        XrActionsSyncInfo syncInfo{ XR_TYPE_ACTIONS_SYNC_INFO };
        syncInfo.countActiveActionSets = aznumeric_cast<uint32_t>(m_xrActiveActionSets.size());
        syncInfo.activeActionSets = m_xrActiveActionSets.data();
        XrResult result = xrSyncActions(m_xrSession, &syncInfo);
        if (IsError(result))
        {
            PrintXrError(LogName, result, "Failed to sync %zu actionSets.\n", m_xrActiveActionSets.size());
            return false;
        }

        return true;
    }



    bool ActionsManager::InitActionSetInternal(const OpenXRActionSet& actionSet,
        AZStd::unordered_set<XrPath>& activeProfiles,
        AZStd::vector<XrActionSuggestedBinding>& activeBindings)
    {
        // Create an action set.
        XrActionSetCreateInfo actionSetCreateInfo{};
        actionSetCreateInfo.type = XR_TYPE_ACTION_SET_CREATE_INFO;
        azstrcpy(actionSetCreateInfo.actionSetName, sizeof(actionSetCreateInfo.actionSetName), actionSet.m_name.c_str());
        const char* localizedNameCStr = actionSet.m_name.c_str();
        if (!actionSet.m_localizedName.empty())
        {
            localizedNameCStr = actionSet.m_localizedName.c_str();
        }
        azstrcpy(actionSetCreateInfo.localizedActionSetName, sizeof(actionSetCreateInfo.localizedActionSetName), localizedNameCStr);
        actionSetCreateInfo.priority = actionSet.m_priority;

        {
            ActionSetInfo newActionSetInfo;
            newActionSetInfo.m_name = actionSet.m_name;
            XrResult result = xrCreateActionSet(m_xrInstance, &actionSetCreateInfo, &newActionSetInfo.m_xrActionSet);
            if (IsError(result))
            {
                PrintXrError(LogName, result, "Failed to instantiate actionSet named [%s].", actionSet.m_name.c_str());
                return false;
            }
            m_actionSets.emplace_back(AZStd::move(newActionSetInfo));
        }

        ActionSetInfo& newActionSetInfo = m_actionSets.back();
        for (const auto& action : actionSet.m_actions)
        {
            if (!InitActionBindingsInternal(newActionSetInfo, action, activeProfiles, activeBindings))
            {
                AZ_Error(LogName, false, "Failed to created action named [%s] under actionSet named [%s].",
                    action.m_name.c_str(), actionSet.m_name.c_str());
                return false;
            }
        }
        
        return true;
    }

    static XrActionType GetActionTypeFromOpenXRActionPath(const OpenXRActionPath& actionPath)
    {
        auto interactionProviderIface = OpenXRInteractionProfileBus::FindFirstHandler(actionPath.m_interactionProfile);
        if (!interactionProviderIface)
        {
            AZ_Error(ActionsManager::LogName, false, "Couldn't find interaction data provider with id [%s].", actionPath.m_interactionProfile.c_str());
            return XR_ACTION_TYPE_MAX_ENUM;
        }
        const auto actionPathInfo = interactionProviderIface->GetActionPathInfo(actionPath.m_userPath, actionPath.m_componentPath);
        return actionPathInfo.m_actionType;
    }

    XrAction ActionsManager::CreateXrActionAndXrSpace(const ActionSetInfo& actionSetInfo,
        const OpenXRAction& action, const XrActionType actionType, XrSpace& newXrActionSpace) const
    {
        XrActionCreateInfo actionCreateInfo{ XR_TYPE_ACTION_CREATE_INFO };
        actionCreateInfo.actionType = actionType;
        azstrcpy(actionCreateInfo.actionName, sizeof(actionCreateInfo.actionName), action.m_name.c_str());
        const char* localizedNameCStr = action.m_name.c_str();
        if (!action.m_localizedName.empty())
        {
            localizedNameCStr = action.m_localizedName.c_str();
        }
        azstrcpy(actionCreateInfo.localizedActionName, sizeof(actionCreateInfo.localizedActionName), localizedNameCStr);
        actionCreateInfo.countSubactionPaths = 0; // Subactions are not supported.
        actionCreateInfo.subactionPaths = nullptr; // Subactions are not supported.

        XrAction newXrAction;
        XrResult result = xrCreateAction(actionSetInfo.m_xrActionSet, &actionCreateInfo, &newXrAction);
        if (IsError(result))
        {
            PrintXrError(ActionsManager::LogName, result, "Failed to create action named %s.\n", action.m_name.c_str());
            return XR_NULL_HANDLE;
        }

        // The space will be relevant if the action type is POSE. 
        newXrActionSpace = XR_NULL_HANDLE;
        if (actionType == XR_ACTION_TYPE_POSE_INPUT)
        {
            XrActionSpaceCreateInfo actionSpaceInfo{ XR_TYPE_ACTION_SPACE_CREATE_INFO };
            actionSpaceInfo.action = newXrAction;
            actionSpaceInfo.poseInActionSpace.orientation.w = 1.f; // Make it an identity quaterion.
            result = xrCreateActionSpace(m_xrSession, &actionSpaceInfo, &newXrActionSpace);
            if (IsError(result))
            {
                xrDestroyAction(newXrAction);
                PrintXrError(ActionsManager::LogName, result, "Failed to create XrSpace for action named %s.\n", action.m_name.c_str());
                return XR_NULL_HANDLE;
            }
        }

        return newXrAction;
    }

    uint32_t ActionsManager::AppendActionBindings(const OpenXRAction& action,
        XrAction newXrAction,
        AZStd::unordered_set<XrPath>& activeProfiles,
        AZStd::vector<XrActionSuggestedBinding>& activeBindings) const
    {
        uint32_t additionalBindingsCount = 0;
        for (const auto& actionPath : action.m_actionPaths)
        {
            auto interactionProviderIface = OpenXRInteractionProfileBus::FindFirstHandler(actionPath.m_interactionProfile);
            if (!interactionProviderIface)
            {
                AZ_Error(LogName, false, "Couldn't find interaction data provider with id [%s].", actionPath.m_interactionProfile.c_str())
                    return false;
            }

            const auto pathInfo = interactionProviderIface->GetActionPathInfo(actionPath.m_userPath, actionPath.m_componentPath);
            if (pathInfo.m_absolutePath.empty())
            {
                AZ_Warning(LogName, false, "Failed to retrieve action path info for profile [%s], user path [%s], component path [%s].\n",
                    actionPath.m_interactionProfile.c_str(), actionPath.m_userPath.c_str(), actionPath.m_componentPath.c_str());
                continue;
            }

            XrPath xrBindingPath;
            XrResult result = xrStringToPath(m_xrInstance, pathInfo.m_absolutePath.c_str(), &xrBindingPath);
            if (IsError(result))
            {
                PrintXrError(LogName, result, "Failed to create XrPath for action with profile [%s], absolute path [%s].\n",
                    actionPath.m_interactionProfile.c_str(), pathInfo.m_absolutePath.c_str());
                continue;
            }

            auto interactionProfilePathStr = interactionProviderIface->GetInteractionProviderPath();
            XrPath xrProviderPath;
            result = xrStringToPath(m_xrInstance, interactionProfilePathStr.c_str(), &xrProviderPath);
            if (IsError(result))
            {
                PrintXrError(LogName, result, "Failed to create XrPath for action provider [%s], provider path [%s].\n",
                    actionPath.m_interactionProfile.c_str(), interactionProfilePathStr.c_str());
                continue;
            }
            activeProfiles.emplace(xrProviderPath);

            XrActionSuggestedBinding binding;
            binding.action = newXrAction;
            binding.binding = xrBindingPath;
            activeBindings.push_back(binding);
            additionalBindingsCount++;
        }

        return additionalBindingsCount;
    }

    bool ActionsManager::InitActionBindingsInternal(ActionSetInfo& actionSetInfo, const OpenXRAction& action,
        AZStd::unordered_set<XrPath>& activeProfiles,
        AZStd::vector<XrActionSuggestedBinding>& activeBindings)
    {
        // One OpenXRAction object will become one XrAction.
        // An OpenXRAction contains a list of OpenXRActionPath that need to be bound.
        // The action type for each XrAction will be the same and it will be determined by
        // the action type of the first action in the list. 
        AZ_Assert(!action.m_actionPaths.empty(), "OpenXR Actions list must contain at least one action.");
        XrActionType firstActionType = GetActionTypeFromOpenXRActionPath(action.m_actionPaths[0]);

        XrSpace newXrActionSpace = XR_NULL_HANDLE; // Optional.
        XrAction newXrAction = CreateXrActionAndXrSpace(actionSetInfo, action, firstActionType, newXrActionSpace);
        if (newXrAction == XR_NULL_HANDLE)
        {
            return false;
        }

        // For each actionPath in the list, create the XrPath and its binding.
        const auto additionalBindingsCount = AppendActionBindings(action, newXrAction, activeProfiles, activeBindings);
        if (additionalBindingsCount < 1)
        {
            // This action has no bindings. Don't add it to the active actions list.
            AZ_Warning(LogName, false, "The action [%s] had no bindings!.\n", action.m_name.c_str());
            if (newXrActionSpace != XR_NULL_HANDLE)
            {
                xrDestroySpace(newXrActionSpace);
            }
            xrDestroyAction(newXrAction);
            return true;
        }

        m_actions.push_back({});
        auto& newActionInfo = m_actions.back();
        newActionInfo.m_name = action.m_name;
        newActionInfo.m_actionType = firstActionType;
        newActionInfo.m_xrAction = newXrAction;
        newActionInfo.m_xrSpace = newXrActionSpace;

        uint16_t newActionIndex = aznumeric_cast<uint16_t>(m_actions.size() - 1);
        actionSetInfo.m_actions.emplace(action.m_name, IOpenXRActions::ActionHandle(newActionIndex));

        return true;
    }

    /////////////////////////////////////////////////
    /// OpenXRActionsInterface overrides
    AZStd::vector<AZStd::string> ActionsManager::GetAllActionSets() const
    {
        AZStd::vector<AZStd::string> retList;
        retList.reserve(m_actionSets.size());
        for (const auto& actionSetInfo : m_actionSets)
        {
            retList.push_back(actionSetInfo.m_name);
        }
        return retList;
    }


    AZStd::vector<AZStd::string> ActionsManager::GetActiveActionSets() const
    {
        AZStd::vector<AZStd::string> retList;
        retList.reserve(m_activeActionSets.count());
        for (size_t i = 0; i < m_actionSets.size(); i++)
        {
            if (m_activeActionSets[i])
            {
                retList.push_back(m_actionSets[i].m_name);
            }
        }
        return retList;
    }


    AZStd::vector<AZStd::string> ActionsManager::GetInactiveActionSets() const
    {
        AZStd::vector<AZStd::string> retList;
        retList.reserve(m_actionSets.size() - m_activeActionSets.count());
        for (size_t i = 0; i < m_actionSets.size(); i++)
        {
            if (!m_activeActionSets[i])
            {
                retList.push_back(m_actionSets[i].m_name);
            }
        }
        return retList;
    }


    AZ::Outcome<bool, AZStd::string> ActionsManager::ChangeActionSetState(const AZStd::string& actionSetName, bool activate)
    {
        constexpr bool recreateXrActiveActionSets = true;
        return ChangeActionSetStateInternal(actionSetName, activate, recreateXrActiveActionSets);
    }


    AZ::Outcome<bool, AZStd::string> ActionsManager::ChangeActionSetsState(const  AZStd::vector<AZStd::string>& actionSetNames, bool activate)
    {
        constexpr bool recreateXrActiveActionSets = false;
        for (const auto& actionSetName : actionSetNames)
        {
            auto outcome = ChangeActionSetStateInternal(actionSetName, activate, recreateXrActiveActionSets);
            if (!outcome.IsSuccess())
            {
                return outcome;
            }
        }
        RecreateXrActiveActionSets();
        return AZ::Success(true);
    }


    IOpenXRActions::ActionHandle ActionsManager::GetActionHandle(const AZStd::string& actionSetName, const AZStd::string& actionName) const
    {
        for (const auto& actionSetInfo : m_actionSets)
        {
            if (actionSetInfo.m_name != actionSetName)
            {
                continue;
            }
            const auto itor = actionSetInfo.m_actions.find(actionName);
            if (itor == actionSetInfo.m_actions.end())
            {
                return IOpenXRActions::ActionHandle::Null;
            }
            return itor->second;
        }
        return IOpenXRActions::ActionHandle::Null;
    }

    AZ::Outcome<bool, AZStd::string> ActionsManager::GetActionStateBoolean(ActionHandle actionHandle) const
    {
        if (!actionHandle.IsValid())
        {
            return AZ::Failure("Invalid actionHandle!");
        }
        const auto actionIndex = actionHandle.GetIndex();

        XrActionStateBoolean state { XR_TYPE_ACTION_STATE_BOOLEAN };
        XrActionStateGetInfo getInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
        getInfo.action = m_actions[actionIndex].m_xrAction;
        XrResult result = xrGetActionStateBoolean(m_xrSession, &getInfo, &state);
        if (IsError(result))
        {
            return AZ::Failure(AZStd::string(GetResultString(result)));
        }

        return AZ::Success(state.currentState);
    }

    AZ::Outcome<float, AZStd::string> ActionsManager::GetActionStateFloat(ActionHandle actionHandle) const
    {
        if (!actionHandle.IsValid())
        {
            return AZ::Failure("Invalid actionHandle!");
        }
        const auto actionIndex = actionHandle.GetIndex();

        XrActionStateFloat state{ XR_TYPE_ACTION_STATE_FLOAT };
        XrActionStateGetInfo getInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
        getInfo.action = m_actions[actionIndex].m_xrAction;
        XrResult result = xrGetActionStateFloat(m_xrSession, &getInfo, &state);
        if (IsError(result))
        {
            return AZ::Failure(AZStd::string(GetResultString(result)));
        }

        return AZ::Success(state.currentState);
    }

    AZ::Outcome<AZ::Vector2, AZStd::string> ActionsManager::GetActionStateVector2(ActionHandle actionHandle) const
    {
        if (!actionHandle.IsValid())
        {
            return AZ::Failure("Invalid actionHandle!");
        }
        const auto actionIndex = actionHandle.GetIndex();

        XrActionStateVector2f state{ XR_TYPE_ACTION_STATE_VECTOR2F };
        XrActionStateGetInfo getInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
        getInfo.action = m_actions[actionIndex].m_xrAction;
        XrResult result = xrGetActionStateVector2f(m_xrSession, &getInfo, &state);
        if (IsError(result))
        {
            return AZ::Failure(AZStd::string(GetResultString(result)));
        }

        return AZ::Success(AZ::Vector2(state.currentState.x, state.currentState.y));
    }

    AZ::Outcome<bool, AZStd::string> ActionsManager::SetBaseVisualizedSpaceForPoseActions(const AZStd::string& visualizedSpaceName)
    {
        if (visualizedSpaceName == m_baseVisualizedSpaceName)
        {
            return AZ::Success(true);
        }

        auto visualizedSpacesIface = OpenXRVisualizedSpacesInterface::Get();
        if (!visualizedSpacesIface)
        {
            AZ_Assert(false, "The OpenXRVisualizedSpacesInterface doesn't exist!");
            return AZ::Failure("The OpenXRVisualizedSpacesInterface doesn't exist!");
        }

        const void* opaqueXrSpace = visualizedSpacesIface->GetVisualizedSpaceNativeHandle(visualizedSpaceName);
        if (!opaqueXrSpace)
        {
            return AZ::Failure(
                AZStd::string::format("Visualized space with name [%s] doesn't exist. Will keep the current base space named [%s]",
                                      visualizedSpaceName.c_str(), m_baseVisualizedSpaceName.c_str())
            );
        }
        m_baseVisualizedSpaceName = visualizedSpaceName;
        m_xrBaseVisualizedSpace = reinterpret_cast<XrSpace>(const_cast<void*>(opaqueXrSpace));
        return AZ::Success(true);
    }

    const AZStd::string& ActionsManager::GetBaseVisualizedSpaceForPoseActions() const
    {
        return m_baseVisualizedSpaceName;
    }

    AZ::Outcome<AZ::Transform, AZStd::string> ActionsManager::GetActionStatePose(ActionHandle actionHandle) const
    {
        if (!actionHandle.IsValid())
        {
            return AZ::Failure("Invalid actionHandle!");
        }
        const auto actionIndex = actionHandle.GetIndex();

        XrSpaceLocation spaceLocation {XR_TYPE_SPACE_LOCATION};
        XrResult result = xrLocateSpace(m_actions[actionIndex].m_xrSpace, m_xrBaseVisualizedSpace, m_predictedDisplaytime, &spaceLocation);
        if (IsError(result))
        {
            return AZ::Failure(AZStd::string(GetResultString(result)));
        }

        AZ::Vector3 poseLocation = AZ::Vector3::CreateZero();
        if (spaceLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)
        {
            poseLocation = AzPositionFromXrPose(spaceLocation.pose);
        }
        
        AZ::Quaternion poseOrientation = AZ::Quaternion::CreateIdentity();
        if (spaceLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)
        {
            poseOrientation = AzQuaternionFromXrPose(spaceLocation.pose);
        }

        AZ::Transform retPoseTransform = AZ::Transform::CreateFromQuaternionAndTranslation(poseOrientation, poseLocation);
        return AZ::Success(retPoseTransform);
    }

    AZ::Outcome<PoseWithVelocities, AZStd::string> ActionsManager::GetActionStatePoseWithVelocities(ActionHandle actionHandle) const
    {
        if (!actionHandle.IsValid())
        {
            return AZ::Failure("Invalid actionHandle!");
        }
        const auto actionIndex = actionHandle.GetIndex();

        XrSpaceVelocity spaceVelocity{ XR_TYPE_SPACE_VELOCITY };
        XrSpaceLocation spaceLocation{ XR_TYPE_SPACE_LOCATION, &spaceVelocity};
        XrResult result = xrLocateSpace(m_actions[actionIndex].m_xrSpace, m_xrBaseVisualizedSpace, m_predictedDisplaytime, &spaceLocation);
        if (IsError(result))
        {
            return AZ::Failure(AZStd::string(GetResultString(result)));
        }

        AZ::Vector3 poseLocation = AZ::Vector3::CreateZero();
        AZ::Vector3 linearVelocity = AZ::Vector3::CreateZero();
        if (spaceLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)
        {
            poseLocation = AzPositionFromXrPose(spaceLocation.pose);
            if (spaceVelocity.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT)
            {
                linearVelocity = AzVector3FromXrVector3(spaceVelocity.linearVelocity);
            }
        }

        AZ::Quaternion poseOrientation = AZ::Quaternion::CreateIdentity();
        AZ::Vector3 angularVelocity = AZ::Vector3::CreateZero();
        if (spaceLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)
        {
            poseOrientation = AzQuaternionFromXrPose(spaceLocation.pose);
            if (spaceVelocity.velocityFlags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT)
            {
                angularVelocity = AzVector3FromXrVector3(spaceVelocity.angularVelocity);
            }
        }

        PoseWithVelocities retPoseWithVelocities { 
            AZ::Transform::CreateFromQuaternionAndTranslation(poseOrientation, poseLocation),
            linearVelocity,
            angularVelocity
        };
        return AZ::Success(retPoseWithVelocities);
    }


    AZ::Outcome<bool, AZStd::string> ActionsManager::ApplyHapticVibrationAction(ActionHandle actionHandle,
        uint64_t durationNanos, float frequencyHz, float amplitude)
    {
        if (!actionHandle.IsValid())
        {
            return AZ::Failure("Invalid actionHandle!");
        }
        const auto actionIndex = actionHandle.GetIndex();

        // fire haptics using output action
        XrHapticVibration vibration{ XR_TYPE_HAPTIC_VIBRATION };
        vibration.amplitude = AZStd::clamp(amplitude, 0.0f, 1.0f);
        vibration.duration = durationNanos;
        vibration.frequency = frequencyHz;
        XrHapticActionInfo hapticActionInfo{ XR_TYPE_HAPTIC_ACTION_INFO };
        hapticActionInfo.action = m_actions[actionIndex].m_xrAction;
        XrResult result = xrApplyHapticFeedback(m_xrSession, &hapticActionInfo, (const XrHapticBaseHeader*)&vibration);
        if (IsError(result))
        {
            return AZ::Failure(AZStd::string(GetResultString(result)));
        }
        return AZ::Success(true);
    }

    AZ::Outcome<bool, AZStd::string> ActionsManager::StopHapticVibrationAction(ActionHandle actionHandle)
    {
        if (!actionHandle.IsValid())
        {
            return AZ::Failure("Invalid actionHandle!");
        }
        const auto actionIndex = actionHandle.GetIndex();

        // fire haptics using output action
        XrHapticActionInfo hapticActionInfo{ XR_TYPE_HAPTIC_ACTION_INFO };
        hapticActionInfo.action = m_actions[actionIndex].m_xrAction;
        XrResult result = xrStopHapticFeedback(m_xrSession, &hapticActionInfo);
        if (IsError(result))
        {
            return AZ::Failure(AZStd::string(GetResultString(result)));
        }
        return AZ::Success(true);
    }
    /// OpenXRActionsInterface overrides
    /////////////////////////////////////////////////
    AZ::Outcome<bool, AZStd::string> ActionsManager::ChangeActionSetStateInternal(const AZStd::string& actionSetName, bool activate, bool recreateXrActiveActionSets)
    {
        // First get the index.
        size_t foundIdx = 0;
        for (const auto& actionSetInfo : m_actionSets)
        {
            if (actionSetInfo.m_name == actionSetName)
            {
                break;
            }
            foundIdx++;
        }

        if (foundIdx >= m_actionSets.size())
        {
            return AZ::Failure(AZStd::string::format(
                "ActionSet with name [%s] not found.", actionSetName.c_str()));
        }

        m_activeActionSets.set(foundIdx, activate);

        if (recreateXrActiveActionSets)
        {
            RecreateXrActiveActionSets();
        }

        return AZ::Success(true);
    }

    void ActionsManager::RecreateXrActiveActionSets()
    {
        // Recreate and cache the XrActionActionSet list.
        m_xrActiveActionSets.clear();
        for (size_t i = 0; i < m_actionSets.size(); i++)
        {
            if (m_activeActionSets[i])
            {
                XrActiveActionSet activeActionSet{ m_actionSets[i].m_xrActionSet, XR_NULL_PATH };
                m_xrActiveActionSets.push_back(activeActionSet);
            }
        }
    }

} // namespace OpenXRVk
