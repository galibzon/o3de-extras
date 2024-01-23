#
# Copyright (c) Contributors to the Open 3D Engine Project.
# For complete copyright and license terms please see the LICENSE at the root of this distribution.
#
# SPDX-License-Identifier: Apache-2.0 OR MIT
#
#

set(FILES
    Include/OpenXRVk/InputDeviceXRController.h
    Include/OpenXRVk/OpenXRVkDevice.h
    Include/OpenXRVk/OpenXRVkInput.h
    Include/OpenXRVk/OpenXRVkInstance.h
    Include/OpenXRVk/OpenXRVkPhysicalDevice.h
    Include/OpenXRVk/OpenXRVkSession.h
    Include/OpenXRVk/OpenXRVkSpace.h
    Include/OpenXRVk/OpenXRVkSwapChain.h
    Include/OpenXRVk/OpenXRVkSystemComponent.h
    Include/OpenXRVk/OpenXRVkUtils.h
    Include/OpenXRVk/OpenXRActionsInterface.h
    Include/OpenXRVk/OpenXRVisualizedSpacesInterface.h
    Include/OpenXRVk/OpenXRInteractionProfileDescriptor.h
    Include/OpenXRVk/OpenXRInteractionProfilesAsset.h
    Include/OpenXRVk/OpenXRActionSetDescriptor.h
    Include/OpenXRVk/OpenXRActionSetsAsset.h
    Source/InputDeviceXRController.cpp
    Source/OpenXRVkCommon.h
    Source/OpenXRVkDevice.cpp
    Source/OpenXRVkInput.cpp
    Source/OpenXRVkInstance.cpp
    Source/OpenXRVkPhysicalDevice.cpp
    Source/OpenXRVkSession.cpp
    Source/OpenXRVkSpace.cpp
    Source/OpenXRVkSwapChain.cpp
    Source/OpenXRVkSystemComponent.cpp
    Source/OpenXRVkUtils.cpp
    Source/InteractionProfiles/OpenXRInteractionProfilesProviderSystemComponent.cpp
    Source/InteractionProfiles/OpenXRInteractionProfilesProviderSystemComponent.h
    Source/InteractionProfiles/OpenXRInteractionProfileDescriptor.cpp
    Source/InteractionProfiles/OpenXRInteractionProfilesAsset.cpp
    Source/InteractionProfiles/OpenXRInteractionProfilesProviderInterface.h
    Source/OpenXRActionSetsAsset.cpp
    Source/OpenXRActionSetDescriptor.cpp
    Source/XRCameraMovementComponent.cpp
    Source/XRCameraMovementComponent.h
#    Source/OpenXRActionsManager.cpp
#    Source/OpenXRActionsManager.h
#    Source/OpenXRVisualizedSpacesManager.cpp
#    Source/OpenXRVisualizedSpacesManager.h
)
