/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/RTTI/RTTI.h>
#include <AzCore/RTTI/ReflectContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/ObjectStream.h>
#include <AzCore/Asset/AssetCommon.h>

#include "InteractionProfiles/OpenXRInteractionProfileDescriptor.h"


namespace OpenXRVk
{
    //! This asset defines a list of Interaction Profile Descriptors.
    //! The Engine only needs one of these assets, which is used to express
    //! all the different interaction profiles (aka XR Headset Devices) that
    //! are supported by OpenXR.
    //! Basically this asset contains data as listed here:
    //! https://registry.khronos.org/OpenXR/specs/1.0/html/xrspec.html#semantic-path-interaction-profiles
    class OpenXRInteractionProfilesAsset final
        : public AZ::Data::AssetData
    {
    public:
        AZ_RTTI(OpenXRActionBindingsAsset, "{02555DCD-E363-42FB-935C-4E67CC3A1699}", AZ::Data::AssetData);
        AZ_CLASS_ALLOCATOR(OpenXRActionBindingsAsset, AZ::SystemAllocator);
        static void Reflect(AZ::ReflectContext* context);

        static constexpr char s_assetTypeName[] = "OpenXR Interaction Profiles";
        static constexpr char s_assetExtension[] = "xrprofiles";

        AZStd::vector<OpenXRInteractionProfileDescriptor> m_interactionProfileDescriptors;
    };
}// namespace OpenXRVk
